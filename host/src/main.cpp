// SPDX-License-Identifier: GPL-3.0-or-later
//
// vstghost-host.exe: VST gHost bridge host (Phase 1: offline).
//
//   vstghost-host scan   <dir>                       list .vst3 bundles/files (no loading)
//   vstghost-host info   <plugin>                    load, print classes/buses/parameters
//   vstghost-host render <plugin> --out <wav> [--midi <mid>] [--input <wav>|builtin]
//                        [--rate 48000] [--block 256] [--tail 3] [--realtime]
//
// Results are a single JSON object on stdout; diagnostics go to stderr.
// Exit codes: 0 ok, 1 usage, 2 load failed, 3 render failed, 70 crashed.
// Unix absolute paths are accepted and mapped to Wine's Z: drive.

#include <windows.h>

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "midifile.h"
#include "util.h"
#include "vst3host.h"
#include "wav.h"

using namespace vg;

namespace {

const char* g_stage = "startup";

LONG WINAPI crashFilter(EXCEPTION_POINTERS* ep) {
    const auto* er = ep->ExceptionRecord;
    HMODULE mod = nullptr;
    wchar_t name[MAX_PATH] = L"?";
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCWSTR)er->ExceptionAddress, &mod))
        GetModuleFileNameW(mod, name, MAX_PATH);
    Json j;
    j.beginObj()
        .boolean("ok", false)
        .str("error", "crashed")
        .str("stage", g_stage)
        .num("exceptionCode", (double)er->ExceptionCode)
        .str("exceptionHex", [&] { char b[16]; snprintf(b, sizeof b, "0x%08lX", er->ExceptionCode); return std::string(b); }())
        .str("module", utf8(std::wstring(name)))
        .num("offset", mod ? (double)((uintptr_t)er->ExceptionAddress - (uintptr_t)mod) : 0)
        .endObj();
    printf("%s\n", j.text().c_str());
    fflush(stdout);
    fprintf(stderr, "CRASH 0x%08lX in %ls during %s\n", er->ExceptionCode, name, g_stage);
    fflush(stderr);
    ExitProcess(70);
    return EXCEPTION_EXECUTE_HANDLER;
}

int usage() {
    fprintf(stderr, "usage: vstghost-host scan <dir> | info <plugin> | render <plugin> --out <wav> [--midi <mid>] "
                    "[--input <wav>|builtin] [--rate N] [--block N] [--tail SEC] [--realtime]\n");
    return 1;
}

// Deterministic effect test signal (stereo): plucked notes (Karplus-Strong), a bass line and
// noise "snare" hits, 8 seconds. Gives an effect transients, sustain and silence to work on.
std::vector<std::vector<float>> builtinSignal(double rate) {
    const int n = (int)(8 * rate);
    std::vector<std::vector<float>> s(2, std::vector<float>((size_t)n, 0.0f));
    uint32_t seed = 1;
    auto rnd = [&] { seed = seed * 1664525u + 1013904223u; return (float)((seed >> 8) / 8388608.0 - 1.0); };
    const double notes[] = {196.0, 246.94, 293.66, 392.0, 329.63, 293.66, 246.94, 220.0};
    for (int k = 0; k < 8; ++k) {  // plucks, every second
        int start = (int)(k * rate), period = (int)(rate / notes[k]);
        std::vector<float> line((size_t)period);
        for (auto& v : line) v = rnd() * 0.5f;
        for (int i = 0; i < (int)(0.9 * rate) && start + i < n; ++i) {
            size_t a = (size_t)(i % period), b = (size_t)((i + 1) % period);
            float v = line[a];
            line[a] = 0.996f * 0.5f * (line[a] + line[b]);
            s[0][(size_t)(start + i)] += v * 0.7f;
            s[1][(size_t)(start + i)] += v * 0.5f;
        }
    }
    for (int i = 0; i < n; ++i) {  // bass: 55 Hz saw, gated in eighths
        double t = i / rate;
        double ph = std::fmod(t * 55.0, 1.0);
        float g = std::fmod(t * 4.0, 1.0) < 0.6 ? 0.15f : 0.0f;
        float v = (float)(2 * ph - 1) * g;
        s[0][(size_t)i] += v; s[1][(size_t)i] += v;
    }
    for (int k = 0; k < 16; ++k) {  // snare-ish noise bursts on beats 2 and 4
        if (k % 2 == 0) continue;
        int start = (int)(k * 0.5 * rate);
        for (int i = 0; i < (int)(0.15 * rate) && start + i < n; ++i) {
            float env = std::exp(-i / (0.03f * (float)rate));
            float v = rnd() * 0.4f * env;
            s[0][(size_t)(start + i)] += v; s[1][(size_t)(start + i)] += v;
        }
    }
    return s;
}

void paramsJson(Json& j, const Plugin& p) {
    j.beginArr("params");
    for (const auto& d : p.params) {
        j.beginObj().num("id", d.id).str("title", d.title).str("units", d.units).num("steps", d.stepCount)
            .num("default", d.defaultNormalized).str("defaultText", d.defaultText).num("flags", d.flags).endObj();
    }
    j.endArr();
}

void pluginJson(Json& j, const Plugin& p, const LoadTimings& t, bool withParams) {
    j.str("name", p.name).str("vendor", p.vendor).str("version", p.version).str("sdk", p.sdkVersion)
        .str("subCategories", p.subCategories).boolean("instrument", p.isInstrument).str("cid", p.cid)
        .str("dll", p.dllPath).boolean("separateController", p.separateController).boolean("midiMapping", p.hasMidiMapping);
    auto buses = [&](const char* key, const std::vector<BusDesc>& v) {
        j.beginArr(key);
        for (const auto& b : v) j.beginObj().str("name", b.name).num("channels", b.channels).boolean("main", b.main).endObj();
        j.endArr();
    };
    buses("audioIn", p.audioIn);
    buses("audioOut", p.audioOut);
    buses("eventIn", p.eventIn);
    j.num("paramCount", (double)p.params.size());
    j.beginObj("loadMs").num("loadLibrary", t.loadLibraryMs).num("factory", t.factoryMs).num("component", t.componentMs)
        .num("controller", t.controllerMs).endObj();
    if (withParams) paramsJson(j, p);
}

int cmdScan(const std::wstring& dir) {
    Json j;
    j.beginObj().boolean("ok", true).beginArr("plugins");
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*.vst3").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            j.beginObj().str("name", utf8(std::wstring(fd.cFileName)))
                .str("path", utf8(dir + L"\\" + fd.cFileName))
                .boolean("bundle", (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0).endObj();
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    j.endArr().endObj();
    printf("%s\n", j.text().c_str());
    return 0;
}

int cmdInfo(const std::wstring& path) {
    std::string err;
    LoadTimings t;
    g_stage = "load";
    double t0 = nowMs();
    auto p = Plugin::load(path, err, t);
    Json j;
    if (!p) {
        j.beginObj().boolean("ok", false).str("error", err).str("stage", g_stage).endObj();
        printf("%s\n", j.text().c_str());
        return 2;
    }
    double loadTotal = nowMs() - t0;
    j.beginObj().boolean("ok", true).num("loadTotalMs", loadTotal);
    pluginJson(j, *p, t, true);
    g_stage = "unload";
    double u0 = nowMs();
    p.reset();
    j.num("unloadMs", nowMs() - u0).endObj();
    printf("%s\n", j.text().c_str());
    return 0;
}

int cmdRender(const std::wstring& path, int argc, wchar_t** argv) {
    RenderSettings s;
    std::wstring out, midi, input;
    for (int i = 0; i < argc; ++i) {
        std::wstring a = argv[i];
        auto next = [&]() -> std::wstring { return i + 1 < argc ? std::wstring(argv[++i]) : L""; };
        if (a == L"--out") out = toWinPath(next());
        else if (a == L"--midi") midi = toWinPath(next());
        else if (a == L"--input") { input = next(); if (input != L"builtin") input = toWinPath(input); }
        else if (a == L"--rate") s.sampleRate = _wtof(next().c_str());
        else if (a == L"--block") s.blockSize = _wtoi(next().c_str());
        else if (a == L"--tail") s.tailSec = _wtof(next().c_str());
        else if (a == L"--realtime") s.realtimeMode = true;
        else return usage();
    }
    if (out.empty() || s.blockSize <= 0 || s.sampleRate <= 0) return usage();

    Json j;
    auto fail = [&](int code, const std::string& e) {
        Json f;
        f.beginObj().boolean("ok", false).str("error", e).str("stage", g_stage).endObj();
        printf("%s\n", f.text().c_str());
        return code;
    };
    std::string err;
    if (!midi.empty()) {
        g_stage = "read-midi";
        s.midi = readMidiFile(midi, err);
        if (!err.empty()) return fail(1, "MIDI: " + err);
    }
    if (input == L"builtin") s.input = builtinSignal(s.sampleRate);
    else if (!input.empty()) {
        int rate = 0;
        g_stage = "read-input";
        if (!readWav(input, s.input, rate, err)) return fail(1, "input WAV: " + err);
    }

    LoadTimings t;
    g_stage = "load";
    double t0 = nowMs();
    auto p = Plugin::load(path, err, t);
    if (!p) return fail(2, err);
    double loadTotal = nowMs() - t0;
    // Effects always need audio in; MIDI (if given) is still delivered to their event bus.
    if (!p->isInstrument && s.input.empty()) s.input = builtinSignal(s.sampleRate);

    g_stage = "render";
    RenderStats st;
    if (!p->render(s, st, err)) return fail(3, err);
    g_stage = "write-wav";
    if (!writeWavFloat(out, st.out, (int)s.sampleRate)) return fail(3, "cannot write output WAV");

    bool silent = true;
    for (double pk : st.peakDb) if (pk > -90) silent = false;
    j.beginObj().boolean("ok", true).num("loadTotalMs", loadTotal);
    pluginJson(j, *p, t, false);
    j.str("out", utf8(out)).num("rate", s.sampleRate).num("block", s.blockSize)
        .str("mode", s.realtimeMode ? "realtime" : "offline")
        .num("audioSec", st.audioSec).num("renderMs", st.renderMs)
        .num("realtimeFactor", st.renderMs > 0 ? st.audioSec * 1000.0 / st.renderMs : 0)
        .beginObj("blockMs").num("budget", st.blockBudgetMs).num("p50", st.blockP50).num("p99", st.blockP99)
        .num("max", st.blockMax).endObj()
        .num("notes", st.notes).num("ccMapped", st.ccMapped).num("ccUnmapped", st.ccUnmapped)
        .num("nonFinite", (double)st.nonFinite).boolean("silent", silent);
    j.beginArr("peakDb"); for (double v : st.peakDb) j.num(nullptr, v); j.endArr();
    j.beginArr("rmsDb"); for (double v : st.rmsDb) j.num(nullptr, v); j.endArr();
    g_stage = "unload";
    double u0 = nowMs();
    p.reset();
    j.num("unloadMs", nowMs() - u0).endObj();
    printf("%s\n", j.text().c_str());
    return 0;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    SetUnhandledExceptionFilter(crashFilter);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);  // JUCE and others expect COM on this thread
    if (argc < 3) return usage();
    std::wstring cmd = argv[1], target = toWinPath(argv[2]);
    int rc;
    if (cmd == L"scan") rc = cmdScan(target);
    else if (cmd == L"info") rc = cmdInfo(target);
    else if (cmd == L"render") rc = cmdRender(target, argc - 3, argv + 3);
    else rc = usage();
    fflush(stdout);
    // Plugins sometimes leave threads behind; don't hang on exit.
    ExitProcess((UINT)rc);
}
