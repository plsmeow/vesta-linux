#pragma once

#include <filesystem>
#include <optional>
#include <system_error>

namespace platform::lnx
{
[[nodiscard]] std::optional<std::filesystem::path> read_config_folder() noexcept;
[[nodiscard]] bool write_config_folder(const std::filesystem::path& folder) noexcept;
[[nodiscard]] std::error_code move_to_recycle_bin(const std::filesystem::path& path) noexcept;
} // namespace platform::lnx
