// Adapt ImGui's public SDL initializer; each context owns and destroys its own hook.
#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_sdl3.h>
#include <algorithm>

// Only the bundled SDL backend translation unit is compiled under this name.
extern bool NoXMac_InitImGuiSDLRenderer(SDL_Window*, SDL_Renderer*);

namespace {
void applySafeArea(ImGuiContext*, ImGuiContextHook* hook)
{
    SDL_Rect area;
    if (!SDL_GetWindowSafeArea(static_cast<SDL_Window*>(hook->UserData), &area) || area.w <= 0 || area.h <= 0) return;
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float right = std::min(viewport->WorkPos.x + viewport->WorkSize.x, viewport->Pos.x + area.x + area.w);
    const float bottom = std::min(viewport->WorkPos.y + viewport->WorkSize.y, viewport->Pos.y + area.y + area.h);
    viewport->WorkPos.x = std::max(viewport->WorkPos.x, viewport->Pos.x + area.x);
    viewport->WorkPos.y = std::max(viewport->WorkPos.y, viewport->Pos.y + area.y);
    viewport->WorkSize = ImVec2(std::max(right - viewport->WorkPos.x, 0.0f), std::max(bottom - viewport->WorkPos.y, 0.0f));
}
}

bool ImGui_ImplSDL3_InitForSDLRenderer(SDL_Window* window, SDL_Renderer* renderer)
{
    if (!NoXMac_InitImGuiSDLRenderer(window, renderer)) return false;
    ImGuiContextHook hook{};
    hook.Type = ImGuiContextHookType_NewFramePost;
    hook.Callback = applySafeArea;
    hook.UserData = window;
    ImGui::AddContextHook(ImGui::GetCurrentContext(), &hook);
    return true;
}
