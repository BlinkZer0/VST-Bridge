// SPDX-License-Identifier: GPL-3.0-or-later
//
// JNI surface of the in-app Wayland compositor (shared/wl/vg_wl.*) that shows Windows plugin editors.
// One compositor per app process; Wine processes connect to it through WAYLAND_DISPLAY.
#include <android/log.h>
#include <jni.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <mutex>
#include <vector>
#include <string>

#include "wl/vg_wl.h"

namespace {
std::mutex g_m;
vgwl::Compositor* g_wl = nullptr;
vgwl::Snapshot g_snap;  // last composed picture, kept so a too-small Java buffer can retry
}  // namespace

// Null on success, else an error message.
extern "C" JNIEXPORT jstring JNICALL
Java_com_vstghost_NativeEngine_wlStart(JNIEnv* env, jclass, jstring path, jint w, jint h) {
    std::lock_guard<std::mutex> lk(g_m);
    const char* c = env->GetStringUTFChars(path, nullptr);
    std::string p(c);
    env->ReleaseStringUTFChars(path, c);
    if (g_wl && g_wl->running()) {
        if (access(p.c_str(), F_OK) == 0) return nullptr;
        g_wl->stop();  // socket file gone (payload reinstalled): listen again
    }
    if (!g_wl) g_wl = new vgwl::Compositor;
    std::string err;
    if (!g_wl->start(p, w, h, err)) {
        __android_log_print(ANDROID_LOG_ERROR, "VSTGHOST", "compositor: %s", err.c_str());
        return env->NewStringUTF(err.c_str());
    }
    __android_log_print(ANDROID_LOG_INFO, "VSTGHOST", "compositor listening on %s (%dx%d)", p.c_str(), w, h);
    return nullptr;
}

// Returns the picture's serial; `have` if unchanged; -1 if `buf` is smaller than meta[0]*meta[1]
// (allocate and call again with have = 0); -2 if the compositor is not running.
// meta[0], meta[1] = width, height (0 x 0 = nothing on screen).
extern "C" JNIEXPORT jlong JNICALL
Java_com_vstghost_NativeEngine_wlSnapshot(JNIEnv* env, jclass, jlong have, jintArray buf, jintArray meta) {
    std::lock_guard<std::mutex> lk(g_m);
    if (!g_wl || !g_wl->running()) return -2;
    g_wl->snapshot(g_snap.serial, g_snap);
    if ((jlong)g_snap.serial == have) return have;
    jint m[2] = {g_snap.width, g_snap.height};
    env->SetIntArrayRegion(meta, 0, 2, m);
    const jsize need = (jsize)((size_t)g_snap.width * g_snap.height);
    if (need > 0) {
        if (!buf || env->GetArrayLength(buf) < need) return -1;
        env->SetIntArrayRegion(buf, 0, need, reinterpret_cast<const jint*>(g_snap.argb.data()));
    }
    return (jlong)g_snap.serial;
}

// Pointer at picture coordinates; button -1 = move, 1 = press, 0 = release.
extern "C" JNIEXPORT void JNICALL
Java_com_vstghost_NativeEngine_wlPointer(JNIEnv*, jclass, jfloat x, jfloat y, jint button) {
    std::lock_guard<std::mutex> lk(g_m);
    if (g_wl) g_wl->pointer(x, y, button);
}

extern "C" JNIEXPORT void JNICALL
Java_com_vstghost_NativeEngine_wlScroll(JNIEnv*, jclass, jfloat notches) {
    std::lock_guard<std::mutex> lk(g_m);
    if (g_wl) g_wl->scroll(notches);
}

// Test hook: writes the current picture as a 24-bit BMP. Returns "WxH" or null.
extern "C" JNIEXPORT jstring JNICALL
Java_com_vstghost_NativeEngine_wlDump(JNIEnv* env, jclass, jstring path) {
    std::lock_guard<std::mutex> lk(g_m);
    if (!g_wl) return nullptr;
    vgwl::Snapshot s;
    g_wl->snapshot(0, s);
    if (s.width <= 0) return nullptr;
    const char* c = env->GetStringUTFChars(path, nullptr);
    FILE* f = fopen(c, "wb");
    env->ReleaseStringUTFChars(path, c);
    if (!f) return nullptr;
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
            const uint32_t p = s.argb[(size_t)y * s.width + x];
            line[(size_t)x * 3] = p & 0xFF; line[(size_t)x * 3 + 1] = (p >> 8) & 0xFF; line[(size_t)x * 3 + 2] = (p >> 16) & 0xFF;
        }
        fwrite(line.data(), 1, line.size(), f);
    }
    fclose(f);
    return env->NewStringUTF((std::to_string(s.width) + "x" + std::to_string(s.height)).c_str());
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_vstghost_NativeEngine_wlStats(JNIEnv* env, jclass) {
    std::lock_guard<std::mutex> lk(g_m);
    return env->NewStringUTF(g_wl ? g_wl->stats().c_str() : "{}");
}
