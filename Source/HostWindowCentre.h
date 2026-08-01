#pragma once

// Best-effort centring of the *host-owned* window that contains a plugin editor.
// In a plugin (e.g. an AU in Logic) the host owns the window, so JUCE cannot move
// it through its own component hierarchy — this reaches the native NSWindow behind
// the editor's view and centres it on the screen it is shown on.
//
// The real (macOS) implementation lives in HostWindowCentre.mm and is compiled
// only into the plugin build (which defines BP303_HAS_NATIVE_WINDOW). Every other
// target — the standalone helper tools that also include the editor — gets the
// inline no-op below and needs no Objective-C++ translation unit.

namespace bp303
{
#if defined (BP303_HAS_NATIVE_WINDOW)
    // nativeViewHandle is the editor peer's native handle (an NSView* on macOS).
    void centreHostWindow (void* nativeViewHandle);
#else
    inline void centreHostWindow (void*) {}
#endif
}
