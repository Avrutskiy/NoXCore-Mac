#include "pointer.hpp"
#include <algorithm>
#include <cmath>

namespace {
constexpr const char* pointerAreaOwned = "nox.mac.pointer-area-owned";
void clampPointer(const SDL_Rect& area, float& x, float& y)
{
    x = std::clamp(x, static_cast<float>(area.x), static_cast<float>(area.x + area.w - 1));
    y = std::clamp(y, static_cast<float>(area.y), static_cast<float>(area.y + area.h - 1));
}
}
namespace noxsdl::macos {
std::optional<SDL_Rect> usableWindowArea(SDL_Window* window)
{
    SDL_Rect area{};
    if (window && SDL_GetWindowSafeArea(window, &area) && area.w > 0 && area.h > 0) return area;
    return std::nullopt;
}

void updatePointerArea(SDL_Window* window, const SDL_FRect* renderArea, float density)
{
    if (!window) return;
    SDL_Rect bounds{};
    const SDL_Rect* area = nullptr;
    if (renderArea && SDL_GetWindowRelativeMouseMode(window)) {
        // SDL confines in window points. Round inward to avoid invisible travel at an edge.
        density = std::max(density, 0.01f);
        bounds.x = static_cast<int>(std::ceil(renderArea->x / density));
        bounds.y = static_cast<int>(std::ceil(renderArea->y / density));
        bounds.w = static_cast<int>(std::floor((renderArea->x + renderArea->w) / density)) - bounds.x;
        bounds.h = static_cast<int>(std::floor((renderArea->y + renderArea->h) / density)) - bounds.y;
        if (bounds.w > 0 && bounds.h > 0) area = &bounds;
    }
    const SDL_Rect* current = SDL_GetWindowMouseRect(window);
    if ((area == nullptr) != (current == nullptr) || (area && current && !SDL_RectsEqual(area, current))) {
        if (!SDL_SetWindowMouseRect(window, area)) {
            SDL_Log("mouse confinement: %s", SDL_GetError());
            return;
        }
    }
    // The embedded play view owns its own rect; event clamping belongs only to this backend's rect.
    if (ownsPointerArea(window) != (area != nullptr)) {
        SDL_SetBooleanProperty(SDL_GetWindowProperties(window), pointerAreaOwned, area != nullptr);
    }
    if (area && SDL_GetMouseFocus() == window && SDL_GetKeyboardFocus() == window) {
        // SDL can retain an old position past a newly shrunken right/bottom edge.
        float x, y;
        SDL_GetMouseState(&x, &y);
        float cx = x, cy = y;
        clampPointer(*area, cx, cy);
        if (cx != x || cy != y) SDL_WarpMouseInWindow(window, cx, cy);
    }
}

bool filterPointerEvent(SDL_Window* window, SDL_Event& event)
{
    if (event.type != SDL_EVENT_MOUSE_MOTION && event.type != SDL_EVENT_MOUSE_BUTTON_DOWN &&
        event.type != SDL_EVENT_MOUSE_BUTTON_UP && event.type != SDL_EVENT_MOUSE_WHEEL) return true;
    if (!window || SDL_GetWindowFromEvent(&event) != window) return true;
    if (SDL_GetWindowRelativeMouseMode(window) &&
        SDL_GetBooleanProperty(SDL_GetWindowProperties(window), pointerAreaOwned, false)) {
        if (const SDL_Rect* area = SDL_GetWindowMouseRect(window)) {
            // Events queued before a layout refresh can still contain the previous bounds.
            switch (event.type) {
            case SDL_EVENT_MOUSE_MOTION: clampPointer(*area, event.motion.x, event.motion.y); break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP: clampPointer(*area, event.button.x, event.button.y); break;
            case SDL_EVENT_MOUSE_WHEEL: clampPointer(*area, event.wheel.mouse_x, event.wheel.mouse_y); break;
            default: break;
            }
        }
    }
    // Always deliver releases, including a drag released outside the usable area.
    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_WHEEL) {
        if (const auto area = usableWindowArea(window)) {
            const float x = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ? event.button.x : event.wheel.mouse_x;
            const float y = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ? event.button.y : event.wheel.mouse_y;
            return x >= area->x && y >= area->y && x < area->x + area->w && y < area->y + area->h;
        }
    }
    return true;
}
bool ownsPointerArea(SDL_Window* window)
{
    return window && SDL_GetBooleanProperty(SDL_GetWindowProperties(window), pointerAreaOwned, false);
}
void relinquishPointerArea(SDL_Window* window)
{
    if (window) SDL_SetBooleanProperty(SDL_GetWindowProperties(window), pointerAreaOwned, false);
}
} // namespace noxsdl::macos
