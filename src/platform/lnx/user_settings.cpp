#include <platform/lnx/user_settings.hpp>

#include <cstdlib>
#include <fstream>
#include <string>

namespace platform::lnx
{
namespace
{
[[nodiscard]] std::filesystem::path settings_file() noexcept
{
    const char* config_home = ::getenv("XDG_CONFIG_HOME");
    std::filesystem::path base;
    if (config_home && config_home[0])
        base = config_home;
    else
    {
        const char* home = ::getenv("HOME");
        if (!home || !home[0])
            return {};
        base = std::filesystem::path(home) / ".config";
    }
    return base / "vesta" / "settings.conf";
}
} // namespace

std::optional<std::filesystem::path> read_config_folder() noexcept
{
    try
    {
        const auto file = settings_file();
        if (file.empty())
            return std::nullopt;
        std::ifstream input(file);
        if (!input.is_open())
            return std::nullopt;
        std::string line;
        while (std::getline(input, line))
        {
            constexpr const char* prefix = "config_folder=";
            if (line.starts_with(prefix))
            {
                std::filesystem::path folder(line.substr(std::string_view(prefix).size()));
                if (folder.is_absolute())
                    return folder;
            }
        }
        return std::nullopt;
    }
    catch (...)
    {
        return std::nullopt;
    }
}

bool write_config_folder(const std::filesystem::path& folder) noexcept
{
    try
    {
        if (!folder.is_absolute())
            return false;
        const auto file = settings_file();
        if (file.empty())
            return false;
        std::error_code error;
        std::filesystem::create_directories(file.parent_path(), error);
        if (error)
            return false;
        std::ofstream output(file, std::ios::trunc);
        if (!output.is_open())
            return false;
        output << "config_folder=" << folder.string() << "\n";
        return output.good();
    }
    catch (...)
    {
        return false;
    }
}

std::error_code move_to_recycle_bin(const std::filesystem::path& path) noexcept
{
    try
    {
        const char* home = ::getenv("HOME");
        if (!home)
            return std::make_error_code(std::errc::not_supported);
        const auto trash_dir = std::filesystem::path(home) / ".local" / "share" / "Trash" / "files";
        std::error_code error;
        std::filesystem::create_directories(trash_dir, error);
        if (error)
            return error;
        const auto dest = trash_dir / path.filename();
        std::filesystem::rename(path, dest, error);
        if (error)
        {
            std::filesystem::copy(path, dest, std::filesystem::copy_options::recursive, error);
            if (error)
                return error;
            std::filesystem::remove_all(path, error);
        }
        return error;
    }
    catch (...)
    {
        return std::make_error_code(std::errc::io_error);
    }
}
} // namespace platform::lnx
