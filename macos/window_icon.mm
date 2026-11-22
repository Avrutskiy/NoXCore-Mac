#import <Foundation/Foundation.h>

#include "window_icon.hpp"
#include <SDL3/SDL.h>
#include <cstring>

namespace noxsdl {

bool setMacWindowIcon(SDL_Window* window)
{
    const char* driver = SDL_GetCurrentVideoDriver();
    if (driver && std::strcmp(driver, "cocoa") == 0) {
        @autoreleasepool {
            NSBundle* bundle = NSBundle.mainBundle;
            id name = [bundle objectForInfoDictionaryKey:@"CFBundleIconFile"];
            if ([name isKindOfClass:NSString.class] && [name length] > 0) {
                NSString* extension = [name hasSuffix:@".icns"] ? nil : @"icns";
                if ([bundle URLForResource:name withExtension:extension]) {
                    SDL_Log("window icon: using the app bundle's icon");
                    return true;
                }
            }
        }
    }
    return setWindowIcon(window);
}

} // namespace noxsdl
