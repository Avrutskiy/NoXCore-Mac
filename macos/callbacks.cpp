// The Mac entry point owns input policy at SDL's existing callback boundary.
#define SDL_MAIN_HANDLED
#include <SDL3/SDL_main.h>
#include "app.hpp"
#include "pointer.hpp"

extern "C" SDL_AppResult SDLCALL SDL_AppEvent(void*, SDL_Event*);
extern "C" SDL_AppResult SDLCALL SDL_AppIterate(void*);

namespace {
bool editorOwnsPointer(const noxsdl::App& app)
{
    return app.editor && !app.playTestInGame && !app.sessionInGame;
}

void releaseOnHandoff(const noxsdl::App& app)
{
    using namespace noxsdl::macos;
    const bool editor = editorOwnsPointer(app);
    if ((!editor && app.mouseMode != noxsdl::MouseMode::Unknown) || !ownsPointerArea(app.window)) return;
    if (editor && app.playTestCaptured && (app.playTestEmbedded || app.sessionEmbedded)) {
        // The upstream panel layout has taken over the rectangle. Do not clear it.
        relinquishPointerArea(app.window);
    } else {
        updatePointerArea(app.window, nullptr, app.pixelDensity);
    }
}
}

extern "C" SDL_AppResult SDLCALL MacAppEvent(void* state, SDL_Event* event)
{
    // SDL may deliver lifecycle notifications off-thread. They need no pointer policy.
    if (!state || !SDL_IsMainThread()) return SDL_AppEvent(state, event);
    auto& app = *static_cast<noxsdl::App*>(state);
    if (!noxsdl::macos::filterPointerEvent(app.window, *event)) return SDL_APP_CONTINUE;
    if (event->type == SDL_EVENT_MOUSE_WHEEL && SDL_GetWindowFromEvent(event) == app.window &&
        (!editorOwnsPointer(app) || (noxsdl::gameInPanel(app) && app.playTestCaptured))) {
        // The game consumes one step per event. SDL accumulates fractional trackpad motion.
        if (event->wheel.integer_y == 0) return SDL_APP_CONTINUE;
        event->wheel.y = static_cast<float>(event->wheel.integer_y);
    }
    const SDL_AppResult result = SDL_AppEvent(state, event);
    releaseOnHandoff(app); // An event can hand the game back before the next frame.
    return result;
}

extern "C" SDL_AppResult SDLCALL MacAppIterate(void* state)
{
    const SDL_AppResult result = SDL_AppIterate(state);
    if (result != SDL_APP_CONTINUE || !state) return result;
    auto& app = *static_cast<noxsdl::App*>(state);
    releaseOnHandoff(app);
    if (!editorOwnsPointer(app) && app.mouseMode != noxsdl::MouseMode::Unknown) {
        // Upstream has now computed the layout and applied this frame's mouse mode.
        const bool relative = app.mouseMode == noxsdl::MouseMode::Relative;
        noxsdl::macos::updatePointerArea(app.window, relative ? &app.gameArea : nullptr, app.pixelDensity);
    }
    return result;
}
