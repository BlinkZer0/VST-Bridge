// SPDX-License-Identifier: GPL-3.0-or-later
//
// PC test driver for the vgwl compositor (shared/wl). Runs the compositor on a socket, then follows a
// script of timed actions so Wine's Wayland driver can be exercised without the phone:
//
//   vgwl_test <socket> <seconds> <outdir> [action...]
//     shot:<t>:<name>          save the composited picture as <outdir>/<name>.bmp at t seconds
//     click:<t>:<x>:<y>        move + press + release at (x, y) at t seconds
//     drag:<t>:<x>:<y>:<dy>    press at (x, y), move by dy in 10 steps, release
//     scroll:<t>:<x>:<y>:<n>   move to (x, y), scroll n notches
//     close:<t>                send xdg_toplevel.close to the shown window
// Prints compositor stats every second and a JSON line per shot.
//
// Build: g++ -std=c++17 -O2 -pthread -I shared tools/wl/vgwl_test.cpp shared/wl/vg_wl.cpp -o vgwl_test
#include <time.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "wl/vg_wl.h"

static double now() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

static bool writeBmp(const std::string& path, const vgwl::Snapshot& s) {
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    const int row = (s.width * 3 + 3) & ~3;
    const uint32_t data = (uint32_t)(row * s.height);
    uint8_t h[54] = {'B', 'M'};
    auto put32 = [&](int o, uint32_t v) { memcpy(h + o, &v, 4); };
    put32(2, 54 + data); put32(10, 54); put32(14, 40); put32(18, (uint32_t)s.width); put32(22, (uint32_t)s.height);
    h[26] = 1; h[28] = 24; put32(34, data);
    fwrite(h, 1, 54, f);
    std::vector<uint8_t> line((size_t)row);
    for (int y = s.height - 1; y >= 0; --y) {
        for (int x = 0; x < s.width; ++x) {
            uint32_t p = s.argb[(size_t)y * s.width + x];
            line[(size_t)x * 3] = p & 0xFF;
            line[(size_t)x * 3 + 1] = (p >> 8) & 0xFF;
            line[(size_t)x * 3 + 2] = (p >> 16) & 0xFF;
        }
        fwrite(line.data(), 1, line.size(), f);
    }
    fclose(f);
    return true;
}

struct Action { double t; std::string kind; std::vector<std::string> a; bool done = false; };

int main(int argc, char** argv) {
    if (argc < 4) { fprintf(stderr, "usage: vgwl_test <socket> <seconds> <outdir> [action...]\n"); return 1; }
    const std::string sock = argv[1], outdir = argv[3];
    const double seconds = atof(argv[2]);
    std::vector<Action> acts;
    for (int i = 4; i < argc; ++i) {
        std::vector<std::string> parts;
        std::string s = argv[i];
        size_t p = 0;
        while (true) {
            size_t e = s.find(':', p);
            parts.push_back(s.substr(p, e == std::string::npos ? std::string::npos : e - p));
            if (e == std::string::npos) break;
            p = e + 1;
        }
        if (parts.size() < 2) continue;
        Action a;
        a.kind = parts[0];
        a.t = atof(parts[1].c_str());
        a.a.assign(parts.begin() + 2, parts.end());
        acts.push_back(a);
    }
    vgwl::Compositor c;
    std::string err;
    if (!c.start(sock, 1920, 1080, err)) { fprintf(stderr, "start: %s\n", err.c_str()); return 2; }
    fprintf(stderr, "vgwl_test: listening on %s\n", sock.c_str());
    const double t0 = now();
    double lastStats = 0;
    vgwl::Snapshot snap;
    while (now() - t0 < seconds) {
        const double t = now() - t0;
        c.snapshot(snap.serial, snap);
        for (auto& a : acts) {
            if (a.done || t < a.t) continue;
            a.done = true;
            if (a.kind == "shot" && !a.a.empty()) {
                size_t lit = 0;
                for (uint32_t px : snap.argb) lit += (px & 0xFFFFFF) != 0;
                const bool ok = snap.width > 0 && writeBmp(outdir + "/" + a.a[0] + ".bmp", snap);
                printf("{\"shot\":\"%s\",\"t\":%.1f,\"ok\":%s,\"width\":%d,\"height\":%d,\"lit\":%.4f,\"title\":\"%s\"}\n", a.a[0].c_str(), t,
                       ok ? "true" : "false", snap.width, snap.height, snap.argb.empty() ? 0.0 : (double)lit / snap.argb.size(), snap.title.c_str());
                fflush(stdout);
            } else if (a.kind == "click" && a.a.size() >= 2) {
                double x = atof(a.a[0].c_str()), y = atof(a.a[1].c_str());
                c.pointer(x, y, -1);
                usleep(50000);
                c.pointer(x, y, 1);
                usleep(80000);
                c.pointer(x, y, 0);
            } else if (a.kind == "drag" && a.a.size() >= 3) {
                double x = atof(a.a[0].c_str()), y = atof(a.a[1].c_str()), dy = atof(a.a[2].c_str());
                c.pointer(x, y, -1);
                usleep(50000);
                c.pointer(x, y, 1);
                for (int k = 1; k <= 10; ++k) { usleep(30000); c.pointer(x, y + dy * k / 10, -1); }
                usleep(50000);
                c.pointer(x, y + dy, 0);
            } else if (a.kind == "scroll" && a.a.size() >= 3) {
                c.pointer(atof(a.a[0].c_str()), atof(a.a[1].c_str()), -1);
                usleep(30000);
                c.scroll(atof(a.a[2].c_str()));
            } else if (a.kind == "close") {
                c.closeShown();
            }
        }
        if (t - lastStats >= 1.0) { lastStats = t; fprintf(stderr, "[%.0fs] %s\n", t, c.stats().c_str()); }
        usleep(20000);
    }
    c.stop();
    return 0;
}
