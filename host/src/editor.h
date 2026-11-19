// SPDX-License-Identifier: LicenseRef-VstGhost-Proprietary
#pragma once

#include <string>

namespace vg {

class Plugin;

// The plugin's own editor (VST3 IPlugView) in a top-level Win32 window. Main thread only; the caller
// pumps Win32 messages and calls poll() regularly.
// With VSTGHOST_WL_ONE_TOPLEVEL=1 the window is a borderless popup whose class name our Wine Wayland
// patch recognises, so the plugin's menus and dialogs become part of the same picture.
class EditorWindow {
public:
    ~EditorWindow();
    bool open(Plugin& p, std::string& err);
    void close();
    bool isOpen() const { return hwnd_ != nullptr; }
    // Hides/shows the window. Hidden, it gets no paint messages, so a software-rasterized editor
    // stops burning CPU while the phone's editor screen is not on (the plugin stays loaded).
    void setVisible(bool visible);
    // Follows resize requests from the plugin; returns false if the window was closed (WM_CLOSE).
    bool poll();
    int width() const { return w_; }
    int height() const { return h_; }
    bool resizable() const { return resizable_; }
    int resizes() const { return resizes_; }
    void* hwnd() const { return hwnd_; }
    // 24-bit BMP of the client area (empty path: just measure). Fraction of non-black pixels, or -1.
    double screenshot(const std::wstring& path);

private:
    Plugin* p_ = nullptr;
    void* hwnd_ = nullptr;
    int w_ = 0, h_ = 0, resizes_ = 0;
    bool resizable_ = false;
};

// `vstghost-host editor <plugin> [--seconds N] [--shot <file.bmp>]`
int cmdEditor(const std::wstring& pluginPath, int seconds, const std::wstring& shotPath, const char** stage);

}  // namespace vg
