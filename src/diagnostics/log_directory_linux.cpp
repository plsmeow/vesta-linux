#include <diagnostics/log_directory.hpp>

#include <cstdlib>

namespace diagnostics
{
std::filesystem::path default_logs_directory()
{
    if (const char* data_home = std::getenv("XDG_DATA_HOME"); data_home && *data_home)
        return std::filesystem::path(data_home) / "vesta" / "logs";
    if (const char* home = std::getenv("HOME"); home && *home)
        return std::filesystem::path(home) / ".local" / "share" / "vesta" / "logs";
    return {};
}
} // namespace diagnostics
