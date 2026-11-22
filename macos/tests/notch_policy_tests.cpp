#define SDL_MAIN_HANDLED
#include "backend_hooks.hpp"
#include "pointer.hpp"
#include <cstdio>
#include <cstdlib>
struct SDL_Window {
    SDL_Rect rect{}, safe{0, 32, 800, 568};
    bool confined = false, relative = true, owned = false;
} primary, secondary;
struct SDL_Renderer {
} renderer;
static bool safeOK = true, setOK = true, focused = true;
static int rectChanges = 0, ownershipChanges = 0, warps = 0;
static float mouseX = 400, mouseY = 100;
extern "C" {
bool SDL_GetWindowSafeArea(SDL_Window *w, SDL_Rect *r)
{
    *r = w->safe;
    return safeOK;
}
bool SDL_GetRenderSafeArea(SDL_Renderer *, SDL_Rect *r)
{
    *r = {0, 64, 1600, 1136};
    return safeOK;
}
bool SDL_GetWindowRelativeMouseMode(SDL_Window *w) { return w->relative; }
const SDL_Rect *SDL_GetWindowMouseRect(SDL_Window *w) { return w->confined ? &w->rect : nullptr; }
bool SDL_SetWindowMouseRect(SDL_Window *w, const SDL_Rect *r)
{
    ++rectChanges;
    if (!setOK)
        return false;
    w->confined = r != nullptr;
    if (r)
        w->rect = *r;
    return true;
}
SDL_PropertiesID SDL_GetWindowProperties(SDL_Window *w) { return w == &primary ? 1 : 2; }
bool SDL_SetBooleanProperty(SDL_PropertiesID p, const char *, bool v)
{
    ++ownershipChanges;
    (p == 1 ? primary : secondary).owned = v;
    return true;
}
bool SDL_GetBooleanProperty(SDL_PropertiesID p, const char *, bool)
{
    return (p == 1 ? primary : secondary).owned;
}
SDL_Window *SDL_GetMouseFocus() { return focused ? &primary : &secondary; }
SDL_Window *SDL_GetKeyboardFocus() { return focused ? &primary : &secondary; }
SDL_MouseButtonFlags SDL_GetMouseState(float *x, float *y)
{
    *x = mouseX;
    *y = mouseY;
    return 0;
}
void SDL_WarpMouseInWindow(SDL_Window *, float x, float y)
{
    ++warps;
    mouseX = x;
    mouseY = y;
}
SDL_Window *SDL_GetWindowFromEvent(const SDL_Event *e)
{
    return e->motion.windowID == 1 ? &primary : &secondary;
}
const char *SDL_GetError() { return "mock failure"; }
void SDL_Log(const char *, ...) {}
}
using namespace noxsdl;
static void check(bool ok, const char *name)
{
    if (!ok) {
        std::fprintf(stderr, "FAIL %s\n", name);
        std::exit(1);
    }
}
static SDL_Event event(Uint32 type, float x, float y, Uint32 window = 1)
{
    SDL_Event e{};
    e.type = type;
    e.motion.windowID = window;
    if (type == SDL_EVENT_MOUSE_MOTION) {
        e.motion.x = x;
        e.motion.y = y;
    } else if (type == SDL_EVENT_MOUSE_WHEEL) {
        e.wheel.mouse_x = x;
        e.wheel.mouse_y = y;
    } else {
        e.button.x = x;
        e.button.y = y;
    }
    return e;
}
int main()
{
    check(!macos::usableWindowArea(nullptr) && !backend::usableRenderArea(nullptr), "null handles");
    check(macos::usableWindowArea(&primary)->y == 32 && backend::usableRenderArea(&renderer)->y == 64,
          "point/pixel units");
    safeOK = false;
    check(!macos::usableWindowArea(&primary) && !backend::usableRenderArea(&renderer), "failed queries");
    safeOK = true;
    SDL_FRect game{0, 64, 1600, 1136};
    macos::updatePointerArea(&primary, &game, 2);
    check(primary.rect.x == 0 && primary.rect.y == 32 && primary.rect.w == 800 && primary.rect.h == 568,
          "Retina bounds");
    const int changes = rectChanges, writes = ownershipChanges;
    macos::updatePointerArea(&primary, &game, 2);
    check(ownershipChanges == writes, "unchanged ownership does not rewrite the property");
    check(rectChanges == changes, "unchanged bounds do not reset SDL rect");
    for (auto type : {SDL_EVENT_MOUSE_MOTION, SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_EVENT_MOUSE_BUTTON_UP,
                      SDL_EVENT_MOUSE_WHEEL}) {
        auto e = event(type, -200, 2000);
        check(macos::filterPointerEvent(&primary, e), "queued event accepted after clamping");
        float x = type == SDL_EVENT_MOUSE_MOTION  ? e.motion.x
                  : type == SDL_EVENT_MOUSE_WHEEL ? e.wheel.mouse_x
                                                  : e.button.x;
        float y = type == SDL_EVENT_MOUSE_MOTION  ? e.motion.y
                  : type == SDL_EVENT_MOUSE_WHEEL ? e.wheel.mouse_y
                                                  : e.button.y;
        check(x == 0 && y == 599, "queued event clamped to render bounds");
    }
    SDL_FRect smaller{20, 80, 600, 400};
    mouseX = 799;
    mouseY = 599;
    macos::updatePointerArea(&primary, &smaller, 2);
    check(mouseX == 309 && mouseY == 239 && warps == 1, "shrinking bounds rebase pointer");
    focused = false;
    mouseX = 799;
    mouseY = 599;
    macos::updatePointerArea(&primary, &smaller, 2);
    check(warps == 1 && mouseX == 799, "unfocused window never warps");
    focused = true;
    macos::updatePointerArea(&primary, nullptr, 2);
    check(!primary.confined && !primary.owned, "editor clears confinement");
    primary.relative = false;
    for (auto type : {SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_EVENT_MOUSE_WHEEL}) {
        auto e = event(type, 400, 0);
        check(!macos::filterPointerEvent(&primary, e), "absolute outside press/wheel dropped");
        e = event(type, 400, 32);
        check(macos::filterPointerEvent(&primary, e), "top edge accepted");
        e = event(type, 800, 100);
        check(!macos::filterPointerEvent(&primary, e), "right edge excluded");
        e = event(type, 400, 0, 2);
        check(macos::filterPointerEvent(&primary, e), "other window untouched");
    }
    auto release = event(SDL_EVENT_MOUSE_BUTTON_UP, 400, 0);
    check(macos::filterPointerEvent(&primary, release) && release.button.y == 0, "outside release preserved");
    primary.relative = true;
    SDL_Rect panel{100, 100, 200, 200};
    SDL_SetWindowMouseRect(&primary, &panel);
    auto outsidePanel = event(SDL_EVENT_MOUSE_BUTTON_DOWN, 50, 50);
    check(macos::filterPointerEvent(&primary, outsidePanel) && outsidePanel.button.x == 50,
          "panel rect remains editor-owned");
    macos::updatePointerArea(&primary, &game, 2);
    check(primary.owned, "return game restores ownership");
    SDL_FRect fraction{11, 65, 901, 501};
    macos::updatePointerArea(&primary, &fraction, 2);
    check(primary.rect.x == 6 && primary.rect.y == 33 && primary.rect.w == 450 && primary.rect.h == 250,
          "fractional edges round inward");
    primary.relative = false;
    macos::updatePointerArea(&primary, &game, 2);
    check(!primary.confined && !primary.owned, "failed/disabled relative mode releases rect");
    primary.relative = true;
    setOK = false;
    macos::updatePointerArea(&primary, &game, 2);
    check(!primary.owned, "failed confinement not claimed");
    std::puts("PASS backend bounds, ownership, queued events, focus, absolute filtering, releases and "
              "fallback paths");
}
