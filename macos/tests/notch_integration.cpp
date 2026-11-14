#define SDL_MAIN_HANDLED
#define SDL_AppEvent UpstreamAppEvent
#define SDL_AppIterate UpstreamAppIterate
#include "../../noxsdl/src/main.cpp"
#undef SDL_AppEvent
#undef SDL_AppIterate
#include "../pointer.hpp"
#include <imgui.h>
extern "C" {
void enable_dummy_confinement();
void SDL_SetWindowSafeAreaInsets(SDL_Window *, int, int, int, int);
void SDL_SetMouseFocus(SDL_Window *);
bool SDL_SetKeyboardFocus(SDL_Window *);
void SDL_SendMouseMotion(Uint64, SDL_Window *, SDL_MouseID, bool, float, float);
}
static void require(bool yes, const char *msg)
{
    if (!yes) {
        std::fprintf(stderr, "FAIL %s (%s)\n", msg, SDL_GetError());
        std::exit(1);
    }
}
#undef main
// The delegate runs the real production layout/mouse code. A one-shot transition
// simulates work performed within a frame, letting us test the outer Mac boundary.
static void (*frameStep)(App &) = nullptr;
extern "C" SDL_AppResult SDLCALL SDL_AppIterate(void *state)
{
    auto &app = *static_cast<App *>(state);
    if (frameStep) {
        auto step = std::exchange(frameStep, nullptr);
        step(app);
        return SDL_APP_CONTINUE;
    }
    if (app.editor && !app.playTestInGame && !app.sessionInGame)
        editorMouse(app);
    else {
        layoutView(app, false);
        applyMouse(app);
    }
    return SDL_APP_CONTINUE;
}
extern "C" SDL_AppResult SDLCALL SDL_AppEvent(void *state, SDL_Event *e)
{
    return UpstreamAppEvent(state, e);
}
extern "C" SDL_AppResult SDLCALL MacAppIterate(void *);
extern "C" SDL_AppResult SDLCALL MacAppEvent(void *, SDL_Event *);
static void refresh(App &app) { require(MacAppIterate(&app) == SDL_APP_CONTINUE, "Mac frame wrapper"); }
static SDL_AppResult dispatch(App &app, SDL_Event &e) { return MacAppEvent(&app, &e); }
static void makeEditor(App &app)
{
    MapEditor::Context c;
    c.window = app.window;
    c.renderer = app.renderer;
    c.things = &app.things;
    c.modifiers = &app.modifiers;
    c.gameData = &app.gameData;
    c.screenshotRun = true;
    app.editor = std::make_unique<MapEditor>(std::move(c));
}
int main(int, char **)
{
    SDL_SetMainReady();
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    require(SDL_Init(SDL_INIT_VIDEO), "SDL init");
    enable_dummy_confinement();
    {
        App app;
        app.window = SDL_CreateWindow("production integration", 800, 600, 0);
        require(app.window, "window");
        app.renderer = SDL_CreateRenderer(app.window, "software");
        require(app.renderer, "renderer");
        SDL_SetMouseFocus(app.window);
        SDL_SetKeyboardFocus(app.window);
        app.display = 2;
        app.mouseAccel = false;
        app.mouseSpeed = 100;
        SDL_SetWindowSafeAreaInsets(app.window, 0, 0, 32, 0);
        refresh(app);
        require(app.gameArea.y == 32 && app.gameArea.h == 568 && app.letterboxed, "production usable layout");
        auto bounds = [&]() {
            auto r = SDL_GetWindowMouseRect(app.window);
            require(r, "production confinement");
            return *r;
        };
        auto r = bounds();
        require(r.y == 32 && r.h == 568, "production window-point rect");
        require(std::abs(app.mouseScale - 568.0f / 480) < .001f, "menu input scale matches usable area");
        for (int edge : {0, 1, 2, 3}) {
            SDL_WarpMouseInWindow(app.window, 400, 300);
            SDL_SendMouseMotion(0, app.window, 1, true,
                                edge == 0   ? -2000
                                : edge == 1 ? 2000
                                            : 0,
                                edge == 2   ? -2000
                                : edge == 3 ? 2000
                                            : 0);
            float x, y;
            SDL_GetMouseState(&x, &y);
            require(x >= 0 && x <= 799 && y >= 32 && y <= 599, "all SDL edges confined");
            float dx = edge == 0 ? 1 : edge == 1 ? -1 : 0, dy = edge == 2 ? 1 : edge == 3 ? -1 : 0;
            SDL_SendMouseMotion(0, app.window, 1, true, dx, dy);
            float nx, ny;
            SDL_GetMouseState(&nx, &ny);
            if (std::abs(nx - x - dx * app.mouseScale) > .001f ||
                std::abs(ny - y - dy * app.mouseScale) > .001f) {
                std::fprintf(stderr, "edge %d: (%g,%g) -> (%g,%g), scale %g\n", edge, x, y, nx, ny,
                             app.mouseScale);
                require(false, "all edges reverse immediately at configured input scale");
            }
        }
        SDL_SetWindowSize(app.window, 640, 480);
        SDL_SetWindowSafeAreaInsets(app.window, 0, 0, 48, 0);
        SDL_Event e{};
        e.type = SDL_EVENT_WINDOW_SAFE_AREA_CHANGED;
        e.window.windowID = SDL_GetWindowID(app.window);
        require(dispatch(app, e) == SDL_APP_CONTINUE, "safe-area event delivered");
        require(app.gameArea.y == 32 && bounds().y == 32, "events do not rerun layout or mouse policy");
        refresh(app);
        require(app.gameArea.y == 48 && bounds().y == 48 && bounds().w == 640,
                "next layout refreshes changed area");
        e = {};
        e.type = SDL_EVENT_MOUSE_MOTION;
        e.motion.windowID = SDL_GetWindowID(app.window);
        e.motion.x = 1000;
        e.motion.y = 0;
        require(dispatch(app, e) == SDL_APP_CONTINUE, "queued motion dispatch");
        require(e.motion.x == 639 && e.motion.y == 0, "clamp before game coordinate mapping");
        makeEditor(app);
        app.playTest = true;
        app.playTestInGame = true;
        frameStep = endPlayTest;
        refresh(app);
        require(!SDL_GetWindowMouseRect(app.window),
                "frame wrapper releases full-window test confinement immediately");
        app.playTestInGame = true;
        refresh(app);
        require(bounds().y == 48, "confinement restored after ending test");
        SDL_Event back{};
        back.type = SDL_EVENT_KEY_DOWN;
        back.key.key = SDLK_F2;
        require(dispatch(app, back) == SDL_APP_CONTINUE, "real F2 handler");
        require(!app.playTestInGame && !SDL_GetWindowMouseRect(app.window),
                "event wrapper releases before next queued event");
        editorMouse(app);
        require(!SDL_GetWindowRelativeMouseMode(app.window), "editor disables relative mode");
        app.playTestInGame = true;
        refresh(app);
        frameStep = [](App &a) {
            a.playTestInGame = false;
            a.playTestEmbedded = true;
            a.playTestCaptured = true;
            a.playTestMouseRect = SDL_Rect{100, 100, 200, 200};
            SDL_SetWindowMouseRect(a.window, &a.playTestMouseRect);
        };
        refresh(app);
        require(!macos::ownsPointerArea(app.window) && bounds().x == 100 && bounds().w == 200,
                "panel takes ownership without losing its rectangle");
        e = {};
        e.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
        e.button.windowID = SDL_GetWindowID(app.window);
        e.button.x = 50;
        e.button.y = 100;
        require(macos::filterPointerEvent(app.window, e) && e.button.x == 50,
                "embedded rect ownership preserved");
        frameStep = endPlayTest;
        refresh(app);
        require(!SDL_GetWindowMouseRect(app.window) && !app.playTestCaptured,
                "embedded test teardown preserves upstream cleanup");
        app.editor.reset();
        handMouseTo(app, MouseOwner::Game);
        refresh(app);
        require(SDL_GetWindowRelativeMouseMode(app.window) && bounds().y == 48,
                "next game frame restores confinement");
        frameStep = [](App &a) { handMouseTo(a, MouseOwner::Menus); };
        refresh(app);
        require(!SDL_GetWindowMouseRect(app.window) && !macos::ownsPointerArea(app.window),
                "handoff waits for a fresh layout instead of reinstalling stale bounds");
        refresh(app);
        require(bounds().y == 48, "next calculated frame restores confinement");
        app.display = 0;
        refresh(app);
        require(!SDL_GetWindowMouseRect(app.window) && !SDL_GetWindowRelativeMouseMode(app.window),
                "absolute mode releases rect");
        e = {};
        e.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
        e.button.windowID = SDL_GetWindowID(app.window);
        e.button.button = SDL_BUTTON_RIGHT;
        e.button.x = 300;
        e.button.y = -10;
        require(dispatch(app, e) == SDL_APP_CONTINUE && e.button.y == -10 && !app.dragging,
                "outside press dropped before mapping");
        SDL_Event inside = e;
        inside.button.y = 100;
        require(dispatch(app, inside) == SDL_APP_CONTINUE && app.dragging, "inside press starts drag");
        e.type = SDL_EVENT_MOUSE_BUTTON_UP;
        require(dispatch(app, e) == SDL_APP_CONTINUE && e.button.y == 0 && !app.dragging,
                "outside release reaches drag handler and is clamped in game coordinates");
        SDL_SetWindowSafeAreaInsets(app.window, 0, 0, 0, 0);
        app.display = 2;
        refresh(app);
        require(bounds().y == 0 && bounds().h == 480, "return to display without notch");
        SDL_SetWindowSafeAreaInsets(app.window, 0, 0, 48, 0);
        // Exercise the real render-target composition with asymmetric insets on every edge.
        SDL_SetWindowSize(app.window, 640, 480);
        SDL_SetWindowSafeAreaInsets(app.window, 8, 12, 48, 16);
        refresh(app);
        require(app.gameArea.x == 8 && app.gameArea.y == 48 && app.gameArea.w == 620 && app.gameArea.h == 416,
                "four-sided usable layout");
        require(std::abs(app.mouseScale - 416.0f / 480) < .001f, "mouse scale uses the fitted layout");
        beginFrame(app);
        SDL_SetRenderDrawColor(app.renderer, 220, 20, 30, 255);
        SDL_RenderClear(app.renderer);
        endFrame(app);
        SDL_Surface *frame = SDL_RenderReadPixels(app.renderer, nullptr);
        require(frame, "render readback");
        auto pixel = [&](int x, int y, bool content) {
            Uint8 r, g, b, a;
            require(SDL_ReadSurfacePixel(frame, x, y, &r, &g, &b, &a), "pixel read");
            require(content ? (r == 220 && g == 20 && b == 30) : (r == 0 && g == 0 && b == 0),
                    "safe-area composition pixels");
        };
        pixel(7, 100, false);
        pixel(8, 48, true);
        pixel(627, 463, true);
        pixel(628, 100, false);
        pixel(100, 47, false);
        pixel(100, 464, false);
        SDL_DestroySurface(frame);
        SDL_SetWindowSafeAreaInsets(app.window, 0, 0, 48, 0);
        refresh(app);
        // The movie path temporarily changes the viewport; the next frame must see the full output.
        app.movie = std::make_unique<MoviePlayer>(app.renderer, nullptr, std::filesystem::path{},
                                                  std::filesystem::path{});
        require(showMovie(app, false) == SDL_APP_CONTINUE, "movie frame");
        require(!SDL_RenderViewportSet(app.renderer) && !SDL_GetRenderTarget(app.renderer),
                "movie restores window viewport");
        app.movie.reset();
        layoutView(app, false);
        require(app.gameArea.y == 48 && app.gameArea.h == 432, "layout after safe-area movie");
        frameStep = editorMouse;
        refresh(app);
        {
            editor::ImGuiHost host(app.window, app.renderer, "", 1, editor::Theme::Dark, false, true);
            host.newFrame();
            auto *viewport = ImGui::GetMainViewport();
            require(viewport->WorkPos.y == 48 && viewport->WorkSize.y == 432,
                    "ImGui work area follows point bounds");
            host.render();
            // Repeated frames and changing all four insets must not accumulate or retain old bounds.
            for (const SDL_Rect insets : {SDL_Rect{8, 48, 12, 16}, SDL_Rect{8, 48, 12, 16},
                                          SDL_Rect{0, 0, 0, 0}, SDL_Rect{0, 32, 0, 0}}) {
                SDL_SetWindowSafeAreaInsets(app.window, insets.x, insets.w, insets.y, insets.h);
                host.newFrame();
                viewport = ImGui::GetMainViewport();
                require(viewport->WorkPos.x == viewport->Pos.x + insets.x &&
                            viewport->WorkPos.y == viewport->Pos.y + insets.y &&
                            viewport->WorkSize.x == 640 - insets.x - insets.w &&
                            viewport->WorkSize.y == 480 - insets.y - insets.h,
                        "editor work bounds refresh without accumulation");
                host.render();
            }
        }
        SDL_DestroyTexture(app.gammaTarget);
        app.gammaTarget = nullptr;
        SDL_DestroyRenderer(app.renderer);
        app.renderer = nullptr;
        SDL_DestroyWindow(app.window);
        app.window = nullptr;
    }
    SDL_Quit();
    std::puts("PASS production layout, rendered safe-area pixels, mouse policy, event dispatch, frame "
              "refresh, handoffs and ImGui work area");
}
