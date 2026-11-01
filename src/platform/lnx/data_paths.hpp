#pragma once
#include <string>
#include <filesystem>
#include <cstdint>
#include <stop_token>

namespace platform::lnx::data_storage
{
[[nodiscard]] std::filesystem::path extended_path(const std::filesystem::path& path);
[[nodiscard]] std::filesystem::path plain_path(const std::filesystem::path& path);
struct locations
{
    std::filesystem::path root, configs, cache, logs, lua, mesh_cache, research_logs;
    [[nodiscard]] static locations under(std::filesystem::path root);
};
struct resolved_locations
{
    locations destination;
    std::filesystem::path legacy;
    std::uint32_t error{};
};
[[nodiscard]] resolved_locations resolve_current_user() noexcept;

enum class migration_stage : std::uint8_t
{
    none,
    validate_paths,
    create_directories,
    lock,
    marker,
    enumerate,
    open_source,
    copy,
    verify,
    publish,
    cancelled
};
struct migration_result
{
    migration_stage stage{};
    std::uint32_t error{};
    std::uint64_t copied{}, retained{};
    bool already_complete{};
    [[nodiscard]] explicit operator bool() const noexcept { return error == 0; }
};
[[nodiscard]] migration_result prepare(const locations& destination, std::stop_token stop = {}) noexcept;
[[nodiscard]] migration_result migrate(const locations& destination, const std::filesystem::path& legacy,
                                       std::stop_token stop = {}) noexcept;
} // namespace platform::lnx::data_storage
