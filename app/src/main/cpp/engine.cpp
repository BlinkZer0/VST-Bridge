// SPDX-License-Identifier: GPL-3.0-or-later
//
// Real-time audio engine: an Oboe output stream whose callback mixes the slots whose audio
// goes to the app (see engine.h). Each slot's bridge host renders ahead into shared memory.
//
// The callback never waits on a host: if a ring is short it outputs silence for that slot and
// counts an underrun. MIDI and control go to the hosts through the same shared memory.
#include "engine.h"

#include <android/log.h>
#include <jni.h>
#include <oboe/Oboe.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <ctime>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "VSTGHOST", __VA_ARGS__)

namespace vg {
namespace {

int64_t nowNs() {
    timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (int64_t)t.tv_sec * 1000000000 + t.tv_nsec;
}

// Effect test input: plucked notes + noise hits, looping every 4 s (deterministic).
class TestSignal {
public:
    void prepare(int rate) {
        const int n = rate * 4;
        l_.assign((size_t)n, 0.0f);
        uint32_t seed = 7;
        auto rnd = [&] { seed = seed * 1664525u + 1013904223u; return (float)((seed >> 8) / 8388608.0 - 1.0); };
        const float notes[] = {196.0f, 246.94f, 293.66f, 392.0f};
        for (int k = 0; k < 4; ++k) {
            int start = k * rate, period = (int)((float)rate / notes[k]);
            std::vector<float> line((size_t)period);
            for (auto& v : line) v = rnd() * 0.5f;
            for (int i = 0; i < (int)(0.9f * (float)rate); ++i) {
                size_t a = (size_t)(i % period), b = (size_t)((i + 1) % period);
                float v = line[a];
                line[a] = 0.996f * 0.5f * (line[a] + line[b]);
                l_[(size_t)(start + i)] += v * 0.6f;
            }
            int hit = start + rate / 2;
            for (int i = 0; i < rate / 8; ++i) l_[(size_t)(hit + i)] += rnd() * 0.3f * std::exp(-(float)i / (0.03f * (float)rate));
        }
        pos_ = 0;
    }
    void fill(float* l, float* r, uint32_t n) {
        if (l_.empty()) { memset(l, 0, n * 4); memset(r, 0, n * 4); return; }
        for (uint32_t i = 0; i < n; ++i) { l[i] = r[i] = l_[pos_]; pos_ = (pos_ + 1) % l_.size(); }
    }
private:
    std::vector<float> l_;
    size_t pos_ = 0;
};

struct Slot {
    std::shared_ptr<BridgeClient> bridge;
    std::atomic<bool> aapOwned{false};
    std::atomic<int> inputMode{0};  // 0 silence, 1 test signal (effects in the app)
    // Adaptive fill state (touched only by the slot's current consumer thread).
    uint64_t lastUnderruns = 0;
    uint32_t growth = 0;
};

class Engine : public oboe::AudioStreamDataCallback, public oboe::AudioStreamErrorCallback {
public:
    std::array<Slot, kSlots> slots;

    std::string createBridge(int slot, const std::string& path, uint32_t rate, uint32_t block, uint32_t fill) {
        std::lock_guard<std::mutex> g(mu_);
        auto b = std::make_shared<BridgeClient>();
        std::string err;
        if (!b->create(path, rate ? rate : (uint32_t)sampleRate_, block, fill, err)) return err;
        slots[slot].lastUnderruns = 0;
        slots[slot].growth = 0;
        std::atomic_store(&slots[slot].bridge, b);
        return {};
    }
    void closeBridge(int slot) {
        std::lock_guard<std::mutex> g(mu_);
        auto b = std::atomic_load(&slots[slot].bridge);
        if (b) b->requestStop();
        std::atomic_store(&slots[slot].bridge, std::shared_ptr<BridgeClient>());
    }

    bool start() {
        std::lock_guard<std::mutex> g(mu_);
        if (stream_) return true;
        oboe::AudioStreamBuilder b;
        b.setDirection(oboe::Direction::Output)
            ->setPerformanceMode(oboe::PerformanceMode::LowLatency)
            ->setSharingMode(oboe::SharingMode::Exclusive)
            ->setFormat(oboe::AudioFormat::Float)
            ->setChannelCount(2)
            ->setSampleRate(48000)
            ->setSampleRateConversionQuality(oboe::SampleRateConversionQuality::Medium)
            ->setUsage(oboe::Usage::Game)
            ->setDataCallback(this)
            ->setErrorCallback(this);
        oboe::Result r = b.openStream(stream_);
        if (r != oboe::Result::OK) { LOGI("openStream failed: %s", oboe::convertToText(r)); stream_.reset(); return false; }
        sampleRate_ = stream_->getSampleRate();
        stream_->setBufferSizeInFrames(stream_->getFramesPerBurst() * bufferBursts_);
        test_.prepare(sampleRate_);
        r = stream_->requestStart();
        LOGI("stream: rate %d burst %d buffer %d/%d exclusive %d lowlatency %d api %s", sampleRate_,
             stream_->getFramesPerBurst(), stream_->getBufferSizeInFrames(), stream_->getBufferCapacityInFrames(),
             stream_->getSharingMode() == oboe::SharingMode::Exclusive,
             stream_->getPerformanceMode() == oboe::PerformanceMode::LowLatency,
             stream_->getAudioApi() == oboe::AudioApi::AAudio ? "AAudio" : "OpenSLES");
        return r == oboe::Result::OK;
    }
    void stop() {
        std::lock_guard<std::mutex> g(mu_);
        if (stream_) { stream_->stop(); stream_->close(); stream_.reset(); }
    }
    void setBufferBursts(int n) {
        std::lock_guard<std::mutex> g(mu_);
        bufferBursts_ = std::max(1, n);
        if (stream_) stream_->setBufferSizeInFrames(stream_->getFramesPerBurst() * bufferBursts_);
    }

    oboe::DataCallbackResult onAudioReady(oboe::AudioStream* s, void* data, int32_t frames) override {
        const int64_t t0 = nowNs();
        float* out = static_cast<float*>(data);
        const uint32_t n = (uint32_t)std::min<int32_t>(frames, kMaxFrames);
        memset(mixL_, 0, n * 4); memset(mixR_, 0, n * 4);
        const uint32_t routeBuf = (uint32_t)std::max<int32_t>(s->getBufferSizeInFrames(), frames);
        bool testFilled = false;
        for (int i = 0; i < kSlots; ++i) {
            Slot& sl = slots[i];
            if (sl.aapOwned.load(std::memory_order_relaxed)) continue;
            auto b = std::atomic_load(&sl.bridge);
            if (!b || b->hostState() != VG_HOST_RUNNING) continue;
            ensureSlotFill(i, *b, n, routeBuf);
            if (sl.inputMode.load(std::memory_order_relaxed) == 1) {
                if (!testFilled) { test_.fill(inL_, inR_, n); testFilled = true; }
                b->writeInput(inL_, inR_, n);
            }
            b->read(l_, r_, n);
            b->wakeHost();
            for (uint32_t k = 0; k < n; ++k) { mixL_[k] += l_[k]; mixR_[k] += r_[k]; }
        }
        const float g = gain_.load(std::memory_order_relaxed);
        for (uint32_t i = 0; i < n; ++i) { out[2 * i] = mixL_[i] * g; out[2 * i + 1] = mixR_[i] * g; }
        for (int32_t i = (int32_t)n; i < frames; ++i) out[2 * i] = out[2 * i + 1] = 0;
        const int64_t dt = nowNs() - t0;
        cbCount_.fetch_add(1, std::memory_order_relaxed);
        cbNsTotal_.fetch_add((uint64_t)dt, std::memory_order_relaxed);
        if ((uint64_t)dt > cbNsMax_.load(std::memory_order_relaxed)) cbNsMax_.store((uint64_t)dt, std::memory_order_relaxed);
        lastFrames_.store(frames, std::memory_order_relaxed);
        return oboe::DataCallbackResult::Continue;
    }

    void onErrorAfterClose(oboe::AudioStream*, oboe::Result err) override {
        LOGI("stream error %s; reopening", oboe::convertToText(err));
        { std::lock_guard<std::mutex> g(mu_); stream_.reset(); }
        start();
        restarts_.fetch_add(1);
    }

    std::vector<double> stats(int slot) {
        std::lock_guard<std::mutex> g(mu_);
        std::vector<double> v(26, 0.0);
        v[0] = sampleRate_;
        if (stream_) {
            v[1] = stream_->getFramesPerBurst();
            v[2] = stream_->getBufferSizeInFrames();
            v[3] = stream_->getBufferCapacityInFrames();
            auto lat = stream_->calculateLatencyMillis();
            v[4] = lat ? lat.value() : -1;
            auto x = stream_->getXRunCount();
            v[5] = x ? x.value() : -1;
            v[14] = stream_->getSharingMode() == oboe::SharingMode::Exclusive;
            v[15] = stream_->getPerformanceMode() == oboe::PerformanceMode::LowLatency;
            v[16] = stream_->getAudioApi() == oboe::AudioApi::AAudio;
        }
        auto b = std::atomic_load(&slots[slot].bridge);
        if (b && b->ipc()) {
            vg_ipc* s = b->ipc();
            v[6] = (double)b->underruns();
            v[7] = b->fill();
            v[8] = (double)VG_LOAD(&s->blocksRendered);
            v[9] = v[8] > 0 ? VG_LOAD(&s->renderNsTotal) / 1e3 / v[8] : 0;
            v[10] = VG_LOAD(&s->maxBlockNs) / 1e3;
            v[17] = s->blockSize;
            v[18] = VG_LOAD(&s->targetFill);
            v[19] = VG_LOAD(&s->wakeMode);
            v[20] = VG_LOAD(&s->hostPid);
            v[21] = (double)b->patternMismatches();
            v[22] = (double)b->patternVerified();
            v[23] = VG_LOAD(&s->hostState);
        }
        const double cnt = (double)cbCount_.load();
        v[11] = cnt > 0 ? cbNsTotal_.load() / 1e3 / cnt : 0;
        v[12] = cbNsMax_.load() / 1e3;
        v[13] = cnt;
        v[24] = lastFrames_.load();
        v[25] = restarts_.load();
        return v;
    }
    void resetStats(int slot) {
        cbCount_ = 0; cbNsTotal_ = 0; cbNsMax_ = 0;
        auto b = std::atomic_load(&slots[slot].bridge);
        if (b) { b->resetUnderruns(); if (b->ipc()) VG_STORE(&b->ipc()->maxBlockNs, 0u); }
    }

    std::atomic<float> gain_{1.0f};

private:
    static constexpr int kMaxFrames = 8192;
    std::mutex mu_;
    std::shared_ptr<oboe::AudioStream> stream_;
    int sampleRate_ = 48000, bufferBursts_ = 2;
    float l_[kMaxFrames], r_[kMaxFrames], inL_[kMaxFrames], inR_[kMaxFrames], mixL_[kMaxFrames], mixR_[kMaxFrames];
    TestSignal test_;
    std::atomic<uint64_t> cbCount_{0}, cbNsTotal_{0}, cbNsMax_{0}, restarts_{0};
    std::atomic<int32_t> lastFrames_{0};
};

Engine& engine() {
    static Engine* e = new Engine;
    return *e;
}

std::string jstr(JNIEnv* env, jstring s) {
    const char* c = env->GetStringUTFChars(s, nullptr);
    std::string r(c);
    env->ReleaseStringUTFChars(s, c);
    return r;
}

bool okSlot(int s) { return s >= 0 && s < kSlots; }

}  // namespace

std::shared_ptr<BridgeClient> slotBridge(int slot) {
    return okSlot(slot) ? std::atomic_load(&engine().slots[slot].bridge) : nullptr;
}

void setSlotAapOwned(int slot, bool owned) {
    if (okSlot(slot)) engine().slots[slot].aapOwned = owned;
}

void ensureSlotFill(int slot, BridgeClient& b, uint32_t frames, uint32_t routeBufferFrames) {
    // The host must keep at least one full consumer buffer (+ one block) rendered ahead, or every
    // read runs short. Bluetooth (A2DP) often reads back to back, hence the whole route buffer.
    // Every underrun after priming grows the target by one block (self-healing, capped).
    Slot& sl = engine().slots[slot];
    vg_ipc* ipc = b.ipc();
    const uint32_t B = ipc->blockSize;
    const uint32_t buf = std::max(frames, routeBufferFrames);
    uint32_t need = (buf + B - 1) / B * B + B;
    const uint64_t ur = b.underruns();
    if (ur < sl.lastUnderruns) sl.lastUnderruns = ur;  // stats were reset
    if (ur > sl.lastUnderruns) { sl.growth += B * (uint32_t)(ur - sl.lastUnderruns); sl.lastUnderruns = ur; }
    need += std::min<uint32_t>(sl.growth, 4096);
    if (need > VG_LOAD(&ipc->targetFill)) b.setTargetFill(need);
}

}  // namespace vg

using namespace vg;

#define JNIFN(ret, name) extern "C" JNIEXPORT ret JNICALL Java_com_vstghost_NativeEngine_##name

JNIFN(jboolean, startAudio)(JNIEnv*, jclass) { return engine().start(); }
JNIFN(void, stopAudio)(JNIEnv*, jclass) { engine().stop(); }
JNIFN(void, setBufferBursts)(JNIEnv*, jclass, jint n) { engine().setBufferBursts(n); }

JNIFN(jstring, createBridge)(JNIEnv* env, jclass, jint slot, jstring path, jint rate, jint block, jint fill) {
    if (!okSlot(slot)) return env->NewStringUTF("bad slot");
    std::string err = engine().createBridge(slot, jstr(env, path), (uint32_t)rate, (uint32_t)block, (uint32_t)fill);
    return err.empty() ? nullptr : env->NewStringUTF(err.c_str());
}
JNIFN(void, closeBridge)(JNIEnv*, jclass, jint slot) { if (okSlot(slot)) engine().closeBridge(slot); }

JNIFN(jint, hostState)(JNIEnv*, jclass, jint slot) {
    auto b = slotBridge(slot);
    return b ? (jint)b->hostState() : (jint)VG_HOST_EXITED;
}

JNIFN(jstring, lastResponse)(JNIEnv* env, jclass, jint slot) {
    auto b = slotBridge(slot);
    if (!b || !b->ipc()) return nullptr;
    std::string s(b->ipc()->resp, strnlen(b->ipc()->resp, 4096));
    return env->NewStringUTF(s.c_str());
}

JNIFN(jstring, request)(JNIEnv* env, jclass, jint slot, jstring req, jint timeoutMs) {
    auto b = slotBridge(slot);
    std::string resp;
    if (!b || !b->request(jstr(env, req), resp, timeoutMs)) return nullptr;
    return env->NewStringUTF(resp.c_str());
}

JNIFN(jboolean, sendMidi)(JNIEnv*, jclass, jint slot, jint st, jint d1, jint d2) {
    auto b = slotBridge(slot);
    return b && b->sendMidi((uint8_t)st, (uint8_t)d1, (uint8_t)d2);
}

JNIFN(void, setTargetFill)(JNIEnv*, jclass, jint slot, jint frames) {
    if (auto b = slotBridge(slot)) b->setTargetFill((uint32_t)frames);
}

JNIFN(void, setInputMode)(JNIEnv*, jclass, jint slot, jint mode) { if (okSlot(slot)) engine().slots[slot].inputMode = mode; }
JNIFN(void, setGain)(JNIEnv*, jclass, jfloat g) { engine().gain_ = g; }
JNIFN(void, setAapOwned)(JNIEnv*, jclass, jint slot, jboolean owned) { setSlotAapOwned(slot, owned); }

JNIFN(void, startProbe)(JNIEnv*, jclass, jint slot, jint note, jint vel, jfloat thresholdDb) {
    if (auto b = slotBridge(slot)) b->startProbe((uint8_t)note, (uint8_t)vel, thresholdDb);
}
JNIFN(jlong, probeResult)(JNIEnv*, jclass, jint slot) {
    auto b = slotBridge(slot);
    int64_t f;
    return b && b->probeResult(f) ? (jlong)f : (jlong)-1;
}

JNIFN(void, setVerifyPattern)(JNIEnv*, jclass, jint slot, jboolean on) {
    if (auto b = slotBridge(slot)) b->setVerifyPattern(on);
}

JNIFN(jdoubleArray, stats)(JNIEnv* env, jclass, jint slot) {
    auto v = engine().stats(okSlot(slot) ? slot : 0);
    jdoubleArray a = env->NewDoubleArray((jsize)v.size());
    env->SetDoubleArrayRegion(a, 0, (jsize)v.size(), v.data());
    return a;
}
JNIFN(void, resetStats)(JNIEnv*, jclass, jint slot) { engine().resetStats(okSlot(slot) ? slot : 0); }
