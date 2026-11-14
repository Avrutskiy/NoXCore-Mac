// Manual native-display check: briefly visits windowed, desktop and exclusive fullscreen.
#define SDL_MAIN_HANDLED
#include "backend_hooks.hpp"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <cstdio>

namespace
{
bool check(SDL_Window *window, SDL_Renderer *renderer, const char *mode)
{
    if (!SDL_SyncWindow(window))
        return false;
    for (int i = 0; i < 30; ++i) {
        SDL_PumpEvents();
        SDL_Delay(20);
    }
    SDL_Rect points{};
    int width = 0, height = 0;
    auto pixels = noxsdl::backend::usableRenderArea(renderer);
    if (!SDL_GetWindowSafeArea(window, &points) || !pixels ||
        !SDL_GetRenderOutputSize(renderer, &width, &height))
        return false;
    std::printf("%s: output=%dx%d density=%g window-safe=%d,%d %dx%d render-safe=%d,%d %dx%d\n", mode, width,
                height, SDL_GetWindowPixelDensity(window), points.x, points.y, points.w, points.h, pixels->x,
                pixels->y, pixels->w, pixels->h);
    if (pixels->x < 0 || pixels->y < 0 || pixels->x + pixels->w > width || pixels->y + pixels->h > height)
        return false;
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    SDL_FRect content{float(pixels->x), float(pixels->y), float(pixels->w), float(pixels->h)};
    SDL_SetRenderDrawColor(renderer, 40, 140, 220, 255);
    if (!SDL_RenderFillRect(renderer, &content))
        return false;
    SDL_Surface *frame = SDL_RenderReadPixels(renderer, nullptr);
    if (!frame)
        return false;
    bool ok = true;
    // Check every pixel at the vertical centerline, including the actual reported top inset.
    for (int y = 0; y < height; ++y) {
        Uint8 r, g, b, a;
        const int x = pixels->x + pixels->w / 2;
        const bool inside = y >= pixels->y && y < pixels->y + pixels->h;
        ok = SDL_ReadSurfacePixel(frame, x, y, &r, &g, &b, &a) &&
             (inside ? r == 40 && g == 140 && b == 220 : r == 0 && g == 0 && b == 0) && ok;
    }
    SDL_DestroySurface(frame);
    SDL_RenderPresent(renderer);
    return ok;
}
} // namespace
int main()
{
    SDL_SetMainReady();
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "FAIL SDL initialization: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Window *window = SDL_CreateWindow("NoX safe-area display check", 800, 600,
                                          SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_RESIZABLE);
    SDL_Renderer *renderer = window ? SDL_CreateRenderer(window, "metal") : nullptr;
    bool ok = renderer && check(window, renderer, "windowed");
    if (ok)
        ok = SDL_SetWindowFullscreen(window, true) && check(window, renderer, "desktop fullscreen");
    if (ok) {
        const SDL_DisplayMode *mode = SDL_GetDesktopDisplayMode(SDL_GetDisplayForWindow(window));
        ok = mode && SDL_SetWindowFullscreenMode(window, mode) &&
             check(window, renderer, "exclusive fullscreen");
    }
    if (!ok)
        std::fprintf(stderr, "FAIL native safe area: %s\n", SDL_GetError());
    if (window) {
        SDL_SetWindowFullscreen(window, false);
        SDL_SyncWindow(window);
    }
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    if (ok)
        std::puts("PASS native safe-area query and Metal readback");
    return ok ? 0 : 1;
}
