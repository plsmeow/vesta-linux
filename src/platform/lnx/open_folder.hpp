#pragma once
#include <filesystem>

namespace platform::lnx
{
[[nodiscard]] bool open_folder(const std::filesystem::path& folder) noexcept;
} // namespace platform::lnx
