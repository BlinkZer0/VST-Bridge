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
#include <unistd.h>
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
    std::shared_ptr<NativeInstance> native;  // Phase 5: an in-process plugin instead of a bridge
    std::atomic<bool> aapOwned{false};
    std::atomic<int> inputMode{0};  // effects: 0 the FX chain (see onAudioReady), 1 the test signal
    std::atomic<int> kind{0};       // 0 instrument, 1 effect (bridged plugins: set by Bridge.load)
    std::atomic<float> peak{0.0f};  // output peak since the last stats reset (app-mixed slots)
    std::atomic<float> gain{1.0f};  // mixer fader (linear), applied to the slot's output
    std::atomic<float> meter{0.0f}; // output peak since the mixer last read it (after the fader)
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

    // Routing: every app-mixed instrument slot plays onto one bus; the app-mixed effect slots form
    // a serial chain, in slot order, fed by that bus (or by the built-in test signal:
    // fxSource_ = 1, or per effect: inputMode = 1); the chain's end goes to the output.
    //   instruments ──Σ──▶ FX a ──▶ FX b ──▶ ... ──▶ output
    // A slot is an instrument or an effect by its `kind` (native plugins know it themselves).
    // Bridged effects run in their own process a few blocks behind their input; the chain tolerates
    // that (it is the same as before, just fed by instruments instead of silence).
    oboe::DataCallbackResult onAudioReady(oboe::AudioStream* s, void* data, int32_t frames) override {
        const int64_t t0 = nowNs();
        float* out = static_cast<float*>(data);
        const uint32_t n = (uint32_t)std::min<int32_t>(frames, kMaxFrames);
        memset(mixL_, 0, n * 4); memset(mixR_, 0, n * 4);
        const uint32_t routeBuf = (uint32_t)std::max<int32_t>(s->getBufferSizeInFrames(), frames);
        bool testFilled = false;
        auto testSignal = [&] { if (!testFilled) { test_.fill(inL_, inR_, n); testFilled = true; } };
        auto isEffect = [&](Slot& sl, const std::shared_ptr<NativeInstance>& nat) {
            return nat ? !nat->instrument() : sl.kind.load(std::memory_order_relaxed) == 1;
        };
        // The slot's fader, then its peaks: `peak` for the self-test, `meter` for the mixer.
        auto addPeak = [&](Slot& sl) {
            const float g = sl.gain.load(std::memory_order_relaxed);
            float pk = 0.0f;
            for (uint32_t k = 0; k < n; ++k) {
                l_[k] *= g; r_[k] *= g;
                pk = std::max(pk, std::max(std::fabs(l_[k]), std::fabs(r_[k])));
            }
            sl.peak.store(std::max(sl.peak.load(std::memory_order_relaxed), pk), std::memory_order_relaxed);
            sl.meter.store(std::max(sl.meter.load(std::memory_order_relaxed), pk), std::memory_order_relaxed);
        };
        // Pass 1: instruments onto the bus (mixL_/mixR_).
        for (int i = 0; i < kSlots; ++i) {
            Slot& sl = slots[i];
            if (sl.aapOwned.load(std::memory_order_relaxed)) continue;
            auto nat = std::atomic_load(&sl.native);
            if (isEffect(sl, nat)) continue;
            if (nat) {
                nat->process(nullptr, nullptr, l_, r_, n);
            } else {
                auto b = std::atomic_load(&sl.bridge);
                if (!b || b->hostState() != VG_HOST_RUNNING) continue;
                ensureSlotFill(i, *b, n, routeBuf);
                b->read(l_, r_, n);
                b->wakeHost();
            }
            addPeak(sl);
            for (uint32_t k = 0; k < n; ++k) { mixL_[k] += l_[k]; mixR_[k] += r_[k]; }
        }
        // Pass 2: the effect chain. chainL_/chainR_ carry the signal from stage to stage.
        if (fxSource_.load(std::memory_order_relaxed) == 1) { testSignal(); memcpy(chainL_, inL_, n * 4); memcpy(chainR_, inR_, n * 4); }
        else { memcpy(chainL_, mixL_, n * 4); memcpy(chainR_, mixR_, n * 4); }
        for (int i = 0; i < kSlots; ++i) {
            Slot& sl = slots[i];
            if (sl.aapOwned.load(std::memory_order_relaxed)) continue;
            auto nat = std::atomic_load(&sl.native);
            if (!isEffect(sl, nat)) continue;
            const float* inL = chainL_;
            const float* inR = chainR_;
            if (sl.inputMode.load(std::memory_order_relaxed) == 1) { testSignal(); inL = inL_; inR = inR_; }
            if (nat) {
                nat->process(inL, inR, l_, r_, n);
            } else {
                auto b = std::atomic_load(&sl.bridge);
                if (!b || b->hostState() != VG_HOST_RUNNING) continue;
                ensureSlotFill(i, *b, n, routeBuf);
                b->writeInput(inL, inR, n);
                b->read(l_, r_, n);
                b->wakeHost();
            }
            addPeak(sl);
            memcpy(chainL_, l_, n * 4); memcpy(chainR_, r_, n * 4);
        }
        const float g = gain_.load(std::memory_order_relaxed);
        for (uint32_t i = 0; i < n; ++i) { out[2 * i] = chainL_[i] * g; out[2 * i + 1] = chainR_[i] * g; }
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
        std::vector<double> v(28, 0.0);
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
            v[26] = (double)VG_LOAD(&s->midiRead);  // MIDI messages the host has consumed
        }
        if (auto nat = std::atomic_load(&slots[slot].native)) {
            const double blocks = (double)nat->blocks.load();
            v[8] = blocks;
            v[9] = blocks > 0 ? nat->renderNsTotal.load() / 1e3 / blocks : 0;
            v[10] = nat->maxBlockNs.load() / 1e3;
            v[23] = VG_HOST_RUNNING;
            v[26] = (double)nat->midiConsumed.load();
        }
        v[27] = slots[slot].peak.load();
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
        slots[slot].peak = 0.0f;
        auto b = std::atomic_load(&slots[slot].bridge);
        if (b) { b->resetUnderruns(); if (b->ipc()) VG_STORE(&b->ipc()->maxBlockNs, 0u); }
        if (auto nat = std::atomic_load(&slots[slot].native)) nat->maxBlockNs = 0;
    }

    std::atomic<float> gain_{1.0f};
    int sampleRate() const { return sampleRate_; }

private:
    static constexpr int kMaxFrames = 8192;
    std::mutex mu_;
    std::shared_ptr<oboe::AudioStream> stream_;
    int sampleRate_ = 48000, bufferBursts_ = 2;
    float zero_[kMaxFrames] = {};
    float l_[kMaxFrames], r_[kMaxFrames], inL_[kMaxFrames], inR_[kMaxFrames], mixL_[kMaxFrames], mixR_[kMaxFrames];
    float chainL_[kMaxFrames], chainR_[kMaxFrames];
public:
    std::atomic<int> fxSource_{0};  // what feeds the effect chain: 0 the instruments, 1 the test signal
private:
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

std::shared_ptr<NativeInstance> slotNative(int slot) {
    return okSlot(slot) ? std::atomic_load(&engine().slots[slot].native) : nullptr;
}

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
    if (slotNative(slot)) return (jint)VG_HOST_RUNNING;
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
    if (auto nat = slotNative(slot)) return env->NewStringUTF(nat->request(jstr(env, req)).c_str());
    auto b = slotBridge(slot);
    std::string resp;
    if (!b || !b->request(jstr(env, req), resp, timeoutMs)) return nullptr;
    return env->NewStringUTF(resp.c_str());
}

JNIFN(jboolean, sendMidi)(JNIEnv*, jclass, jint slot, jint st, jint d1, jint d2) {
    if (auto nat = slotNative(slot)) return nat->queueMidi((uint8_t)st, (uint8_t)d1, (uint8_t)d2);
    auto b = slotBridge(slot);
    return b && b->sendMidi((uint8_t)st, (uint8_t)d1, (uint8_t)d2);
}

JNIFN(void, setTargetFill)(JNIEnv*, jclass, jint slot, jint frames) {
    if (auto b = slotBridge(slot)) b->setTargetFill((uint32_t)frames);
}

JNIFN(void, setInputMode)(JNIEnv*, jclass, jint slot, jint mode) { if (okSlot(slot)) engine().slots[slot].inputMode = mode; }
JNIFN(void, setSlotKind)(JNIEnv*, jclass, jint slot, jint kind) { if (okSlot(slot)) engine().slots[slot].kind = kind; }
JNIFN(void, setFxInput)(JNIEnv*, jclass, jint mode) { engine().fxSource_ = mode; }
JNIFN(void, setGain)(JNIEnv*, jclass, jfloat g) { engine().gain_ = g; }
JNIFN(void, setSlotGain)(JNIEnv*, jclass, jint slot, jfloat g) { if (okSlot(slot)) engine().slots[slot].gain = g; }
// The slot's output peak since the previous call (the mixer polls this at its frame rate).
JNIFN(jfloat, slotMeter)(JNIEnv*, jclass, jint slot) { return okSlot(slot) ? engine().slots[slot].meter.exchange(0.0f) : 0.0f; }
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

// ---- Phase 5: native plugins (see native_host.h). All on the main thread.

// Initialises a native plugin library (already System.loadLibrary'd). Returns info JSON or "ERROR: ...".
JNIFN(jstring, nativeInit)(JNIEnv* env, jclass, jstring lib, jobject context) {
    return env->NewStringUTF(nativeInit(env, context, jstr(env, lib)).c_str());
}

// Puts a new instance of the library's plugin into the slot. rate 0 = the output stream's rate.
JNIFN(jstring, nativeOpen)(JNIEnv* env, jclass, jint slot, jstring lib, jint rate, jint maxBlock) {
    if (!okSlot(slot)) return env->NewStringUTF("bad slot");
    std::string err;
    auto inst = NativeInstance::open(jstr(env, lib), rate > 0 ? rate : engine().sampleRate(), maxBlock, err);
    if (!inst) return env->NewStringUTF(err.c_str());
    engine().closeBridge(slot);
    std::atomic_store(&engine().slots[slot].native, inst);
    return nullptr;
}

// Takes the slot's native plugin out of the audio path and destroys it here (the main thread).
JNIFN(void, nativeClose)(JNIEnv*, jclass, jint slot) {
    if (!okSlot(slot)) return;
    auto inst = std::atomic_exchange(&engine().slots[slot].native, std::shared_ptr<NativeInstance>());
    if (!inst) return;
    for (int i = 0; i < 2000 && inst.use_count() > 1; ++i) usleep(500);  // audio callback done with it
    if (inst.use_count() > 1) {
        LOGI("native slot %d still in use; leaking it", slot);
        new std::shared_ptr<NativeInstance>(inst);
        return;
    }
    inst.reset();
}

JNIFN(jboolean, isNative)(JNIEnv*, jclass, jint slot) { return slotNative(slot) != nullptr; }

// Attaches the plugin's own editor into viewGroup, fitted to w x h. Returns [w, h] or null.
JNIFN(jintArray, nativeEditorOpen)(JNIEnv* env, jclass, jint slot, jobject viewGroup, jint w, jint h) {
    auto nat = slotNative(slot);
    if (!nat) return nullptr;
    int32_t ew = 0, eh = 0;
    if (!nat->api()->editorOpen(nat->plugin(), env, viewGroup, w, h, &ew, &eh)) return nullptr;
    jint r[2] = {ew, eh};
    jintArray a = env->NewIntArray(2);
    env->SetIntArrayRegion(a, 0, 2, r);
    return a;
}

JNIFN(void, nativeEditorClose)(JNIEnv*, jclass, jint slot) {
    if (auto nat = slotNative(slot)) nat->api()->editorClose(nat->plugin());
}
