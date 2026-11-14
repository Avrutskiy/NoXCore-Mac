#include "video/SDL_sysvideo.h"
static bool set_rect(SDL_VideoDevice *device, SDL_Window *window)
{
    (void)device;
    (void)window;
    return true;
}
void enable_dummy_confinement(void) { SDL_GetVideoDevice()->SetWindowMouseRect = set_rect; }
