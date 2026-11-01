#include <diagnostics/crash_record.hpp>
#include <diagnostics/build_identity.hpp>
#include <diagnostics/log_directory.hpp>

#include <array>
#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <dlfcn.h>
#include <fcntl.h>
#include <filesystem>
#include <unistd.h>

namespace diagnostics
{
namespace
{
constexpr std::size_t path_capacity = 1024;
constexpr std::size_t record_limit = 16 * 1024;
char g_path[path_capacity]{};
std::atomic_flag g_recording = ATOMIC_FLAG_INIT;
std::array<struct sigaction, 6> g_previous{};
constexpr std::array<int, 6> g_signals{SIGSEGV, SIGABRT, SIGBUS, SIGILL, SIGFPE, SIGTRAP};

struct line_buffer
{
    char data[512]{};
    std::size_t size{};
    void text(const char* value) noexcept
    {
        while (*value && size < sizeof(data) - 1)
            data[size++] = *value++;
    }
    void decimal(std::uint64_t value) noexcept
    {
        char digits[20]{};
        std::size_t count{};
        do
        {
            digits[count++] = static_cast<char>('0' + value % 10);
            value /= 10;
        } while (value && count < sizeof(digits));
        while (count && size < sizeof(data) - 1)
            data[size++] = digits[--count];
    }
    void hex(std::uint64_t value) noexcept
    {
        text("0x");
        char digits[16]{};
        std::size_t count{};
        do
        {
            digits[count++] = "0123456789ABCDEF"[value & 0xf];
            value >>= 4;
        } while (value && count < sizeof(digits));
        while (count && size < sizeof(data) - 1)
            data[size++] = digits[--count];
    }
};

void crash_handler(int signal_number, siginfo_t* info, void*) noexcept
{
    line_buffer line;
    line.text("crash version=" VESTA_JOURNAL_VERSION " build=" VESTA_JOURNAL_REVISION " signal=");
    line.decimal(static_cast<std::uint64_t>(signal_number));
    line.text(" address=");
    line.hex(reinterpret_cast<std::uintptr_t>(info ? info->si_addr : nullptr));
    line.text("\n");
    const int file = ::open(g_path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0600);
    if (file >= 0)
    {
        (void)::write(file, line.data, line.size);
        ::close(file);
    }
    ::signal(signal_number, SIG_DFL);
    ::raise(signal_number);
}
}

bool take_previous_crash(const std::filesystem::path& directory) noexcept
{
    try
    {
        const auto target = directory.empty() ? default_logs_directory() : directory;
        if (target.empty())
            return false;
        const auto current = target / "crash.log";
        std::error_code error;
        if (!std::filesystem::is_regular_file(current, error) || std::filesystem::file_size(current, error) == 0)
            return false;
        std::filesystem::rename(current, target / "crash.previous.log", error);
        return !error;
    }
    catch (...)
    {
        return false;
    }
}

bool install_crash_recorder(const std::filesystem::path& directory) noexcept
{
    try
    {
        const auto target = directory.empty() ? default_logs_directory() : directory;
        if (target.empty())
            return false;
        std::error_code error;
        std::filesystem::create_directories(target, error);
        if (error)
            return false;
        const auto file = (target / "crash.log").string();
        if (file.empty() || file.size() >= path_capacity)
            return false;
        std::memcpy(g_path, file.c_str(), file.size() + 1);
        struct sigaction action{};
        sigemptyset(&action.sa_mask);
        action.sa_sigaction = crash_handler;
        action.sa_flags = SA_SIGINFO | SA_RESETHAND;
        for (std::size_t index = 0; index < g_signals.size(); ++index)
            if (::sigaction(g_signals[index], &action, &g_previous[index]) != 0)
                return false;
        return true;
    }
    catch (...)
    {
        return false;
    }
}
} // namespace diagnostics
