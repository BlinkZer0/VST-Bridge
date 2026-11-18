// SPDX-License-Identifier: GPL-3.0-or-later
//
// vgwl: a tiny Wayland compositor for showing Windows plugin editors.
//
// Wine's Wayland driver (winewayland.drv) connects to it like to any desktop compositor. We speak the
// Wayland wire protocol directly (no libwayland), implement only what Wine needs (wl_compositor,
// wl_subcompositor, wl_shm, xdg_wm_base, wp_viewporter, wl_seat with a pointer, wl_output), copy every
// committed shm buffer, and composite the newest top-level window plus its subsurfaces (popups, menus)
// into one ARGB image. The UI side polls snapshot() and feeds touches back with pointer().
//
// Plain POSIX: the same code runs inside the Android app (bionic) and in a Linux test tool.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace vgwl {

struct Snapshot {
    int width = 0, height = 0;
    std::vector<uint32_t> argb;  // 0xAARRGGBB, row-major, width*height
    uint64_t serial = 0;         // changes whenever the picture changes
    std::string title;           // xdg_toplevel title of the shown window
};

class Compositor {
public:
    Compositor();
    ~Compositor();
    Compositor(const Compositor&) = delete;
    Compositor& operator=(const Compositor&) = delete;

    // Listens on a Unix socket at socketPath (an absolute path works as WAYLAND_DISPLAY).
    // outputW/H is the size of the one virtual monitor we advertise.
    bool start(const std::string& socketPath, int outputW, int outputH, std::string& err);
    void stop();
    bool running() const;

    // Copies the current picture into `out` if its serial differs from `haveSerial`. Returns true if
    // copied. Returns false (out untouched) when unchanged; out.width == 0 after a copy means nothing
    // is on screen.
    bool snapshot(uint64_t haveSerial, Snapshot& out);

    // Pointer input in snapshot pixel coordinates. button: -1 = move only, 1 = press, 0 = release.
    // A press is preceded by a move to (x, y); the pointer stays grabbed by the pressed surface.
    void pointer(double x, double y, int button);
    // Scroll: positive = down. In wheel notches.
    void scroll(double notches);
    // Ask the shown window to close (xdg_toplevel.close).
    void closeShown();

    std::string stats();  // one-line JSON for logs

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

}  // namespace vgwl
