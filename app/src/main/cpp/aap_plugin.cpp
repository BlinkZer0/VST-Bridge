// SPDX-License-Identifier: GPL-3.0-or-later
//
// AAP (Audio Plugins For Android) glue: our 8 "bridge slots" as AAP plugins.
//
// AAP hosts bind to org.androidaudioplugin.AudioPluginService declared in our manifest; the AAP
// runtime then loads this library *inside our app process* and calls the functions below. Each
// slot forwards to its bridge (shared memory -> Windows plugin under Wine/Box64):
//   - prepare(): asks Kotlin (SlotManager) to start the slot's host for the assigned VST
//   - process(): MIDI 2.0 UMP from the AAP host -> MIDI 1.0 -> bridge; audio read from the ring
//     (render-ahead, same as the app's own Oboe callback; effects also feed the input ring)
//   - parameters extension: the VST's real parameters, reported at runtime
//   - state extension: the VST's component + controller state
#include <aap/android-audio-plugin.h>
#include <aap/ext/midi.h>
#include <aap/ext/parameters.h>
#include <aap/ext/state.h>
#include <android/log.h>
#include <jni.h>

#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "engine.h"

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "VSTGHOST", __VA_ARGS__)

using namespace vg;

namespace {

JavaVM* g_vm = nullptr;
jclass g_slotManager = nullptr;
jmethodID g_prepare = nullptr, g_release = nullptr;

JNIEnv* jniEnv() {
    if (!g_vm) return nullptr;
    JNIEnv* env = nullptr;
    if (g_vm->GetEnv((void**)&env, JNI_VERSION_1_6) == JNI_EDETACHED) g_vm->AttachCurrentThread(&env, nullptr);
    return env;
}

struct Param {
    uint32_t vstId;
    int steps;
    double def;
    std::string title;
};

struct Ctx {
    int slot = 0;
    bool effect = false;
    int rate = 48000;
    AndroidAudioPluginHost host{};
    std::mutex pmu;
    std::vector<Param> params;
    std::string state;
    // Parameter changes from process() are applied on a worker (control requests block).
    std::mutex qmu;
    std::condition_variable qcv;
    std::vector<std::pair<uint32_t, double>> queue;
    bool quit = false;
    std::thread worker;
    // Diagnostics (process thread only): logged every ~3 s of audio.
    uint64_t calls = 0, midiMsgs = 0, framesSinceLog = 0;
    float peak = 0;
};

int slotFromId(const char* id, bool& effect) {
    // urn:vstghost:slot:inst1..inst4 -> 1..4, urn:vstghost:slot:fx1..fx4 -> 5..8
    std::string s(id ? id : "");
    auto p = s.rfind(':');
    std::string tail = p == std::string::npos ? s : s.substr(p + 1);
    effect = tail.rfind("fx", 0) == 0;
    int n = atoi(tail.c_str() + (effect ? 2 : 4));
    if (n < 1 || n > 4) return -1;
    return effect ? 4 + n : n;
}

void fetchParams(Ctx* c) {
    auto b = slotBridge(c->slot);
    std::string resp;
    if (!b || !b->request("params_tsv", resp, 10000)) return;
    std::vector<Param> ps;
    size_t pos = 0;
    while (pos < resp.size()) {
        size_t e = resp.find('\n', pos);
        if (e == std::string::npos) e = resp.size();
        std::string line = resp.substr(pos, e - pos);
        pos = e + 1;
        Param p{};
        char title[256] = {0};
        double value;
        if (sscanf(line.c_str(), "%u\t%d\t%lf\t%lf\t%255[^\n]", &p.vstId, &p.steps, &p.def, &value, title) >= 4) {
            p.title = title;
            ps.push_back(std::move(p));
        }
    }
    std::lock_guard<std::mutex> g(c->pmu);
    c->params = std::move(ps);
}

void workerLoop(Ctx* c) {
    for (;;) {
        std::vector<std::pair<uint32_t, double>> todo;
        {
            std::unique_lock<std::mutex> l(c->qmu);
            c->qcv.wait(l, [&] { return c->quit || !c->queue.empty(); });
            if (c->quit) return;
            todo.swap(c->queue);
        }
        auto b = slotBridge(c->slot);
        std::string resp;
        for (auto& [index, value] : todo) {
            uint32_t id;
            { std::lock_guard<std::mutex> g(c->pmu); if (index >= c->params.size()) continue; id = c->params[index].vstId; }
            if (b) b->request("set\n" + std::to_string(id) + "\n" + std::to_string(value), resp, 2000);
        }
    }
}

void enqueueParam(Ctx* c, uint32_t index, double value) {
    { std::lock_guard<std::mutex> g(c->qmu); c->queue.emplace_back(index, value); }
    c->qcv.notify_one();
}

// MIDI 2.0 UMP sequence -> MIDI 1.0 messages for the bridge; parameter messages -> worker.
void handleEvents(Ctx* c, BridgeClient& b, aap_buffer_t* buffer) {
    auto* hdr = (AAPMidiBufferHeader*)buffer->get_event_buffer(buffer, AAP_PORT_DIRECTION_INPUT, 0);
    if (!hdr || hdr->length == 0) return;
    const uint32_t* w = (const uint32_t*)(hdr + 1);
    const uint32_t nWords = hdr->length / 4;
    static const uint8_t kWords[16] = {1, 1, 1, 2, 2, 4, 1, 1, 2, 2, 2, 3, 3, 4, 4, 4};
    for (uint32_t i = 0; i < nWords;) {
        const uint32_t w0 = w[i];
        const uint32_t type = w0 >> 28;
        const uint32_t size = kWords[type];
        if (i + size > nWords) break;
        if (type == 0x2 || type == 0x4) ++c->midiMsgs;
        if (type == 0x2) {  // MIDI 1.0 channel voice in UMP
            b.sendMidi((uint8_t)((w0 >> 16) & 0xFF), (uint8_t)((w0 >> 8) & 0x7F), (uint8_t)(w0 & 0x7F));
        } else if (type == 0x4) {  // MIDI 2.0 channel voice
            const uint32_t w1 = w[i + 1];
            const uint8_t op = (w0 >> 20) & 0xF, ch = (w0 >> 16) & 0xF, idx = (w0 >> 8) & 0x7F;
            switch (op) {
            case 0x9: { uint8_t v = (uint8_t)((w1 >> 16) >> 9); b.sendMidi(0x90 | ch, idx, v ? v : 1); break; }
            case 0x8: b.sendMidi(0x80 | ch, idx, (uint8_t)((w1 >> 16) >> 9)); break;
            case 0xA: b.sendMidi(0xA0 | ch, idx, (uint8_t)(w1 >> 25)); break;
            case 0xB: b.sendMidi(0xB0 | ch, idx, (uint8_t)(w1 >> 25)); break;
            case 0xC: b.sendMidi(0xC0 | ch, (uint8_t)((w1 >> 24) & 0x7F), 0); break;
            case 0xD: b.sendMidi(0xD0 | ch, (uint8_t)(w1 >> 25), 0); break;
            case 0xE: { uint32_t v = w1 >> 18; b.sendMidi(0xE0 | ch, (uint8_t)(v & 0x7F), (uint8_t)(v >> 7)); break; }
            case 0x3:  // assignable controller (NRPN): AAP parameter index = bank * 128 + index
                enqueueParam(c, ((w0 >> 8) & 0x7F) * 0x80 + (w0 & 0x7F), aapParameterUint32ToNormalized(w1));
                break;
            default: break;
            }
        } else if (type == 0x5) {  // SysEx8: AAP parameter change
            uint8_t group, channel, key, extra;
            uint16_t index;
            uint32_t value;
            if (aapReadMidi2ParameterSysex8(&group, &channel, &key, &extra, &index, &value, w[i], w[i + 1], w[i + 2], w[i + 3]))
                enqueueParam(c, index, aapParameterUint32ToNormalized(value));
        }
        i += size;
    }
}

// ---------------------------------------------------------------- plugin functions

void prepare(AndroidAudioPlugin* plugin, int sampleRate, aap_buffer_t* buffer) {
    auto* c = (Ctx*)plugin->plugin_specific;
    c->rate = sampleRate;
    const int frames = buffer ? buffer->num_frames(buffer) : 1024;
    JNIEnv* env = jniEnv();
    bool ok = false;
    if (env && g_slotManager && g_prepare) {
        ok = env->CallStaticBooleanMethod(g_slotManager, g_prepare, (jint)c->slot, (jint)sampleRate, (jint)frames);
        if (env->ExceptionCheck()) { env->ExceptionDescribe(); env->ExceptionClear(); ok = false; }
    }
    LOGI("AAP slot %d prepare: rate %d frames %d -> %s", c->slot, sampleRate, frames, ok ? "host running" : "FAILED");
    if (!ok) return;
    fetchParams(c);
    if (c->host.get_extension) {
        auto* ph = (aap_parameters_host_extension_t*)c->host.get_extension(&c->host, AAP_PARAMETERS_EXTENSION_URI);
        if (ph && ph->notify_parameters_changed) ph->notify_parameters_changed(ph, &c->host);
    }
}

void activate(AndroidAudioPlugin*) {}
void deactivate(AndroidAudioPlugin*) {}

void process(AndroidAudioPlugin* plugin, aap_buffer_t* buffer, int32_t frameCount, int64_t) {
    auto* c = (Ctx*)plugin->plugin_specific;
    const int nOut = buffer->get_audio_channel_count(buffer, AAP_PORT_DIRECTION_OUTPUT, 0);
    float** outs = buffer->get_audio_channels(buffer, AAP_PORT_DIRECTION_OUTPUT, 0);
    auto b = slotBridge(c->slot);
    if (!b || b->hostState() != VG_HOST_RUNNING || !outs || nOut <= 0) {
        for (int ch = 0; ch < nOut; ++ch) if (outs && outs[ch]) memset(outs[ch], 0, sizeof(float) * (size_t)frameCount);
        return;
    }
    ensureSlotFill(c->slot, *b, (uint32_t)frameCount, (uint32_t)frameCount);
    handleEvents(c, *b, buffer);
    if (c->effect) {
        const int nIn = buffer->get_audio_channel_count(buffer, AAP_PORT_DIRECTION_INPUT, 0);
        float** ins = buffer->get_audio_channels(buffer, AAP_PORT_DIRECTION_INPUT, 0);
        if (ins && nIn > 0) b->writeInput(ins[0], ins[nIn > 1 ? 1 : 0], (uint32_t)frameCount);
    }
    if (nOut >= 2) {
        b->read(outs[0], outs[1], (uint32_t)frameCount);
    } else {
        static thread_local std::vector<float> scratch;
        scratch.resize((size_t)frameCount);
        b->read(outs[0], scratch.data(), (uint32_t)frameCount);
    }
    for (int ch = 2; ch < nOut; ++ch) memset(outs[ch], 0, sizeof(float) * (size_t)frameCount);
    b->wakeHost();

    ++c->calls;
    for (int32_t i = 0; i < frameCount; ++i) c->peak = std::max(c->peak, std::fabs(outs[0][i]));
    c->framesSinceLog += (uint64_t)frameCount;
    if (c->framesSinceLog >= (uint64_t)c->rate * 3) {
        LOGI("AAP slot %d: %llu process() calls, %llu MIDI msgs, output peak %.1f dBFS, frames/call %d, underruns %llu, target %u",
             c->slot, (unsigned long long)c->calls, (unsigned long long)c->midiMsgs,
             c->peak > 0 ? 20 * std::log10(c->peak) : -999.0, frameCount, (unsigned long long)b->underruns(),
             VG_LOAD(&b->ipc()->targetFill));
        c->framesSinceLog = 0;
        c->peak = 0;
    }
}

// ---- parameters extension
int32_t paramCount(aap_parameters_extension_t*, AndroidAudioPlugin* plugin) {
    auto* c = (Ctx*)plugin->plugin_specific;
    std::lock_guard<std::mutex> g(c->pmu);
    return (int32_t)c->params.size();
}

aap_parameter_info_t paramInfo(aap_parameters_extension_t*, AndroidAudioPlugin* plugin, int32_t index) {
    auto* c = (Ctx*)plugin->plugin_specific;
    aap_parameter_info_t info{};
    std::lock_guard<std::mutex> g(c->pmu);
    if (index < 0 || index >= (int32_t)c->params.size()) return info;
    const Param& p = c->params[(size_t)index];
    info.stable_id = (int16_t)index;
    strncpy(info.display_name, p.title.c_str(), sizeof(info.display_name) - 1);
    info.min_value = 0.0;
    info.max_value = 1.0;
    info.default_value = p.def;
    return info;
}

double paramProperty(aap_parameters_extension_t*, AndroidAudioPlugin* plugin, int32_t id, int32_t prop) {
    auto* c = (Ctx*)plugin->plugin_specific;
    std::lock_guard<std::mutex> g(c->pmu);
    if (id < 0 || id >= (int32_t)c->params.size()) return 0.0;
    const Param& p = c->params[(size_t)id];
    switch (prop) {
    case AAP_PARAMETER_PROPERTY_MIN_VALUE: return 0.0;
    case AAP_PARAMETER_PROPERTY_MAX_VALUE: return 1.0;
    case AAP_PARAMETER_PROPERTY_DEFAULT_VALUE: return p.def;
    case AAP_PARAMETER_PROPERTY_IS_DISCRETE: return p.steps > 0 ? 1.0 : 0.0;
    default: return 0.0;
    }
}

int32_t enumCount(aap_parameters_extension_t*, AndroidAudioPlugin*, int32_t) { return 0; }
aap_parameter_enum_t enumItem(aap_parameters_extension_t*, AndroidAudioPlugin*, int32_t, int32_t) { return aap_parameter_enum_t{}; }

aap_parameters_extension_t g_paramsExt{nullptr, paramCount, paramInfo, paramProperty, enumCount, enumItem};

// ---- state extension: "VSTGHOST-STATE-1\n<component b64>\n<controller b64>\n"
std::string jsonField(const std::string& json, const char* key) {
    std::string k = std::string("\"") + key + "\":\"";
    size_t p = json.find(k);
    if (p == std::string::npos) return {};
    p += k.size();
    size_t e = json.find('"', p);
    return e == std::string::npos ? std::string() : json.substr(p, e - p);
}

size_t stateSize(aap_state_extension_t*, AndroidAudioPlugin* plugin) {
    auto* c = (Ctx*)plugin->plugin_specific;
    auto b = slotBridge(c->slot);
    std::string resp;
    c->state.clear();
    if (b && b->request("get_state", resp, 10000))
        c->state = "VSTGHOST-STATE-1\n" + jsonField(resp, "component") + "\n" + jsonField(resp, "controller") + "\n";
    return c->state.size();
}

void getState(aap_state_extension_t* ext, AndroidAudioPlugin* plugin, aap_state_t* dst) {
    auto* c = (Ctx*)plugin->plugin_specific;
    if (c->state.empty()) stateSize(ext, plugin);
    if (!dst || !dst->data) return;
    const size_t n = std::min(dst->data_size, c->state.size());
    memcpy(dst->data, c->state.data(), n);
    dst->data_size = n;
}

void setState(aap_state_extension_t*, AndroidAudioPlugin* plugin, aap_state_t* src) {
    auto* c = (Ctx*)plugin->plugin_specific;
    if (!src || !src->data) return;
    std::string s((const char*)src->data, src->data_size);
    if (s.rfind("VSTGHOST-STATE-1\n", 0) != 0) return;
    size_t a = s.find('\n') + 1, m = s.find('\n', a), e = s.find('\n', m + 1);
    if (m == std::string::npos) return;
    std::string comp = s.substr(a, m - a), ctrl = e == std::string::npos ? std::string() : s.substr(m + 1, e - m - 1);
    auto b = slotBridge(c->slot);
    std::string resp;
    if (b) b->request("set_state\n" + comp + "\n" + ctrl, resp, 10000);
}

aap_state_extension_t g_stateExt{nullptr, stateSize, getState, setState};

void* getExtension(AndroidAudioPlugin*, const char* uri) {
    if (!strcmp(uri, AAP_PARAMETERS_EXTENSION_URI)) return &g_paramsExt;
    if (!strcmp(uri, AAP_STATE_EXTENSION_URI)) return &g_stateExt;
    return nullptr;
}

// ---- factory
AndroidAudioPlugin* instantiate(AndroidAudioPluginFactory*, const char* pluginId, AndroidAudioPluginHost* host) {
    bool effect = false;
    int slot = slotFromId(pluginId, effect);
    LOGI("AAP instantiate %s -> slot %d", pluginId ? pluginId : "(null)", slot);
    if (slot < 0) return nullptr;
    auto* c = new Ctx;
    c->slot = slot;
    c->effect = effect;
    if (host) c->host = *host;
    c->worker = std::thread(workerLoop, c);
    return new AndroidAudioPlugin{c, prepare, activate, process, deactivate, getExtension, nullptr};
}

void release(AndroidAudioPluginFactory*, AndroidAudioPlugin* instance) {
    auto* c = (Ctx*)instance->plugin_specific;
    { std::lock_guard<std::mutex> g(c->qmu); c->quit = true; }
    c->qcv.notify_one();
    if (c->worker.joinable()) c->worker.join();
    if (JNIEnv* env = jniEnv(); env && g_slotManager && g_release) {
        env->CallStaticVoidMethod(g_slotManager, g_release, (jint)c->slot);
        if (env->ExceptionCheck()) { env->ExceptionDescribe(); env->ExceptionClear(); }
    }
    LOGI("AAP slot %d released", c->slot);
    delete c;
    delete instance;
}

AndroidAudioPluginFactory g_factory{instantiate, release, nullptr};

}  // namespace

extern "C" AAP_PUBLIC_API AndroidAudioPluginFactory* GetAndroidAudioPluginFactory() { return &g_factory; }

// The app's Application loads this library with System.loadLibrary before any service runs, so
// JNI_OnLoad sees the app class loader and can resolve SlotManager for later native callbacks.
extern "C" JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void*) {
    g_vm = vm;
    JNIEnv* env = nullptr;
    if (vm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK) return JNI_VERSION_1_6;
    if (jclass cls = env->FindClass("com/vstghost/SlotManager")) {
        g_slotManager = (jclass)env->NewGlobalRef(cls);
        g_prepare = env->GetStaticMethodID(cls, "prepareForAap", "(III)Z");
        g_release = env->GetStaticMethodID(cls, "releaseFromAap", "(I)V");
    }
    if (env->ExceptionCheck()) env->ExceptionClear();
    return JNI_VERSION_1_6;
}
