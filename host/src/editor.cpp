// SPDX-License-Identifier: LicenseRef-VstGhost-Proprietary
//
// Plugin editor window (VST3 IPlugView in a Win32 top-level window), used by the live `serve` mode
// (editor_open / editor_close requests) and by the `editor` test command, which opens the editor for
// a few seconds and saves screenshots so rendering can be checked without looking at a screen.
#include "editor.h"

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <string>

#include "util.h"
#include "vst3host.h"

namespace vg {
namespace {

struct WinState {
    Plugin* plugin = nullptr;
    bool closed = false;
};

LRESULT CALLBACK wndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    auto* st = reinterpret_cast<WinState*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    switch (msg) {
    case WM_SIZE:
        if (st && st->plugin) st->plugin->editorHostResized(LOWORD(lp), HIWORD(lp));
        return 0;
    case WM_CLOSE:
        if (st) st->closed = true;
        return 0;  // the owner closes the editor properly (removed() before the window goes)
    case WM_ERASEBKGND:
        return 1;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

bool oneToplevelMode() {
    const char* e = getenv("VSTGHOST_WL_ONE_TOPLEVEL");
    return e && e[0] == '1';
}

void setClientSize(HWND h, int w, int ht) {
    RECT r{0, 0, w, ht};
    AdjustWindowRectEx(&r, (DWORD)GetWindowLongPtrW(h, GWL_STYLE), FALSE, (DWORD)GetWindowLongPtrW(h, GWL_EXSTYLE));
    SetWindowPos(h, nullptr, 0, 0, r.right - r.left, r.bottom - r.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

}  // namespace

EditorWindow::~EditorWindow() { close(); }

bool EditorWindow::open(Plugin& p, std::string& err) {
    if (hwnd_) return true;
    p_ = &p;
    // The pid in the class name lets our Wine Wayland patch (VSTGHOST_WL_ONE_TOPLEVEL) find this window.
    static std::wstring cls;
    if (cls.empty()) {
        cls = L"VstGhostEditor-" + std::to_wstring(GetCurrentProcessId());
        WNDCLASSW wc{};
        wc.lpfnWndProc = wndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = cls.c_str();
        wc.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
        wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
        RegisterClassW(&wc);
    }
    const DWORD style = oneToplevelMode() ? (WS_POPUP | WS_CLIPCHILDREN) : (WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN);
    const std::wstring title = L"VST gHost - " + wide(p.name);
    HWND h = CreateWindowExW(0, cls.c_str(), title.c_str(), style, oneToplevelMode() ? 0 : CW_USEDEFAULT,
                             oneToplevelMode() ? 0 : CW_USEDEFAULT, 800, 600, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!h) {
        err = "CreateWindow failed (no display driver?) win32=" + std::to_string(GetLastError());
        return false;
    }
    auto* st = new WinState{&p, false};
    SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)st);
    int w = 0, ht = 0;
    bool resizable = false;
    if (!p.openEditor(h, w, ht, resizable, err)) {
        SetWindowLongPtrW(h, GWLP_USERDATA, 0);
        delete st;
        DestroyWindow(h);
        return false;
    }
    hwnd_ = h;
    w_ = w;
    h_ = ht;
    resizable_ = resizable;
    resizes_ = 0;
    setClientSize(h, w, ht);
    ShowWindow(h, SW_SHOWNORMAL);
    UpdateWindow(h);
    return true;
}

void EditorWindow::close() {
    if (!hwnd_) return;
    HWND h = static_cast<HWND>(hwnd_);
    if (p_) p_->closeEditor();
    auto* st = reinterpret_cast<WinState*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    SetWindowLongPtrW(h, GWLP_USERDATA, 0);
    DestroyWindow(h);
    delete st;
    hwnd_ = nullptr;
}

bool EditorWindow::poll() {
    if (!hwnd_) return false;
    HWND h = static_cast<HWND>(hwnd_);
    auto* st = reinterpret_cast<WinState*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (st && st->closed) {
        close();
        return false;
    }
    int nw, nh;
    if (p_ && p_->takeEditorResizeRequest(nw, nh)) {
        setClientSize(h, nw, nh);
        w_ = nw;
        h_ = nh;
        ++resizes_;
    }
    return true;
}

double EditorWindow::screenshot(const std::wstring& path) {
    if (!hwnd_) return -1;
    HWND h = static_cast<HWND>(hwnd_);
    RECT rc;
    GetClientRect(h, &rc);
    const int w = rc.right, ht = rc.bottom;
    if (w <= 0 || ht <= 0) return -1;
    HDC src = GetDC(h);
    HDC mem = CreateCompatibleDC(src);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -ht;  // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ old = SelectObject(mem, bmp);
    BitBlt(mem, 0, 0, w, ht, src, 0, 0, SRCCOPY);
    auto px = static_cast<const uint32_t*>(bits);
    auto litFraction = [&] {
        size_t lit = 0;
        for (int i = 0; i < w * ht; ++i) if ((px[i] & 0xFFFFFF) != 0) ++lit;
        return (double)lit / (double)(w * ht);
    };
    double lit = litFraction();
    if (lit == 0.0) { PrintWindow(h, mem, PW_CLIENTONLY); lit = litFraction(); }  // ask the window to paint into our DC
    if (!path.empty()) {
        FILE* f = _wfopen(path.c_str(), L"wb");
        if (f) {
            const int row = (w * 3 + 3) & ~3;
            const uint32_t dataSize = (uint32_t)(row * ht);
            BITMAPFILEHEADER fh{0x4D42, (DWORD)(54 + dataSize), 0, 0, 54};
            BITMAPINFOHEADER ih{sizeof(BITMAPINFOHEADER), w, ht, 1, 24, BI_RGB, dataSize, 2835, 2835, 0, 0};
            fwrite(&fh, sizeof fh, 1, f);
            fwrite(&ih, sizeof ih, 1, f);
            std::string line((size_t)row, '\0');
            for (int y = ht - 1; y >= 0; --y) {  // BMP rows bottom-up
                for (int x = 0; x < w; ++x) {
                    uint32_t p = px[y * w + x];
                    line[(size_t)x * 3 + 0] = (char)(p & 0xFF);
                    line[(size_t)x * 3 + 1] = (char)((p >> 8) & 0xFF);
                    line[(size_t)x * 3 + 2] = (char)((p >> 16) & 0xFF);
                }
                fwrite(line.data(), 1, (size_t)row, f);
            }
            fclose(f);
        }
    }
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(h, src);
    return lit;
}

int cmdEditor(const std::wstring& pluginPath, int seconds, const std::wstring& shotPath, const char** stage) {
    std::string err;
    LoadTimings t;
    *stage = "load";
    auto p = Plugin::load(pluginPath, err, t);
    Json j;
    if (!p) {
        j.beginObj().boolean("ok", false).str("error", err).str("stage", *stage).endObj();
        printf("%s\n", j.text().c_str());
        return 2;
    }
    *stage = "createView/attached";
    EditorWindow ed;
    double t0 = nowMs();
    if (!ed.open(*p, err)) {
        j.beginObj().boolean("ok", false).str("error", err).str("stage", *stage).endObj();
        printf("%s\n", j.text().c_str());
        return 3;
    }
    const double openMs = nowMs() - t0;
    fprintf(stderr, "editor: %dx%d resizable=%d opened in %.0f ms\n", ed.width(), ed.height(), (int)ed.resizable(), openMs);

    *stage = "message loop";
    double end = nowMs() + seconds * 1000.0, shotAt = nowMs() + 2000.0;
    double litEarly = -1;
    bool closed = false;
    MSG m;
    while (!closed && nowMs() < end) {
        while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
        if (!ed.poll()) { closed = true; break; }
        if (litEarly < 0 && nowMs() >= shotAt) litEarly = ed.screenshot(shotPath.empty() ? L"" : shotPath + L".2s.bmp");
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 15, QS_ALLINPUT);
    }
    *stage = "screenshot";
    const double lit = closed ? -1 : ed.screenshot(shotPath);
    *stage = "close";
    const int w = ed.width(), h = ed.height(), resizes = ed.resizes();
    const bool resizable = ed.resizable();
    ed.close();

    j.beginObj().boolean("ok", true).str("name", p->name).num("width", w).num("height", h)
        .boolean("resizable", resizable).num("openMs", openMs).num("resizeRequests", resizes)
        .num("litFraction2s", litEarly).num("litFractionEnd", lit).boolean("closedByUser", closed).endObj();
    printf("%s\n", j.text().c_str());
    return 0;
}

}  // namespace vg
