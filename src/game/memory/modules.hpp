#pragma once

#include <cstdint>

#ifdef __linux__
namespace platform::lnx
{
class process_session;
}
#else
namespace platform::windows
{
class process_session;
}
#endif

namespace game
{

class module_catalog
{
public:
#ifdef __linux__
    [[nodiscard]] bool discover(const platform::lnx::process_session& process);
#else
    [[nodiscard]] bool discover(const platform::windows::process_session& process);
#endif

    std::uintptr_t client{};
    std::uintptr_t engine{};
    std::uintptr_t input_system{};
    std::uintptr_t tier{};
    std::uintptr_t schema{};
    std::uintptr_t physics{};
    std::uintptr_t panorama{};
};

} // namespace game
