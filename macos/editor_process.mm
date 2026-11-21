#import <Foundation/Foundation.h>
#include "backend_hooks.hpp"

#include <SDL3/SDL.h>
#include <vector>

// The editor's Private Test launch (script and mod sessions).
extern "C" SDL_Process* SDLCALL NoXMac_CreateEditorProcess(const char* const* args, bool pipeStdio)
{
    @autoreleasepool {
        NSURL* exe = NSBundle.mainBundle.executableURL;
        if (!exe.fileSystemRepresentation) {
            SDL_SetError("Cannot locate the running private-test executable");
            return nullptr;
        }
        try {
            const auto user = noxsdl::backend::settingsDirectory({});
            const auto log = user / (std::string("private-test-") + NSUUID.UUID.UUIDString.UTF8String + ".log");
            std::vector<const char*> child{exe.fileSystemRepresentation};
            for (size_t i = 1; args[i]; ++i) child.push_back(args[i]);
            // Repeated options take the last value. Keep the map, mod and cache arguments verbatim.
            child.insert(child.end(), {"--user-dir", user.c_str(), "--log", log.c_str(), nullptr});
            return SDL_CreateProcess(child.data(), pipeStdio);
        } catch (const std::exception& e) {
            SDL_SetError("Cannot prepare the private test: %s", e.what());
            return nullptr;
        }
    }
}
