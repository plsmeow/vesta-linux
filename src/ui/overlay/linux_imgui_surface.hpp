#pragma once

struct GLFWwindow;

namespace ui::linux_surface
{
struct surface
{
    GLFWwindow* window{};
    int width{};
    int height{};
};

[[nodiscard]] surface create(const char* title, int width, int height);
void destroy(surface& value) noexcept;
[[nodiscard]] bool begin(surface& value);
void end(surface& value) noexcept;
} // namespace ui::linux_surface
