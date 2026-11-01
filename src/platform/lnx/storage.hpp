#pragma once

#include <platform/lnx/data_paths.hpp>

#include <filesystem>
#include <string_view>
#include <system_error>

namespace platform::lnx::runtime_storage
{

[[nodiscard]] data_storage::migration_result initialize();
[[nodiscard]] data_storage::migration_result initialize(data_storage::resolved_locations locations);
[[nodiscard]] const data_storage::locations& paths() noexcept;

} // namespace platform::lnx::runtime_storage

namespace platform::lnx
{
[[nodiscard]] std::error_code write_file_atomically(const std::filesystem::path& target,
                                                    std::string_view contents) noexcept;
} // namespace platform::lnx
