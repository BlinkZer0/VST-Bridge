// SPDX-License-Identifier: GPL-3.0-or-later
//
// Native (Android-built) plugins in a slot: a library exporting the vgn interface (native/vgn/vgn.h)
// runs inside the app process, called straight from the audio callback. Control requests use the
// same text protocol as the Wine host (host/src/serve.cpp), so the rest of the app treats both alike.
#pragma once

#include <atomic>
#include <memory>
#include <string>

#include "vgn/vgn.h"

namespace vg {

struct VgnApi;

class NativeInstance {
public:
    ~NativeInstance();
    // Main thread. rate/maxBlock: the stream's. Returns null + err on failure.
    static std::shared_ptr<NativeInstance> open(const std::string& lib, double rate, int maxBlock, std::string& err);

    // Audio thread: renders n frames (MIDI queued so far goes in at offset 0). in: 2 channels or null.
    void process(const float* inL, const float* inR, float* outL, float* outR, uint32_t n);
    // Any thread (single producer per slot is not required: a spinlock-free MPSC would be overkill,
    // MIDI arrives from one thread at a time in practice; the queue tolerates rare overwrite).
    bool queueMidi(uint8_t status, uint8_t d1, uint8_t d2);
    // Same requests as the Wine host's `serve` (ping, info, params, params_tsv, set, get_state, set_state).
    std::string request(const std::string& req);

    bool instrument() const { return instrument_; }
    vgn_plugin* plugin() const { return p_; }
    const VgnApi* api() const { return api_; }
    double rate() const { return rate_; }

    std::atomic<uint64_t> renderNsTotal{0}, blocks{0}, maxBlockNs{0}, midiConsumed{0};

private:
    const VgnApi* api_ = nullptr;
    vgn_plugin* p_ = nullptr;
    bool instrument_ = true;
    double rate_ = 48000;
    int maxBlock_ = 1024;
    std::string name_, vendor_, version_;
    static constexpr uint32_t kQ = 1024;
    vgn_midi q_[kQ]{};
    std::atomic<uint32_t> qw_{0}, qr_{0};
    vgn_midi scratch_[kQ]{};
};

// Loads lib (already System.loadLibrary'd by Java, so JNI_OnLoad ran) and calls vgn_init on the
// calling (main) thread. Returns vgn_info_json or "ERROR: ...".
std::string nativeInit(JNIEnv* env, jobject context, const std::string& lib);
// vgn_info_json of an initialised library, or "".
std::string nativeInfo(const std::string& lib);

struct VgnApi {
    void* handle = nullptr;
    decltype(&vgn_info_json) info = nullptr;
    decltype(&vgn_init) init = nullptr;
    decltype(&vgn_create) create = nullptr;
    decltype(&vgn_destroy) destroy = nullptr;
    decltype(&vgn_prepare) prepare = nullptr;
    decltype(&vgn_process) process = nullptr;
    decltype(&vgn_param_count) paramCount = nullptr;
    decltype(&vgn_param_info) paramInfo = nullptr;
    decltype(&vgn_param_get) paramGet = nullptr;
    decltype(&vgn_param_set) paramSet = nullptr;
    decltype(&vgn_param_text) paramText = nullptr;
    decltype(&vgn_get_state) getState = nullptr;
    decltype(&vgn_set_state) setState = nullptr;
    decltype(&vgn_free) free = nullptr;
    decltype(&vgn_editor_open) editorOpen = nullptr;
    decltype(&vgn_editor_close) editorClose = nullptr;
};

}  // namespace vg
