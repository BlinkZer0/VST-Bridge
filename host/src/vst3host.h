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

// One MIDI message at a sample offset inside the current block.
struct BlockMidi {
    int32_t offset;
    uint8_t status, d1, d2;
};

// One parameter change at a sample offset inside the current block.
struct BlockParam {
    uint32_t id;
    int32_t offset;
    double value;
};

struct BlockCounters {
    int32_t notes = 0, ccMapped = 0, ccUnmapped = 0;
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

    // Offline render = begin + process per block + end.
    bool render(const RenderSettings& s, RenderStats& st, std::string& err);

    // ---- Streaming API (audio thread: process; begin/end from the main thread)
    bool begin(double sampleRate, int32_t maxBlock, bool realtime, std::string& err);
    // in: inChannels() pointers (or nullptr), out: outChannels() pointers. n <= maxBlock.
    bool process(int32_t n, const BlockMidi* midi, size_t nMidi, const BlockParam* prm, size_t nPrm,
                 const float* const* in, float* const* out, int64_t projectFrame, BlockCounters& c);
    void end();
    int inChannels() const;   // main input bus channels (0 for instruments)
    int outChannels() const;  // main output bus channels

    // ---- Controller / state (main thread)
    double getParam(uint32_t id);
    void setParam(uint32_t id, double normalized);      // controller side only
    std::string paramText(uint32_t id, double normalized);
    bool getState(std::string& component, std::string& controller);
    bool setState(const std::string& component, const std::string& controller, std::string& err);

    struct Impl;
    std::unique_ptr<Impl> impl;
};

// True for parameters a user should see (drops JUCE's "MIDI CC n|m" emulation parameters).
bool isUserParam(const ParamDesc& p);

// Pump the Win32 message queue for roughly `ms` milliseconds (JUCE posts async work there).
void pumpMessages(int ms);

}  // namespace vg
