// SPDX-License-Identifier: LicenseRef-VstGhost-Proprietary
#include "native_host.h"

#include <android/log.h>
#include <dlfcn.h>
#include <time.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <vector>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "VSTGHOST", __VA_ARGS__)

namespace vg {
namespace {

std::mutex g_libsMu;
std::map<std::string, VgnApi> g_libs;  // by library file name, e.g. "libvgn_epianovst.so"
std::atomic_flag g_midiLock = ATOMIC_FLAG_INIT;

int64_t nowNs() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

template <class F>
bool sym(void* h, const char* name, F& out) {
    out = reinterpret_cast<F>(dlsym(h, name));
    return out != nullptr;
}

std::string jsonEsc(const std::string& s) {
    std::string o;
    for (char c : s) {
        switch (c) {
        case '"': o += "\\\""; break;
        case '\\': o += "\\\\"; break;
        case '\n': o += "\\n"; break;
        case '\t': o += "\\t"; break;
        default:
            if ((unsigned char)c < 0x20) { char b[8]; snprintf(b, sizeof b, "\\u%04x", c); o += b; }
            else o += c;
        }
    }
    return o;
}

std::string jsonField(const std::string& json, const char* key) {  // tiny: string fields of vgn_info_json
    const std::string k = std::string("\"") + key + "\"";
    size_t p = json.find(k);
    if (p == std::string::npos) return {};
    p = json.find(':', p);
    if (p == std::string::npos) return {};
    ++p;
    while (p < json.size() && json[p] == ' ') ++p;
    if (p < json.size() && json[p] == '"') {
        size_t e = json.find('"', p + 1);
        return json.substr(p + 1, e - p - 1);
    }
    size_t e = json.find_first_of(",}\n", p);
    return json.substr(p, e - p);
}

const char* kB64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string b64enc(const uint8_t* d, size_t n) {
    std::string out;
    out.reserve((n + 2) / 3 * 4);
    size_t i = 0;
    for (; i + 2 < n; i += 3) {
        uint32_t v = d[i] << 16 | d[i + 1] << 8 | d[i + 2];
        out += kB64[v >> 18]; out += kB64[(v >> 12) & 63]; out += kB64[(v >> 6) & 63]; out += kB64[v & 63];
    }
    if (i + 1 == n) {
        uint32_t v = d[i] << 16;
        out += kB64[v >> 18]; out += kB64[(v >> 12) & 63]; out += "==";
    } else if (i + 2 == n) {
        uint32_t v = d[i] << 16 | d[i + 1] << 8;
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

}  // namespace

std::string nativeInit(JNIEnv* env, jobject context, const std::string& lib) {
    std::lock_guard<std::mutex> lk(g_libsMu);
    auto it = g_libs.find(lib);
    if (it != g_libs.end()) return it->second.info();
    void* h = dlopen(lib.c_str(), RTLD_NOW | RTLD_NOLOAD);
    if (!h) h = dlopen(lib.c_str(), RTLD_NOW);
    if (!h) return std::string("ERROR: dlopen: ") + dlerror();
    VgnApi a;
    a.handle = h;
    int (*apiVersion)() = nullptr;
    const bool ok = sym(h, "vgn_api_version", apiVersion) && sym(h, "vgn_info_json", a.info) && sym(h, "vgn_init", a.init) &&
                    sym(h, "vgn_create", a.create) && sym(h, "vgn_destroy", a.destroy) && sym(h, "vgn_prepare", a.prepare) &&
                    sym(h, "vgn_process", a.process) && sym(h, "vgn_param_count", a.paramCount) &&
                    sym(h, "vgn_param_info", a.paramInfo) && sym(h, "vgn_param_get", a.paramGet) &&
                    sym(h, "vgn_param_set", a.paramSet) && sym(h, "vgn_param_text", a.paramText) &&
                    sym(h, "vgn_get_state", a.getState) && sym(h, "vgn_set_state", a.setState) && sym(h, "vgn_free", a.free) &&
                    sym(h, "vgn_editor_open", a.editorOpen) && sym(h, "vgn_editor_close", a.editorClose) &&
                    sym(h, "vgn_program_count", a.programCount) && sym(h, "vgn_program_get", a.programGet) &&
                    sym(h, "vgn_program_name", a.programName) && sym(h, "vgn_program_set", a.programSet);
    if (!ok) return "ERROR: " + lib + " does not export the vgn interface";
    if (apiVersion() != VGN_API_VERSION) return "ERROR: " + lib + " has vgn API " + std::to_string(apiVersion());
    if (!a.init(env, context)) return "ERROR: vgn_init failed";
    g_libs[lib] = a;
    LOGI("native plugin library %s: %s", lib.c_str(), a.info());
    return a.info();
}

std::string nativeInfo(const std::string& lib) {
    std::lock_guard<std::mutex> lk(g_libsMu);
    auto it = g_libs.find(lib);
    return it == g_libs.end() ? std::string() : std::string(it->second.info());
}

std::shared_ptr<NativeInstance> NativeInstance::open(const std::string& lib, double rate, int maxBlock, std::string& err) {
    const VgnApi* api = nullptr;
    {
        std::lock_guard<std::mutex> lk(g_libsMu);
        auto it = g_libs.find(lib);
        if (it == g_libs.end()) { err = lib + " not initialised"; return nullptr; }
        api = &it->second;  // std::map nodes are stable
    }
    auto n = std::make_shared<NativeInstance>();
    n->api_ = api;
    n->p_ = api->create();
    if (!n->p_) { err = "vgn_create failed"; return nullptr; }
    n->rate_ = rate;
    n->maxBlock_ = maxBlock;
    if (!api->prepare(n->p_, rate, maxBlock)) { err = "vgn_prepare failed"; return nullptr; }
    const std::string info = api->info();
    n->name_ = jsonField(info, "name");
    n->vendor_ = jsonField(info, "vendor");
    n->version_ = jsonField(info, "version");
    n->instrument_ = jsonField(info, "instrument") != "false";
    return n;
}

NativeInstance::~NativeInstance() {
    if (api_ && p_) api_->destroy(p_);
}

bool NativeInstance::queueMidi(uint8_t status, uint8_t d1, uint8_t d2) {
    while (g_midiLock.test_and_set(std::memory_order_acquire)) {}
    const uint32_t w = qw_.load(std::memory_order_relaxed);
    const bool ok = w - qr_.load(std::memory_order_acquire) < kQ;
    if (ok) {
        q_[w % kQ] = vgn_midi{0, status, d1, d2, 0};
        qw_.store(w + 1, std::memory_order_release);
    }
    g_midiLock.clear(std::memory_order_release);
    return ok;
}

void NativeInstance::process(const float* inL, const float* inR, float* outL, float* outR, uint32_t n) {
    const int64_t t0 = nowNs();
    uint32_t r = qr_.load(std::memory_order_relaxed);
    const uint32_t w = qw_.load(std::memory_order_acquire);
    int32_t count = 0;
    for (; r != w && count < (int32_t)kQ; ++r) scratch_[count++] = q_[r % kQ];
    qr_.store(r, std::memory_order_release);
    if (count) midiConsumed.fetch_add((uint64_t)count, std::memory_order_relaxed);
    const float* in[2] = {inL, inR};
    float* out[2] = {outL, outR};
    api_->process(p_, (inL && !instrument_) ? in : nullptr, out, (int32_t)n, scratch_, count);
    const uint64_t dt = (uint64_t)(nowNs() - t0);
    renderNsTotal.fetch_add(dt, std::memory_order_relaxed);
    blocks.fetch_add(1, std::memory_order_relaxed);
    if (dt > maxBlockNs.load(std::memory_order_relaxed)) maxBlockNs.store(dt, std::memory_order_relaxed);
}

std::string NativeInstance::request(const std::string& req) {
    auto a = splitLines(req);
    const std::string& cmd = a[0];
    std::string j = "{\"cmd\":\"" + jsonEsc(cmd) + "\"";
    char buf[512];
    auto title = [&](int32_t i, std::string& units, int& steps, double& def) {
        api_->paramInfo(p_, i, buf, sizeof buf);  // "name\tunits\tsteps\tdefault"
        std::vector<std::string> parts;
        const std::string s(buf);
        size_t p0 = 0;
        while (true) {
            const size_t e = s.find('\t', p0);
            parts.push_back(s.substr(p0, e == std::string::npos ? std::string::npos : e - p0));
            if (e == std::string::npos) break;
            p0 = e + 1;
        }
        units = parts.size() > 1 ? parts[1] : "";
        steps = parts.size() > 2 ? atoi(parts[2].c_str()) : 0;
        def = parts.size() > 3 ? atof(parts[3].c_str()) : 0;
        return parts[0];
    };
    auto text = [&](int32_t i, float v) {
        api_->paramText(p_, i, v, buf, sizeof buf);
        return std::string(buf);
    };
    if (cmd == "ping") {
        j += ",\"ok\":true,\"native\":true";
    } else if (cmd == "info") {
        snprintf(buf, sizeof buf, ",\"ok\":true,\"inChannels\":%d,\"outChannels\":2,\"paramCount\":%d,\"native\":true",
                 instrument_ ? 0 : 2, api_->paramCount(p_));
        j += ",\"name\":\"" + jsonEsc(name_) + "\",\"vendor\":\"" + jsonEsc(vendor_) + "\",\"version\":\"" + jsonEsc(version_) +
             "\",\"instrument\":" + (instrument_ ? "true" : "false") + buf;
    } else if (cmd == "params") {
        j += ",\"ok\":true,\"params\":[";
        const int32_t n = api_->paramCount(p_);
        for (int32_t i = 0; i < n; ++i) {
            std::string units;
            int steps;
            double def;
            const std::string t = title(i, units, steps, def);
            const float v = api_->paramGet(p_, i);
            snprintf(buf, sizeof buf, "%s{\"id\":%d,\"steps\":%d,\"value\":%.9g,\"default\":%.9g,", i ? "," : "", i, steps, v, def);
            j += buf;
            j += "\"title\":\"" + jsonEsc(t) + "\",\"units\":\"" + jsonEsc(units) + "\",\"text\":\"" + jsonEsc(text(i, v)) + "\"}";
        }
        j += "]";
    } else if (cmd == "params_tsv") {
        std::string out;
        const int32_t n = api_->paramCount(p_);
        for (int32_t i = 0; i < n; ++i) {
            std::string units;
            int steps;
            double def;
            std::string t = title(i, units, steps, def);
            for (char& ch : t) if (ch == '\t' || ch == '\n') ch = ' ';
            snprintf(buf, sizeof buf, "%d\t%d\t%.9g\t%.9g\t", i, steps, def, api_->paramGet(p_, i));
            out += buf;
            out += t + "\n";
        }
        return out;
    } else if (cmd == "set" && a.size() >= 3) {
        const int32_t id = atoi(a[1].c_str());
        const float v = (float)std::clamp(atof(a[2].c_str()), 0.0, 1.0);
        api_->paramSet(p_, id, v);
        const float now = api_->paramGet(p_, id);
        snprintf(buf, sizeof buf, ",\"ok\":true,\"id\":%d,\"value\":%.9g", id, now);
        j += buf;
        j += ",\"text\":\"" + jsonEsc(text(id, now)) + "\"";
    } else if (cmd == "programs") {
        // One list, like the Wine host's "programs": JUCE's AudioProcessor programs.
        const int32_t n = api_->programCount(p_);
        snprintf(buf, sizeof buf, ",\"ok\":true,\"lists\":[{\"id\":0,\"name\":\"Programs\",\"selectable\":true,\"current\":%d,\"programs\":[", n > 0 ? api_->programGet(p_) : -1);
        j += buf;
        for (int32_t i = 0; i < n; ++i) {
            char nm[256];
            api_->programName(p_, i, nm, sizeof nm);
            j += std::string(i ? "," : "") + "\"" + jsonEsc(nm) + "\"";
        }
        j += "]}]";
    } else if (cmd == "set_program" && a.size() >= 3) {
        const int32_t i = atoi(a[2].c_str());
        if (i < 0 || i >= api_->programCount(p_)) j += ",\"ok\":false,\"error\":\"no such program\"";
        else {
            api_->programSet(p_, i);
            char nm[256];
            api_->programName(p_, i, nm, sizeof nm);
            j += ",\"ok\":true,\"program\":\"" + jsonEsc(nm) + "\"";
        }
    } else if (cmd == "get_state") {
        int32_t size = 0;
        void* d = api_->getState(p_, &size);
        j += ",\"ok\":true,\"component\":\"" + b64enc(static_cast<const uint8_t*>(d), (size_t)size) + "\",\"controller\":\"\"";
        api_->free(d);
    } else if (cmd == "set_state" && a.size() >= 2) {
        const std::string blob = b64dec(a[1]);
        const bool ok = api_->setState(p_, blob.data(), (int32_t)blob.size());
        j += std::string(",\"ok\":") + (ok ? "true" : "false");
    } else {
        j += ",\"ok\":false,\"error\":\"unknown command\"";
    }
    return j + "}";
}

}  // namespace vg
