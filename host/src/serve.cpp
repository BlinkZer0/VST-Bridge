// SPDX-License-Identifier: LicenseRef-VstGhost-Proprietary
//
// `vstghost-host serve <plugin> --shm <file>`: live mode for the real-time engine.
// See shared/ipc/vg_ipc.h for the shared-memory protocol.
#include "serve.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <string>
#include <vector>

#include "editor.h"
#include "ipc/vg_ipc.h"
#include "linux_sys.h"
#include "util.h"
#include "vst3host.h"

namespace vg {
namespace {

vg_ipc* g_ipc = nullptr;
Plugin* g_plugin = nullptr;
bool g_futex = false;
bool g_pattern = false;  // ring integrity test: write frame index instead of plugin audio
std::atomic<bool> g_quit{false};

// Parameter changes from the main thread to the render thread (SPSC, process-local).
constexpr size_t kParamQ = 1024;
BlockParam g_pq[kParamQ];
std::atomic<uint64_t> g_pqWrite{0}, g_pqRead{0};
EditorWindow* g_editor = nullptr;

// Main thread only (single producer): hand a parameter change to the render thread.
void queueParam(uint32_t id, double v) {
    uint64_t w = g_pqWrite.load(std::memory_order_relaxed);
    if (w - g_pqRead.load(std::memory_order_acquire) < kParamQ) {
        g_pq[w % kParamQ] = {id, 0, v};
        g_pqWrite.store(w + 1, std::memory_order_release);
    }
}

void wake(uint32_t* w) {
    VG_ADD(w, 1u);
    if (g_futex) linux_sys::futexWake(w, 8);
}

// Sleep until *w changes from `seen` (or timeout). Poll mode just sleeps briefly.
void waitOn(uint32_t* w, uint32_t seen, int64_t timeoutNs) {
    if (g_futex) {
        linux_sys::futexWait(w, seen, timeoutNs);
    } else if (VG_LOAD(w) == seen) {
        Sleep(1);
    }
}

// ---------------------------------------------------------------- base64

const char* kB64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string b64enc(const std::string& in) {
    std::string out;
    out.reserve((in.size() + 2) / 3 * 4);
    size_t i = 0;
    for (; i + 2 < in.size(); i += 3) {
        uint32_t v = (uint8_t)in[i] << 16 | (uint8_t)in[i + 1] << 8 | (uint8_t)in[i + 2];
        out += kB64[v >> 18]; out += kB64[(v >> 12) & 63]; out += kB64[(v >> 6) & 63]; out += kB64[v & 63];
    }
    if (i + 1 == in.size()) {
        uint32_t v = (uint8_t)in[i] << 16;
        out += kB64[v >> 18]; out += kB64[(v >> 12) & 63]; out += "==";
    } else if (i + 2 == in.size()) {
        uint32_t v = (uint8_t)in[i] << 16 | (uint8_t)in[i + 1] << 8;
        out += kB64[v >> 18]; out += kB64[(v >> 12) & 63]; out += kB64[(v >> 6) & 63]; out += '=';
    }
    return out;
}

std::string b64dec(const std::string& in) {
    int8_t map[256];
    memset(map, -1, sizeof map);
    for (int i = 0; i < 64; ++i) map[(uint8_t)kB64[i]] = (int8_t)i;
    std::string out;
    uint32_t acc = 0;
    int bits = 0;
    for (char ch : in) {
        int8_t v = map[(uint8_t)ch];
        if (v < 0) continue;
        acc = acc << 6 | (uint32_t)v;
        bits += 6;
        if (bits >= 8) { bits -= 8; out += (char)((acc >> bits) & 0xFF); }
    }
    return out;
}

// ---------------------------------------------------------------- render thread

DWORD WINAPI renderThread(void*) {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    vg_ipc* s = g_ipc;
    const uint32_t B = s->blockSize, R = s->ringFrames, M = s->midiSize;
    const int outCh = g_pattern ? VG_IPC_CHANNELS : std::min<int>(g_plugin->outChannels(), VG_IPC_CHANNELS);
    const int inCh = g_pattern ? 0 : std::min<int>(g_plugin->inChannels(), VG_IPC_CHANNELS);
    std::vector<std::vector<float>> outBuf(VG_IPC_CHANNELS, std::vector<float>(B)), inBuf(VG_IPC_CHANNELS, std::vector<float>(B));
    std::vector<float*> outP(VG_IPC_CHANNELS);
    std::vector<const float*> inP(VG_IPC_CHANNELS);
    for (int c = 0; c < VG_IPC_CHANNELS; ++c) { outP[c] = outBuf[c].data(); inP[c] = inBuf[c].data(); }
    std::vector<BlockMidi> midi;
    std::vector<BlockParam> prm;
    midi.reserve(M);
    prm.reserve(kParamQ);
    BlockCounters cnt;
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);

    while (!g_quit.load() && !VG_LOAD(&s->stopFlag)) {
        const uint64_t w = VG_LOAD(&s->outWrite), r = VG_LOAD(&s->outRead);
        const uint32_t target = std::min<uint32_t>(VG_LOAD(&s->targetFill), R - B);
        if (w - r >= target) {
            const uint32_t seen = VG_LOAD(&s->hostWake);
            if (VG_LOAD(&s->outRead) == r) waitOn(&s->hostWake, seen, 20'000'000);
            continue;
        }
        // MIDI: everything queued goes into this block at offset 0 (live input, as soon as possible).
        midi.clear();
        uint64_t mr = VG_LOAD(&s->midiRead);
        const uint64_t mw = VG_LOAD(&s->midiWrite);
        uint32_t probeSeq = 0;
        for (; mr < mw && midi.size() < M; ++mr) {
            const vg_midi_event& e = s->midi[mr & (M - 1)];
            midi.push_back({0, e.status, e.d1, e.d2});
            if (e.seq) probeSeq = e.seq;
        }
        VG_STORE(&s->midiRead, mr);
        prm.clear();
        for (uint64_t pr = g_pqRead.load(std::memory_order_acquire), pw = g_pqWrite.load(std::memory_order_acquire); pr < pw; ++pr) {
            prm.push_back(g_pq[pr % kParamQ]);
            g_pqRead.store(pr + 1, std::memory_order_release);
        }
        // Effect input.
        if (inCh > 0) {
            const uint64_t iw = VG_LOAD(&s->inWrite), ir = VG_LOAD(&s->inRead);
            const uint32_t avail = (uint32_t)std::min<uint64_t>(iw - ir, B);
            for (int c = 0; c < inCh; ++c) {
                for (uint32_t i = 0; i < avail; ++i) inBuf[c][i] = s->in[c][(ir + i) & (R - 1)];
                std::fill(inBuf[c].begin() + avail, inBuf[c].end(), 0.0f);
            }
            VG_STORE(&s->inRead, ir + avail);
        }

        LARGE_INTEGER t0, t1;
        QueryPerformanceCounter(&t0);
        if (g_pattern) {
            for (int c = 0; c < VG_IPC_CHANNELS; ++c)
                for (uint32_t i = 0; i < B; ++i) outBuf[c][i] = (float)((w + i) & 0xFFFFFF);
        } else {
            g_plugin->process((int32_t)B, midi.data(), midi.size(), prm.data(), prm.size(),
                              inCh ? inP.data() : nullptr, outP.data(), (int64_t)w, cnt);
        }
        QueryPerformanceCounter(&t1);
        const uint32_t ns = (uint32_t)((t1.QuadPart - t0.QuadPart) * 1000000000LL / freq.QuadPart);

        for (int c = 0; c < VG_IPC_CHANNELS; ++c) {
            const float* src = outBuf[c < outCh ? c : 0].data();
            float* dst = s->out[c];
            for (uint32_t i = 0; i < B; ++i) dst[(w + i) & (R - 1)] = src[i];
        }
        if (probeSeq) { VG_STORE(&s->lastProbeFrame, w); VG_STORE(&s->lastProbeSeq, (uint64_t)probeSeq); }
        VG_STORE(&s->lastBlockNs, ns);
        if (ns > VG_LOAD(&s->maxBlockNs)) VG_STORE(&s->maxBlockNs, ns);
        VG_ADD(&s->renderNsTotal, (uint64_t)ns);
        VG_ADD(&s->blocksRendered, (uint64_t)1);
        VG_STORE(&s->outWrite, w + B);
    }
    return 0;
}

// ---------------------------------------------------------------- control requests

std::vector<std::string> splitLines(const std::string& s) {
    std::vector<std::string> v;
    size_t p = 0;
    while (p <= s.size()) {
        size_t e = s.find('\n', p);
        if (e == std::string::npos) e = s.size();
        v.push_back(s.substr(p, e - p));
        p = e + 1;
    }
    return v;
}

std::string handle(const std::string& req) {
    auto a = splitLines(req);
    const std::string& cmd = a[0];
    Json j;
    j.beginObj().str("cmd", cmd);
    if (cmd == "ping" || !g_plugin) {
        j.boolean("ok", cmd == "ping").boolean("pattern", g_pattern);
        return j.endObj().text();
    }
    Plugin& p = *g_plugin;
    if (cmd == "info") {
        size_t user = 0;
        for (const auto& d : p.params) user += isUserParam(d);
        j.boolean("ok", true).str("name", p.name).str("vendor", p.vendor).str("version", p.version)
            .boolean("instrument", p.isInstrument).num("inChannels", p.inChannels()).num("outChannels", p.outChannels())
            .num("paramCount", (double)user).boolean("futex", g_futex);
    } else if (cmd == "params") {
        j.boolean("ok", true).beginArr("params");
        for (const auto& d : p.params) {
            if (!isUserParam(d)) continue;
            double v = p.getParam(d.id);
            j.beginObj().num("id", d.id).str("title", d.title).str("units", d.units).num("steps", d.stepCount)
                .num("value", v).num("default", d.defaultNormalized).str("text", p.paramText(d.id, v)).endObj();
        }
        j.endArr();
    } else if (cmd == "params_tsv") {
        // For native consumers (AAP glue): "id \t steps \t default \t value \t title" per line.
        std::string out;
        for (const auto& d : p.params) {
            if (!isUserParam(d)) continue;
            char b[64];
            snprintf(b, sizeof b, "%u\t%d\t%.9g\t%.9g\t", d.id, d.stepCount, d.defaultNormalized, p.getParam(d.id));
            out += b;
            for (char ch : d.title) out += (ch == '\t' || ch == '\n') ? ' ' : ch;
            out += '\n';
        }
        return out;
    } else if (cmd == "set" && a.size() >= 3) {
        uint32_t id = (uint32_t)strtoul(a[1].c_str(), nullptr, 10);
        double v = std::clamp(strtod(a[2].c_str(), nullptr), 0.0, 1.0);
        p.setParam(id, v);
        queueParam(id, v);
        j.boolean("ok", true).num("id", id).num("value", p.getParam(id)).str("text", p.paramText(id, p.getParam(id)));
    } else if (cmd == "get_state") {
        std::string c, k;
        bool ok = p.getState(c, k);
        j.boolean("ok", ok).str("component", b64enc(c)).str("controller", b64enc(k));
    } else if (cmd == "set_state" && a.size() >= 3) {
        std::string err;
        bool ok = p.setState(b64dec(a[1]), b64dec(a[2]), err);
        j.boolean("ok", ok);
        if (!ok) j.str("error", err);
    } else if (cmd == "programs") {
        // Factory programs (VST3 program lists). "set_program <listId> <index>" selects one.
        j.boolean("ok", true).beginArr("lists");
        for (const auto& pl : p.programLists) {
            double cur = -1;
            if (pl.paramId != 0xFFFFFFFFu && pl.programs.size() > 1) cur = std::round(p.getParam(pl.paramId) * (double)(pl.programs.size() - 1));
            j.beginObj().num("id", pl.id).str("name", pl.name).boolean("selectable", pl.paramId != 0xFFFFFFFFu).num("current", cur).beginArr("programs");
            for (const auto& n : pl.programs) j.str(nullptr, n);
            j.endArr().endObj();
        }
        j.endArr();
    } else if (cmd == "set_program" && a.size() >= 3) {
        const int32_t listId = atoi(a[1].c_str());
        const int32_t index = atoi(a[2].c_str());
        const ProgramList* pl = nullptr;
        for (const auto& l : p.programLists) if (l.id == listId) pl = &l;
        if (!pl || pl->paramId == 0xFFFFFFFFu) j.boolean("ok", false).str("error", "no program-change parameter");
        else if (index < 0 || index >= (int32_t)pl->programs.size()) j.boolean("ok", false).str("error", "no such program");
        else {
            const double v = pl->programs.size() > 1 ? (double)index / (double)(pl->programs.size() - 1) : 0.0;
            p.setParam(pl->paramId, v);
            queueParam(pl->paramId, v);
            j.boolean("ok", true).str("program", pl->programs[(size_t)index]);
        }
    } else if (cmd == "load_preset" && a.size() >= 2) {
        std::string err;
        const bool ok = p.loadVstPreset(toWinPath(wide(a[1])), err);
        j.boolean("ok", ok);
        if (!ok) j.str("error", err);
    } else if (cmd == "save_preset" && a.size() >= 2) {
        std::string err;
        const bool ok = p.saveVstPreset(toWinPath(wide(a[1])), err);
        j.boolean("ok", ok);
        if (!ok) j.str("error", err);
    } else if (cmd == "editor_open") {
        std::string err;
        const double t0 = nowMs();
        const bool ok = g_editor && g_editor->open(p, err);
        j.boolean("ok", ok).num("openMs", nowMs() - t0);
        if (ok) j.num("width", g_editor->width()).num("height", g_editor->height()).boolean("resizable", g_editor->resizable());
        else j.str("error", g_editor ? err : "no editor support");
    } else if (cmd == "editor_close") {
        if (g_editor) g_editor->close();
        j.boolean("ok", true);
    } else if (cmd == "editor_visible" && a.size() >= 2) {
        if (g_editor) g_editor->setVisible(a[1] != "0");
        j.boolean("ok", g_editor != nullptr);
    } else if (cmd == "editor_state") {
        const bool open = g_editor && g_editor->isOpen();
        j.boolean("ok", true).boolean("open", open);
        if (open) j.num("width", g_editor->width()).num("height", g_editor->height());
    } else {
        j.boolean("ok", false).str("error", "unknown command");
    }
    j.endObj();
    return j.text();
}

}  // namespace

int cmdServe(const std::wstring& pluginPath, const std::wstring& shmPath, bool quiet, bool pattern) {
    g_pattern = pattern;
    HANDLE f = CreateFileW(shmPath.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) { fprintf(stderr, "serve: cannot open shm file\n"); return 2; }
    HANDLE m = CreateFileMappingW(f, nullptr, PAGE_READWRITE, 0, 0, nullptr);
    void* view = m ? MapViewOfFile(m, FILE_MAP_ALL_ACCESS, 0, 0, 0) : nullptr;
    if (!view) { fprintf(stderr, "serve: cannot map shm file (%lu)\n", GetLastError()); return 2; }
    vg_ipc* s = g_ipc = (vg_ipc*)view;
    if (s->magic != VG_IPC_MAGIC || s->version != VG_IPC_VERSION || s->ringFrames != VG_IPC_RING_FRAMES) {
        fprintf(stderr, "serve: bad shm header\n");
        return 2;
    }
    // Probe raw futex support: FUTEX_WAKE on our own word returns >= 0 (woken count) if it works.
    const long long fr = linux_sys::futexWake(&s->engineWake, 0);
    g_futex = fr >= 0;
    VG_STORE(&s->wakeMode, g_futex ? VG_WAKE_FUTEX : VG_WAKE_POLL);
    VG_STORE(&s->hostPid, (uint32_t)linux_sys::getpid());
    const long long parent = linux_sys::getppid();
    if (!quiet) fprintf(stderr, "serve: futex=%d (ret %lld) pid=%lld ppid=%lld\n", (int)g_futex, fr, linux_sys::getpid(), parent);

    VG_STORE(&s->hostState, VG_HOST_LOADING);
    std::string err;
    LoadTimings t;
    if (g_pattern) {
        VG_STORE(&s->hostState, VG_HOST_RUNNING);
        wake(&s->engineWake);
        HANDLE th = CreateThread(nullptr, 0, renderThread, nullptr, 0, nullptr);
        const long long parent = linux_sys::getppid();
        while (!VG_LOAD(&s->stopFlag) && linux_sys::getppid() == parent) {
            const uint32_t seen = VG_LOAD(&s->ctrlWake);
            const uint32_t req = VG_LOAD(&s->reqSeq);
            if (req != VG_LOAD(&s->respSeq)) {
                std::string resp = handle(std::string(s->req, std::min<uint32_t>(VG_LOAD(&s->reqLen), VG_IPC_REQ_BYTES)));
                memcpy(s->resp, resp.data(), resp.size());
                VG_STORE(&s->respLen, (uint32_t)resp.size());
                VG_STORE(&s->respSeq, req);
                wake(&s->engineWake);
            }
            waitOn(&s->ctrlWake, seen, 100'000'000);
        }
        g_quit = true;
        wake(&s->hostWake);
        WaitForSingleObject(th, 2000);
        VG_STORE(&s->hostState, VG_HOST_EXITED);
        wake(&s->engineWake);
        return 0;
    }
    auto plugin = Plugin::load(pluginPath, err, t);
    if (!plugin || !plugin->begin(s->sampleRate, (int32_t)s->blockSize, true, err)) {
        fprintf(stderr, "serve: %s\n", err.c_str());
        snprintf(s->resp, VG_IPC_RESP_BYTES, "{\"ok\":false,\"error\":\"load: %s\"}", err.c_str());
        VG_STORE(&s->hostState, VG_HOST_FAILED);
        wake(&s->engineWake);
        return 2;
    }
    g_plugin = plugin.get();
    // Knob moves in the plugin's own editor reach the processor like our `set` requests do.
    plugin->setEditCallback([](uint32_t id, double v) { queueParam(id, v); });
    EditorWindow editor;
    g_editor = &editor;

    // Pre-roll so Box64 translates the DSP code before anyone plays: silence, a chord, release.
    {
        const int B = (int)s->blockSize, outCh = plugin->outChannels(), inCh = plugin->inChannels();
        std::vector<std::vector<float>> o((size_t)std::max(outCh, 1), std::vector<float>((size_t)B)), in((size_t)std::max(inCh, 1), std::vector<float>((size_t)B));
        std::vector<float*> op; std::vector<const float*> ip;
        for (auto& v : o) op.push_back(v.data());
        for (auto& v : in) ip.push_back(v.data());
        BlockCounters c;
        const int blocksPerSec = (int)s->sampleRate / B;
        int64_t pos = 0;
        auto run = [&](int blocks, const std::vector<BlockMidi>& first) {
            for (int i = 0; i < blocks; ++i, pos += B)
                plugin->process(B, i == 0 ? first.data() : nullptr, i == 0 ? first.size() : 0, nullptr, 0,
                                inCh ? ip.data() : nullptr, op.data(), pos, c);
        };
        double t0 = nowMs();
        run(blocksPerSec / 2, {});
        run(blocksPerSec, {{0, 0x90, 48, 100}, {0, 0x90, 60, 110}, {0, 0x90, 64, 90}, {0, 0x90, 67, 80}, {0, 0x90, 72, 127}});
        run(blocksPerSec, {{0, 0x80, 48, 0}, {0, 0x80, 60, 0}, {0, 0x80, 64, 0}, {0, 0x80, 67, 0}, {0, 0x80, 72, 0}});
        if (!quiet) fprintf(stderr, "serve: pre-roll %.0f ms\n", nowMs() - t0);
    }

    snprintf(s->resp, VG_IPC_RESP_BYTES, "{\"ok\":true,\"loadMs\":%.1f}", t.loadLibraryMs + t.factoryMs + t.componentMs + t.controllerMs);
    VG_STORE(&s->hostState, VG_HOST_RUNNING);
    wake(&s->engineWake);
    HANDLE th = CreateThread(nullptr, 0, renderThread, nullptr, 0, nullptr);

    // Main thread: control requests, Win32 messages, parent watchdog.
    uint32_t handled = VG_LOAD(&s->respSeq);
    double lastParentCheck = nowMs();
    while (!VG_LOAD(&s->stopFlag)) {
        const uint32_t seen = VG_LOAD(&s->ctrlWake);
        const uint32_t req = VG_LOAD(&s->reqSeq);
        if (req != handled) {
            uint32_t len = std::min<uint32_t>(VG_LOAD(&s->reqLen), VG_IPC_REQ_BYTES);
            std::string resp = handle(std::string(s->req, len));
            if (resp.size() >= VG_IPC_RESP_BYTES) resp = "{\"ok\":false,\"error\":\"response too large\"}";
            memcpy(s->resp, resp.data(), resp.size());
            VG_STORE(&s->respLen, (uint32_t)resp.size());
            VG_STORE(&s->respSeq, req);
            handled = req;
            wake(&s->engineWake);
            continue;
        }
        pumpMessages(0);
        if (editor.isOpen()) editor.poll();
        if (nowMs() - lastParentCheck > 500) {
            lastParentCheck = nowMs();
            if (linux_sys::getppid() != parent) { fprintf(stderr, "serve: parent gone, exiting\n"); break; }
        }
        // An open editor wants its input and paint messages promptly.
        waitOn(&s->ctrlWake, seen, editor.isOpen() ? 4'000'000 : 10'000'000);
    }
    editor.close();
    g_editor = nullptr;
    g_quit = true;
    VG_ADD(&s->hostWake, 1u);
    if (g_futex) linux_sys::futexWake(&s->hostWake, 8);
    WaitForSingleObject(th, 2000);
    plugin->end();
    VG_STORE(&s->hostState, VG_HOST_EXITED);
    wake(&s->engineWake);
    return 0;
}

}  // namespace vg
