#include <ui/overlay/linux_imgui_surface.hpp>

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <GLFW/glfw3.h>

namespace ui::linux_surface
{
namespace
{
int g_users{};
}

surface create(const char* title, int width, int height)
{
    if (g_users == 0)
    {
        glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_WAYLAND);
        if (!glfwInit())
            return {};
    }
    ++g_users;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
    glfwWindowHint(GLFW_FLOATING, GLFW_TRUE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    auto* window = glfwCreateWindow(width, height, title, nullptr, nullptr);
    if (!window)
    {
        if (--g_users == 0)
            glfwTerminate();
        return {};
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    if (!ImGui_ImplGlfw_InitForOpenGL(window, true) || !ImGui_ImplOpenGL3_Init("#version 330"))
    {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        glfwDestroyWindow(window);
        if (--g_users == 0)
            glfwTerminate();
        return {};
    }
    return {window, width, height};
}

void destroy(surface& value) noexcept
{
    if (!value.window)
        return;
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(value.window);
    value = {};
    if (--g_users == 0)
        glfwTerminate();
}

bool begin(surface& value)
{
    if (!value.window || glfwWindowShouldClose(value.window))
        return false;
    glfwPollEvents();
    glfwGetFramebufferSize(value.window, &value.width, &value.height);
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    return true;
}

void end(surface& value) noexcept
{
    if (!value.window)
        return;
    ImGui::Render();
    glViewport(0, 0, value.width, value.height);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(value.window);
}
} // namespace ui::linux_surface
