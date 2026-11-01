#include <diagnostics/journal.hpp>
#include <diagnostics/build_identity.hpp>
#include <diagnostics/log_directory.hpp>

#include <foundation/scope_exit.hpp>
#include <sys/eventfd.h>
#include <algorithm>
#include <array>
#include <poll.h>
#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/random.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

namespace diagnostics
{
namespace
{
constexpr const char* build_configuration = VESTA_JOURNAL_CONFIGURATION;

class file_sink
{
public:
    file_sink(const journal_options& options, std::atomic<std::uint32_t>& error,
              std::atomic<std::uint32_t>& operation, std::atomic<std::uint64_t>& retries)
        : m_error(error), m_operation(operation), m_retries(retries), m_limit(options.file_bytes)
    {
        auto directory = options.directory.empty() ? default_logs_directory() : options.directory;
        if (directory.empty())
        {
            fail(ENOENT, storage_stage::initialize);
            return;
        }
        std::error_code ec;
        std::filesystem::create_directories(directory, ec);
        if (ec)
        {
            fail(static_cast<std::uint32_t>(ec.value()), storage_stage::initialize);
            return;
        }
        m_path = directory / "journal.log";
        for (std::size_t i = 0; i < m_backups.size(); ++i)
            m_backups[i] = directory / ("journal." + std::to_string(i + 1) + ".log");
        if (::getrandom(&m_session, sizeof(m_session), 0) != static_cast<ssize_t>(sizeof(m_session)))
        {
            fail(static_cast<std::uint32_t>(errno), storage_stage::initialize);
            return;
        }
        m_gate = ::open((directory / options.writer_gate).c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0600);
        if (m_gate < 0)
        {
            fail(static_cast<std::uint32_t>(errno), storage_stage::gate);
            return;
        }
        m_ready = true;
    }
    ~file_sink() { end(); if (m_gate >= 0) ::close(m_gate); }
    std::uint64_t session() const noexcept { return m_session; }
    bool begin() noexcept
    {
        if (!m_ready)
            return false;
        while (::flock(m_gate, LOCK_EX | LOCK_NB) != 0)
        {
            if (errno != EWOULDBLOCK && errno != EINTR)
            {
                fail(static_cast<std::uint32_t>(errno), storage_stage::gate);
                return false;
            }
            m_retries.fetch_add(1, std::memory_order_relaxed);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        m_locked = true;
        return true;
    }
    void end() noexcept
    {
        if (m_file >= 0)
        {
            ::close(m_file);
            m_file = -1;
        }
        if (m_locked)
        {
            ::flock(m_gate, LOCK_UN);
            m_locked = false;
        }
    }
    bool line(std::string_view text) noexcept
    {
        if (!m_locked)
            return false;
        char header[256]{};
        const int header_size = std::snprintf(header, sizeof(header), "schema=1 version=%s build=%s config=%s\n",
                                              VESTA_JOURNAL_VERSION, VESTA_JOURNAL_REVISION, build_configuration);
        if (header_size <= 0 || static_cast<std::size_t>(header_size) >= sizeof(header))
            return false;
        if (!open_file())
            return false;
        if (m_size + text.size() + (m_size ? 0u : static_cast<std::size_t>(header_size)) > m_limit)
        {
            ::close(m_file);
            m_file = -1;
            if (!rotate() || !open_file())
                return false;
        }
        if (!m_size && !write({header, static_cast<std::size_t>(header_size)}))
            return false;
        return write(text);
    }
private:
    void fail(std::uint32_t code, storage_stage stage) noexcept
    {
        m_operation.store(static_cast<std::uint32_t>(stage), std::memory_order_relaxed);
        m_error.store(code, std::memory_order_release);
    }
    bool open_file() noexcept
    {
        if (m_file >= 0)
            return true;
        m_file = ::open(m_path.c_str(), O_CREAT | O_WRONLY | O_APPEND | O_CLOEXEC, 0600);
        if (m_file < 0)
        {
            fail(static_cast<std::uint32_t>(errno), storage_stage::open);
            return false;
        }
        struct stat info{};
        if (::fstat(m_file, &info) != 0)
        {
            fail(static_cast<std::uint32_t>(errno), storage_stage::seek);
            return false;
        }
        m_size = static_cast<std::size_t>(info.st_size);
        return true;
    }
    bool write(std::string_view text) noexcept
    {
        std::size_t offset{};
        while (offset < text.size())
        {
            const auto count = ::write(m_file, text.data() + offset, text.size() - offset);
            if (count < 0 && errno == EINTR)
                continue;
            if (count <= 0)
            {
                fail(static_cast<std::uint32_t>(errno), storage_stage::write);
                return false;
            }
            offset += static_cast<std::size_t>(count);
            m_size += static_cast<std::size_t>(count);
        }
        return true;
    }
    bool rotate() noexcept
    {
        std::error_code ec;
        for (std::size_t i = m_backups.size(); i > 0; --i)
        {
            const auto& source = i == 1 ? m_path : m_backups[i - 2];
            if (!std::filesystem::exists(source, ec))
                continue;
            std::filesystem::rename(source, m_backups[i - 1], ec);
            if (ec)
            {
                fail(static_cast<std::uint32_t>(ec.value()), storage_stage::rotate);
                return false;
            }
        }
        m_size = 0;
        return true;
    }
    std::atomic<std::uint32_t>& m_error;
    std::atomic<std::uint32_t>& m_operation;
    std::atomic<std::uint64_t>& m_retries;
    std::size_t m_limit{};
    std::filesystem::path m_path;
    std::array<std::filesystem::path, journal::backup_count> m_backups{};
    int m_gate{-1};
    int m_file{-1};
    std::uint64_t m_session{};
    std::size_t m_size{};
    bool m_ready{}, m_locked{};
};
}

void journal::lock_owner() noexcept
{
    while (m_queue_lock.test_and_set(std::memory_order_acquire))
        std::this_thread::yield();
}

bool journal::start() noexcept
{
    return start(journal_options{});
}

bool journal::start(journal_options options) noexcept
{
    if (m_thread.joinable() || options.file_bytes < 4096 || options.file_bytes > 64 * 1024 ||
        options.writer_gate.empty())
    {
        m_storage_error.store(EINVAL, std::memory_order_release);
        return false;
    }
    m_wake = ::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (m_wake < 0)
    {
        m_storage_error.store(static_cast<std::uint32_t>(errno), std::memory_order_release);
        return false;
    }
    m_head = m_count = 0;
    m_sequence = 0;
    m_origin_ms = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
    m_submitted = m_accepted = m_dropped = m_pending_dropped = m_suppressed = 0;
    m_storage_lost = m_storage_retries = m_storage_error = m_storage_operation = 0;
    m_accepting.store(true, std::memory_order_release);
    try
    {
        m_thread = std::thread([this, options = std::move(options)]() mutable { writer(std::move(options)); });
    }
    catch (...)
    {
        m_accepting.store(false, std::memory_order_release);
        ::close(m_wake);
        m_wake = -1;
        m_storage_error.store(ENOMEM, std::memory_order_release);
        return false;
    }
    return true;
}

void journal::stop() noexcept
{
    if (!m_thread.joinable())
        return;
    lock_owner();
    m_accepting.store(false, std::memory_order_release);
    const std::uint64_t one = 1;
    (void)::write(m_wake, &one, sizeof(one));
    m_queue_lock.clear(std::memory_order_release);
    m_thread.join();
    ::close(m_wake);
    m_wake = -1;
}

bool journal::submit(event id, severity level, code_domain domain, std::uint32_t code, subject owner) noexcept
{
    if (static_cast<std::size_t>(id) >= event_count || level > severity::error || domain > code_domain::last ||
        owner > subject::last || !m_accepting.load(std::memory_order_acquire))
        return false;
    m_submitted.fetch_add(1, std::memory_order_relaxed);
    const auto drop = [this]() noexcept
    {
        m_dropped.fetch_add(1, std::memory_order_relaxed);
        m_pending_dropped.fetch_add(1, std::memory_order_relaxed);
        return false;
    };
    if (m_queue_lock.test_and_set(std::memory_order_acquire))
        return drop();
    const foundation::scope_exit unlock([this]() noexcept { m_queue_lock.clear(std::memory_order_release); });
    const auto capacity = level == severity::error ? queue_capacity : queue_capacity - error_reserve;
    if (!m_accepting.load(std::memory_order_acquire) || m_count >= capacity)
        return drop();
    const auto now = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
    m_queue[(m_head + m_count) % queue_capacity] = {
        ++m_sequence, now - m_origin_ms, id, level, domain, owner, domain == code_domain::none ? 0 : code};
    ++m_count;
    m_accepted.fetch_add(1, std::memory_order_relaxed);
    const std::uint64_t one = 1;
    (void)::write(m_wake, &one, sizeof(one));
    return true;
}

journal_stats journal::stats() const noexcept
{
    return {m_submitted.load(std::memory_order_relaxed), m_accepted.load(std::memory_order_relaxed),
            m_dropped.load(std::memory_order_relaxed), m_suppressed.load(std::memory_order_relaxed),
            m_storage_lost.load(std::memory_order_relaxed), m_storage_retries.load(std::memory_order_relaxed),
            m_storage_error.load(std::memory_order_acquire),
            static_cast<storage_stage>(m_storage_operation.load(std::memory_order_relaxed))};
}

void journal::writer(journal_options options) noexcept
{
    const foundation::scope_exit close_admission([this]() noexcept
    {
        lock_owner();
        m_accepting.store(false, std::memory_order_release);
        m_storage_lost.fetch_add(m_count, std::memory_order_relaxed);
        m_count = 0;
        m_queue_lock.clear(std::memory_order_release);
    });
    try
    {
        file_sink output(options, m_storage_error, m_storage_operation, m_storage_retries);
        struct recent { record last{}; std::uint64_t repeats{}; bool present{}; };
        std::array<recent, event_count> history{};
        std::array<record, 64> batch{};
        std::uint64_t output_sequence{};
        const auto write_record = [&](const record& item, std::uint64_t repeats)
        {
            char line[512]{};
            const auto label = name(item.id);
            const int count = std::snprintf(line, sizeof(line),
                "session=%016llx seq=%llu event_seq=%llu t_ms=%llu event=%.*s level=%u domain=%u subject=%u code=%u repeat=%llu version=%s build=%s config=%s\n",
                static_cast<unsigned long long>(output.session()), static_cast<unsigned long long>(++output_sequence),
                static_cast<unsigned long long>(item.sequence), static_cast<unsigned long long>(item.elapsed_ms),
                static_cast<int>(label.size()), label.data(), static_cast<unsigned>(item.level),
                static_cast<unsigned>(item.domain), static_cast<unsigned>(item.owner), item.code,
                static_cast<unsigned long long>(repeats), VESTA_JOURNAL_VERSION, VESTA_JOURNAL_REVISION,
                build_configuration);
            if (count <= 0 || static_cast<std::size_t>(count) >= sizeof(line) ||
                !output.line({line, static_cast<std::size_t>(count)}))
                m_storage_lost.fetch_add(repeats ? repeats : 1, std::memory_order_relaxed);
        };
        for (;;)
        {
            pollfd descriptor{m_wake, POLLIN, 0};
            if (::poll(&descriptor, 1, -1) < 0 && errno != EINTR)
            {
                m_storage_error.store(static_cast<std::uint32_t>(errno), std::memory_order_release);
                return;
            }
            std::uint64_t wake_count{};
            (void)::read(m_wake, &wake_count, sizeof(wake_count));
            bool finished{};
            for (;;)
            {
                lock_owner();
                const auto size = std::min(m_count, batch.size());
                for (std::size_t i = 0; i < size; ++i)
                    batch[i] = m_queue[(m_head + i) % queue_capacity];
                m_head = (m_head + size) % queue_capacity;
                m_count -= size;
                finished = !m_accepting.load(std::memory_order_relaxed) && m_count == 0;
                const auto lost = m_pending_dropped.exchange(0, std::memory_order_relaxed);
                m_queue_lock.clear(std::memory_order_release);
                if (size == 0 && lost == 0)
                    break;
                if (!output.begin())
                {
                    m_storage_lost.fetch_add(size, std::memory_order_relaxed);
                    continue;
                }
                const foundation::scope_exit end_output([&]() noexcept { output.end(); });
                if (lost)
                {
                    char line[256]{};
                    const int n = std::snprintf(line, sizeof(line), "session=%016llx seq=%llu dropped=%llu version=%s build=%s config=%s\n",
                        static_cast<unsigned long long>(output.session()), static_cast<unsigned long long>(++output_sequence),
                        static_cast<unsigned long long>(lost), VESTA_JOURNAL_VERSION, VESTA_JOURNAL_REVISION,
                        build_configuration);
                    if (n > 0 && static_cast<std::size_t>(n) < sizeof(line))
                        (void)output.line({line, static_cast<std::size_t>(n)});
                }
                for (std::size_t i = 0; i < size; ++i)
                {
                    const auto& item = batch[i];
                    auto& previous = history[static_cast<std::size_t>(item.id)];
                    if (previous.present && previous.last.level == item.level && previous.last.domain == item.domain &&
                        previous.last.owner == item.owner && previous.last.code == item.code &&
                        item.elapsed_ms - previous.last.elapsed_ms < 1000)
                    {
                        ++previous.repeats;
                        m_suppressed.fetch_add(1, std::memory_order_relaxed);
                        continue;
                    }
                    if (previous.repeats)
                        write_record(previous.last, previous.repeats);
                    write_record(item, 0);
                    previous = {item, 0, true};
                }
            }
            if (finished)
                break;
        }
        if (output.begin())
        {
            const foundation::scope_exit end_output([&]() noexcept { output.end(); });
            for (const auto& item : history)
                if (item.repeats)
                    write_record(item.last, item.repeats);
        }
    }
    catch (...)
    {
        m_storage_error.store(ENOMEM, std::memory_order_release);
        lock_owner();
        m_accepting.store(false, std::memory_order_release);
        m_storage_lost.fetch_add(m_count, std::memory_order_relaxed);
        m_count = 0;
        m_queue_lock.clear(std::memory_order_release);
    }
}

journal& process_journal() noexcept
{
    static journal value{};
    return value;
}

void emit(event id, severity level, code_domain domain, std::uint32_t code, subject owner) noexcept
{
    (void)process_journal().submit(id, level, domain, code, owner);
}
} // namespace diagnostics
