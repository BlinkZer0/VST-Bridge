// SPDX-License-Identifier: GPL-3.0-or-later
// Minimal VST3 host: load a .vst3 module, inspect it, and render audio offline.
// Uses only Steinberg's pluginterfaces headers (MIT); no SDK hosting helpers.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "midifile.h"

namespace vg {

struct ParamDesc {
    uint32_t id;
    std::string title, units, defaultText;
    int32_t stepCount;
    double defaultNormalized;
    int32_t flags;
};

struct BusDesc {
    std::string name;
    int32_t channels;
    bool main;
    bool defaultActive;
};

struct LoadTimings {
    double loadLibraryMs = 0, factoryMs = 0, componentMs = 0, controllerMs = 0, setupMs = 0;
};

struct RenderSettings {
    double sampleRate = 48000;
    int32_t blockSize = 256;
    double tailSec = 3;
    bool realtimeMode = false;  // VST3 kRealtime instead of kOffline
    std::vector<MidiEvent> midi;
    std::vector<std::vector<float>> input;  // effect input, per channel
};

struct RenderStats {
    std::vector<std::vector<float>> out;
    double audioSec = 0, renderMs = 0;
    double blockP50 = 0, blockP99 = 0, blockMax = 0, blockBudgetMs = 0;
    int64_t nonFinite = 0;
    int32_t ccMapped = 0, ccUnmapped = 0, notes = 0;
    std::vector<double> peakDb, rmsDb;
};

class Plugin {
public:
    // path: a .vst3 bundle directory or a single-file .vst3 DLL (Windows path).
    static std::unique_ptr<Plugin> load(const std::wstring& path, std::string& err, LoadTimings& t);
    ~Plugin();

    std::string dllPath, name, vendor, version, sdkVersion, subCategories, cid;
    bool isInstrument = false, separateController = false, hasMidiMapping = false;
    std::vector<BusDesc> audioIn, audioOut, eventIn;
    std::vector<ParamDesc> params;

    // Set up processing, render, and tear processing down again.
    bool render(const RenderSettings& s, RenderStats& st, std::string& err);

    struct Impl;
    std::unique_ptr<Impl> impl;
};

// Pump the Win32 message queue for roughly `ms` milliseconds (JUCE posts async work there).
void pumpMessages(int ms);

}  // namespace vg
