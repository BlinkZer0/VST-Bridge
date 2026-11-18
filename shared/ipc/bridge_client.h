// SPDX-License-Identifier: GPL-3.0-or-later
//
// Engine side of the bridge protocol (shared/ipc/vg_ipc.h). Portable Linux/Android C++:
// used by the Android audio engine and by tools/ipc_sim (PC test against the real host).
#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>

#include "vg_ipc.h"

namespace vg {

class BridgeClient {
public:
    ~BridgeClient() { close(); }

    // Creates (truncates) the shm file, maps it and initialises the header.
    bool create(const std::string& path, uint32_t sampleRate, uint32_t blockSize, uint32_t targetFill, std::string& err);
    void close();
    vg_ipc* ipc() const { return s_; }

    // ---- audio thread (single consumer), real-time safe
    // Copies up to `frames` rendered frames; zero-fills the rest. Returns frames delivered.
    uint32_t read(float* left, float* right, uint32_t frames);
    // Effect input for the host (engine is the single producer).
    void writeInput(const float* left, const float* right, uint32_t frames);
    void wakeHost();

    // ---- any non-RT thread (serialised internally)
    bool sendMidi(uint8_t status, uint8_t d1, uint8_t d2, uint32_t seq = 0);
    void setTargetFill(uint32_t frames);
    // Blocking control request (JSON response). Returns false on timeout or host failure.
    bool request(const std::string& req, std::string& resp, int timeoutMs);
    void requestStop();
    uint32_t hostState() const;
    bool waitForState(uint32_t state, int timeoutMs);

    // ---- latency probe: note-on now, measure frames until the output crosses `thresholdDb`
    void startProbe(uint8_t note, uint8_t velocity, float thresholdDb);
    // Returns true when finished; frames = consumer frames from the note-on to the onset.
    bool probeResult(int64_t& frames) const;

    // ---- stats (any thread)
    uint64_t underruns() const { return underruns_.load(); }
    uint64_t framesRead() const { return framesRead_.load(); }
    uint32_t fill() const;
    void resetUnderruns() { underruns_ = 0; }

    // ---- ring integrity test: the host writes frame index (mod 2^24) as the sample value
    // (`serve - --pattern`); read() then counts every sample that doesn't match.
    void setVerifyPattern(bool on) { verify_.store(on); mismatches_ = 0; verified_ = 0; }
    uint64_t patternMismatches() const { return mismatches_.load(); }
    uint64_t patternVerified() const { return verified_.load(); }

private:
    vg_ipc* s_ = nullptr;
    size_t size_ = 0;
    int fd_ = -1;
    std::mutex midiMu_, ctrlMu_;
    std::atomic<uint64_t> underruns_{0}, framesRead_{0};
    bool primed_ = false;  // audio thread only
    // probe
    std::atomic<int> probeState_{0};  // 0 idle, 1 armed, 2 done
    std::atomic<uint64_t> probeStart_{0};
    std::atomic<int64_t> probeFrames_{0};
    float probeThreshold_ = 0.01f;
    std::atomic<bool> verify_{false};
    std::atomic<uint64_t> mismatches_{0}, verified_{0};
};

// Linux futex helpers (shared mapping: not FUTEX_PRIVATE).
long futexWake(uint32_t* addr, int n);
long futexWait(uint32_t* addr, uint32_t expected, int64_t timeoutNs);

}  // namespace vg
