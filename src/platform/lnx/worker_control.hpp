#pragma once

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <stop_token>
#include <system_error>
#include <utility>
#include <thread>

#include <sys/eventfd.h>
#include <unistd.h>

namespace platform::lnx
{

class stop_signal final
{
    struct signal
    {
        int fd;
        void operator()() const noexcept
        {
            const std::uint64_t one = 1;
            ::write(fd, &one, sizeof(one));
        }
    };

    static int create()
    {
        const int fd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        return fd;
    }

public:
    explicit stop_signal(std::stop_token stop)
        : m_fd(create())
        , m_callback(stop, signal{m_fd})
    {
    }
    ~stop_signal()
    {
        if (m_fd >= 0)
            ::close(m_fd);
    }
    [[nodiscard]] int get() const noexcept { return m_fd; }

private:
    int m_fd{-1};
    std::stop_callback<signal> m_callback;
};

struct worker_control
{
    std::stop_token stop;
    int wake{-1};

    [[nodiscard]] bool stopping() const noexcept { return stop.stop_requested(); }

    void wait_handle(int fd) const
    {
        if (fd < 0)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            return;
        }
        std::uint64_t value{};
        ::read(fd, &value, sizeof(value));
    }

    template<class Rep, class Period>
    void sleep_for(std::chrono::duration<Rep, Period> duration) const
    {
        if (duration <= std::chrono::duration<Rep, Period>::zero())
            return;
        std::this_thread::sleep_for(duration);
    }

    void sleep_until(std::chrono::steady_clock::time_point deadline) const
    {
        sleep_for(deadline - std::chrono::steady_clock::now());
    }
};

class precise_worker_wait final
{
public:
    explicit precise_worker_wait(std::stop_token stop)
        : m_stop(stop)
    {
    }

    template<class Rep, class Period>
    void sleep_for(std::chrono::duration<Rep, Period> duration) const
    {
        if (m_stop.stop_requested())
            return;
        std::this_thread::sleep_for(duration);
    }

    void sleep_until(std::chrono::steady_clock::time_point deadline) const
    {
        sleep_for(deadline - std::chrono::steady_clock::now());
    }

private:
    std::stop_token m_stop;
};

} // namespace platform::lnx
