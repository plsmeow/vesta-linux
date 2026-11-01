#include <platform/lnx/data_paths.hpp>
#include <cstdlib>
#include <fstream>
#include <algorithm>

namespace platform::lnx::data_storage
{


std::filesystem::path extended_path(const std::filesystem::path& path)
{
    return path.lexically_normal();
}

std::filesystem::path plain_path(const std::filesystem::path& path)
{
    return path;
}
locations locations::under(std::filesystem::path root)
{
    locations value;
    value.root = root;
    if (value.root.empty())
        return value;
    value.configs = value.root / "configs";
    value.cache = value.root / "cache";
    value.logs = value.root / "logs";
    value.lua = value.root / "lua";
    value.mesh_cache = value.cache / "chams";
    value.research_logs = value.logs / "research";
    return value;
}

resolved_locations resolve_current_user() noexcept
{
    resolved_locations result;
    try
    {
        const char* data_home = std::getenv("XDG_DATA_HOME");
        std::filesystem::path root;
        if (data_home && *data_home)
            root = data_home;
        else if (const char* home = std::getenv("HOME"); home && *home)
            root = std::filesystem::path(home) / ".local" / "share";
        else
        {
            result.error = 2;
            return result;
        }
        result.destination = locations::under(root / "vesta");
    }
    catch (...)
    {
        result.error = 12;
    }
    return result;
}

migration_result prepare(const locations& destination, std::stop_token stop) noexcept
{
    try
    {
        if (stop.stop_requested())
            return {migration_stage::cancelled, 0};
        std::error_code error;
        std::filesystem::create_directories(destination.configs, error);
        std::filesystem::create_directories(destination.cache, error);
        std::filesystem::create_directories(destination.logs, error);
        std::filesystem::create_directories(destination.lua, error);
        std::filesystem::create_directories(destination.mesh_cache, error);
        std::filesystem::create_directories(destination.research_logs, error);
        return {migration_stage::none, 0, 0, 0, true};
    }
    catch (...)
    {
        return {migration_stage::copy, 12};
    }
}

migration_result migrate(const locations& destination, const std::filesystem::path& legacy,
                         std::stop_token stop) noexcept
{
    auto result = prepare(destination, stop);
    if (!result)
        return result;
    if (legacy.empty() || !std::filesystem::exists(legacy))
        return result;

    try
    {
        std::error_code error;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(legacy, error))
        {
            if (stop.stop_requested())
                return {migration_stage::cancelled, 0};
            if (entry.is_directory())
                continue;
            const auto relative = std::filesystem::relative(entry.path(), legacy, error);
            if (error)
                continue;
            const auto target = destination.root / relative;
            if (std::filesystem::exists(target, error))
            {
                ++result.retained;
                continue;
            }
            std::filesystem::create_directories(target.parent_path(), error);
            std::filesystem::copy_file(entry.path(), target, std::filesystem::copy_options::skip_existing, error);
            if (!error)
                ++result.copied;
        }
    }
    catch (...)
    {
        return {migration_stage::copy, 12};
    }
    return result;
}

} // namespace platform::lnx::data_storage
