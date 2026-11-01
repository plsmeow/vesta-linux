#include <platform/lnx/open_folder.hpp>

#include <filesystem>
#include <cstdlib>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

namespace platform::lnx
{
bool open_folder(const std::filesystem::path& folder) noexcept
{
    try
    {
        std::error_code error;
        if (!std::filesystem::is_directory(folder, error))
            return false;
        const auto path_str = folder.string();
        const pid_t pid = ::fork();
        if (pid < 0)
            return false;
        if (pid == 0)
        {
            ::setsid();
            ::execlp("xdg-open", "xdg-open", path_str.c_str(), static_cast<char*>(nullptr));
            ::_exit(127);
        }
        int status{};
        ::waitpid(pid, &status, 0);
        return WIFEXITED(status) && WEXITSTATUS(status) == 0;
    }
    catch (...)
    {
        return false;
    }
}
} // namespace platform::lnx
