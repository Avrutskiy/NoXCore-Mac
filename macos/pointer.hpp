#pragma once
#include <SDL3/SDL.h>
#include <optional>

namespace noxsdl::macos {
// Mac-only input policy; none of these functions are part of the upstream contract.
std::optional<SDL_Rect> usableWindowArea(SDL_Window* window);
void updatePointerArea(SDL_Window* window, const SDL_FRect* area, float density);
bool filterPointerEvent(SDL_Window* window, SDL_Event& event);
bool ownsPointerArea(SDL_Window* window);
void relinquishPointerArea(SDL_Window* window);
}
