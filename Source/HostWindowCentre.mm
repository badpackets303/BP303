#include "HostWindowCentre.h"

#import <AppKit/AppKit.h>

namespace bp303
{
    void centreHostWindow (void* nativeViewHandle)
    {
        if (nativeViewHandle == nullptr)
            return;

        NSView* view = (NSView*) nativeViewHandle;
        NSWindow* window = [view window];
        if (window == nil)
            return;

        // The screen the window is currently on (nil when it is off-screen).
        NSScreen* screen = [window screen];
        if (screen == nil)
            screen = [NSScreen mainScreen];
        if (screen == nil)
            return;

        // visibleFrame excludes the menu bar and Dock, so the whole window stays
        // reachable. NSWindow coordinates are bottom-left origin.
        const NSRect vis = [screen visibleFrame];
        const NSRect f   = [window frame];
        const CGFloat x = vis.origin.x + (vis.size.width  - f.size.width)  * (CGFloat) 0.5;
        const CGFloat y = vis.origin.y + (vis.size.height - f.size.height) * (CGFloat) 0.5;
        [window setFrameOrigin: NSMakePoint (x, y)];
    }
}
