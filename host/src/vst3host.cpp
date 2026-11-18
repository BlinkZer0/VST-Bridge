// SPDX-License-Identifier: GPL-3.0-or-later
#include "vst3host.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <map>
#include <variant>

#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/vst/ivstattributes.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivsthostapplication.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "util.h"

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace vg {
namespace {

#define VG_QI(I)                                                     \
    if (FUnknownPrivate::iidEqual(_iid, I::iid.toTUID())) {          \
        addRef();                                                    \
        *obj = static_cast<I*>(this);                                \
        return kResultOk;                                            \
    }
#define VG_REFCOUNT                                                  \
    std::atomic<uint32> refs_{1};                                    \
    uint32 PLUGIN_API addRef() override { return ++refs_; }          \
    uint32 PLUGIN_API release() override {                           \
        uint32 r = --refs_;                                          \
        if (!r) delete this;                                         \
        return r;                                                    \
    }

template <class T> void releaseAndNull(T*& p) { if (p) { p->release(); p = nullptr; } }

// ---------------------------------------------------------------- host-side objects

class AttributeList : public IAttributeList {
public:
    VG_REFCOUNT
    tresult PLUGIN_API queryInterface(const TUID _iid, void** obj) override {
        VG_QI(IAttributeList) VG_QI(FUnknown)
        *obj = nullptr; return kNoInterface;
    }
    tresult PLUGIN_API setInt(AttrID id, int64 v) override { m_[id] = v; return kResultOk; }
    tresult PLUGIN_API getInt(AttrID id, int64& v) override {
        auto it = m_.find(id);
        if (it == m_.end() || !std::holds_alternative<int64>(it->second)) return kResultFalse;
        v = std::get<int64>(it->second); return kResultOk;
    }
    tresult PLUGIN_API setFloat(AttrID id, double v) override { m_[id] = v; return kResultOk; }
    tresult PLUGIN_API getFloat(AttrID id, double& v) override {
        auto it = m_.find(id);
        if (it == m_.end() || !std::holds_alternative<double>(it->second)) return kResultFalse;
        v = std::get<double>(it->second); return kResultOk;
    }
    tresult PLUGIN_API setString(AttrID id, const TChar* s) override {
        std::u16string v; for (; s && *s; ++s) v.push_back(*s);
        m_[id] = v; return kResultOk;
    }
    tresult PLUGIN_API getString(AttrID id, TChar* s, uint32 sizeInBytes) override {
        auto it = m_.find(id);
        if (it == m_.end() || !std::holds_alternative<std::u16string>(it->second) || sizeInBytes < 2) return kResultFalse;
        const auto& v = std::get<std::u16string>(it->second);
        size_t n = std::min<size_t>(v.size(), sizeInBytes / 2 - 1);
        memcpy(s, v.data(), n * 2); s[n] = 0; return kResultOk;
    }
    tresult PLUGIN_API setBinary(AttrID id, const void* data, uint32 size) override {
        auto* b = static_cast<const char*>(data);
        m_[id] = std::vector<char>(b, b + size); return kResultOk;
    }
    tresult PLUGIN_API getBinary(AttrID id, const void*& data, uint32& size) override {
        auto it = m_.find(id);
        if (it == m_.end() || !std::holds_alternative<std::vector<char>>(it->second)) return kResultFalse;
        const auto& v = std::get<std::vector<char>>(it->second);
        data = v.data(); size = (uint32)v.size(); return kResultOk;
    }
    virtual ~AttributeList() = default;
private:
    std::map<std::string, std::variant<int64, double, std::u16string, std::vector<char>>> m_;
};

class Message : public IMessage {
public:
    VG_REFCOUNT
    tresult PLUGIN_API queryInterface(const TUID _iid, void** obj) override {
        VG_QI(IMessage) VG_QI(FUnknown)
        *obj = nullptr; return kNoInterface;
    }
    FIDString PLUGIN_API getMessageID() override { return id_.c_str(); }
    void PLUGIN_API setMessageID(FIDString id) override { id_ = id ? id : ""; }
    IAttributeList* PLUGIN_API getAttributes() override { return attrs_; }
    virtual ~Message() { attrs_->release(); }
private:
    std::string id_;
    AttributeList* attrs_ = new AttributeList;
};

class HostApp : public IHostApplication {
public:
    VG_REFCOUNT
    tresult PLUGIN_API queryInterface(const TUID _iid, void** obj) override {
        VG_QI(IHostApplication) VG_QI(FUnknown)
        *obj = nullptr; return kNoInterface;
    }
    tresult PLUGIN_API getName(String128 name) override {
        const char16_t n[] = u"VST gHost";
        memcpy(name, n, sizeof n); return kResultOk;
    }
    tresult PLUGIN_API createInstance(TUID cid, TUID _iid, void** obj) override {
        if (FUnknownPrivate::iidEqual(cid, IMessage::iid.toTUID()) && FUnknownPrivate::iidEqual(_iid, IMessage::iid.toTUID())) {
            *obj = static_cast<IMessage*>(new Message); return kResultOk;
        }
        if (FUnknownPrivate::iidEqual(cid, IAttributeList::iid.toTUID()) && FUnknownPrivate::iidEqual(_iid, IAttributeList::iid.toTUID())) {
            *obj = static_cast<IAttributeList*>(new AttributeList); return kResultOk;
        }
        *obj = nullptr; return kResultFalse;
    }
    virtual ~HostApp() = default;
};

class ComponentHandler : public IComponentHandler {
public:
    VG_REFCOUNT
    tresult PLUGIN_API queryInterface(const TUID _iid, void** obj) override {
        VG_QI(IComponentHandler) VG_QI(FUnknown)
        *obj = nullptr; return kNoInterface;
    }
    tresult PLUGIN_API beginEdit(ParamID) override { return kResultOk; }
    tresult PLUGIN_API performEdit(ParamID, ParamValue) override { return kResultOk; }
    tresult PLUGIN_API endEdit(ParamID) override { return kResultOk; }
    tresult PLUGIN_API restartComponent(int32 flags) override { restartFlags |= flags; return kResultOk; }
    int32 restartFlags = 0;
    virtual ~ComponentHandler() = default;
};

class MemoryStream : public IBStream {
public:
    VG_REFCOUNT
    tresult PLUGIN_API queryInterface(const TUID _iid, void** obj) override {
        VG_QI(IBStream) VG_QI(FUnknown)
        *obj = nullptr; return kNoInterface;
    }
    tresult PLUGIN_API read(void* buf, int32 n, int32* got) override {
        int32 k = (int32)std::min<int64>(n, (int64)data.size() - pos);
        if (k < 0) k = 0;
        memcpy(buf, data.data() + pos, (size_t)k); pos += k;
        if (got) *got = k;
        return kResultOk;
    }
    tresult PLUGIN_API write(void* buf, int32 n, int32* wrote) override {
        if (pos + n > (int64)data.size()) data.resize((size_t)(pos + n));
        memcpy(data.data() + pos, buf, (size_t)n); pos += n;
        if (wrote) *wrote = n;
        return kResultOk;
    }
    tresult PLUGIN_API seek(int64 p, int32 mode, int64* result) override {
        int64 np = mode == kIBSeekSet ? p : mode == kIBSeekCur ? pos + p : (int64)data.size() + p;
        if (np < 0) return kResultFalse;
        pos = np; if (result) *result = pos;
        return kResultOk;
    }
    tresult PLUGIN_API tell(int64* p) override { if (p) *p = pos; return kResultOk; }
    std::vector<char> data;
    int64 pos = 0;
    virtual ~MemoryStream() = default;
};

class EventList : public IEventList {
public:
    VG_REFCOUNT
    tresult PLUGIN_API queryInterface(const TUID _iid, void** obj) override {
        VG_QI(IEventList) VG_QI(FUnknown)
        *obj = nullptr; return kNoInterface;
    }
    int32 PLUGIN_API getEventCount() override { return (int32)events.size(); }
    tresult PLUGIN_API getEvent(int32 i, Event& e) override {
        if (i < 0 || i >= (int32)events.size()) return kResultFalse;
        e = events[(size_t)i]; return kResultOk;
    }
    tresult PLUGIN_API addEvent(Event& e) override { events.push_back(e); return kResultOk; }
    std::vector<Event> events;
    virtual ~EventList() = default;
};

class ParamQueue : public IParamValueQueue {
public:
    VG_REFCOUNT
    tresult PLUGIN_API queryInterface(const TUID _iid, void** obj) override {
        VG_QI(IParamValueQueue) VG_QI(FUnknown)
        *obj = nullptr; return kNoInterface;
    }
    ParamID PLUGIN_API getParameterId() override { return id; }
    int32 PLUGIN_API getPointCount() override { return (int32)points.size(); }
    tresult PLUGIN_API getPoint(int32 i, int32& offset, ParamValue& v) override {
        if (i < 0 || i >= (int32)points.size()) return kResultFalse;
        offset = points[(size_t)i].first; v = points[(size_t)i].second; return kResultOk;
    }
    tresult PLUGIN_API addPoint(int32 offset, ParamValue v, int32& index) override {
        points.emplace_back(offset, v); index = (int32)points.size() - 1; return kResultOk;
    }
    ParamID id = 0;
    std::vector<std::pair<int32, ParamValue>> points;
    virtual ~ParamQueue() = default;
};

class ParamChanges : public IParameterChanges {
public:
    VG_REFCOUNT
    tresult PLUGIN_API queryInterface(const TUID _iid, void** obj) override {
        VG_QI(IParameterChanges) VG_QI(FUnknown)
        *obj = nullptr; return kNoInterface;
    }
    int32 PLUGIN_API getParameterCount() override { return used_; }
    IParamValueQueue* PLUGIN_API getParameterData(int32 i) override {
        return i >= 0 && i < used_ ? queues_[(size_t)i] : nullptr;
    }
    IParamValueQueue* PLUGIN_API addParameterData(const ParamID& id, int32& index) override {
        for (int32 i = 0; i < used_; ++i)
            if (queues_[(size_t)i]->id == id) { index = i; return queues_[(size_t)i]; }
        if (used_ == (int32)queues_.size()) queues_.push_back(new ParamQueue);
        ParamQueue* q = queues_[(size_t)used_];
        q->id = id; q->points.clear();
        index = used_++;
        return q;
    }
    void clear() { used_ = 0; }
    virtual ~ParamChanges() { for (auto* q : queues_) q->release(); }
private:
    std::vector<ParamQueue*> queues_;
    int32 used_ = 0;
};

std::string tuidHex(const TUID t) {
    char b[33];
    for (int i = 0; i < 16; ++i) snprintf(b + 2 * i, 3, "%02X", (unsigned char)t[i]);
    return b;
}

std::wstring findDll(const std::wstring& path) {
    DWORD a = GetFileAttributesW(path.c_str());
    if (a == INVALID_FILE_ATTRIBUTES) return {};
    if (!(a & FILE_ATTRIBUTE_DIRECTORY)) return path;  // single-file (legacy) VST3
    // Bundle: <name>.vst3/Contents/x86_64-win/<name>.vst3
    std::wstring base = path;
    while (!base.empty() && (base.back() == L'\\' || base.back() == L'/')) base.pop_back();
    size_t slash = base.find_last_of(L"\\/");
    std::wstring leaf = slash == std::wstring::npos ? base : base.substr(slash + 1);
    std::wstring dll = base + L"\\Contents\\x86_64-win\\" + leaf;
    if (GetFileAttributesW(dll.c_str()) != INVALID_FILE_ATTRIBUTES) return dll;
    // Otherwise the first *.vst3 file in that folder.
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((base + L"\\Contents\\x86_64-win\\*.vst3").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return {};
    FindClose(h);
    return base + L"\\Contents\\x86_64-win\\" + fd.cFileName;
}

std::string lastErrorText(DWORD e) {
    wchar_t* msg = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, e, 0, (LPWSTR)&msg, 0, nullptr);
    std::string s = msg ? utf8(std::wstring(msg)) : "";
    if (msg) LocalFree(msg);
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
    char b[32]; snprintf(b, sizeof b, " (Win32 error %lu)", e);
    return s + b;
}

}  // namespace

// ---------------------------------------------------------------- Plugin

struct Plugin::Impl {
    HMODULE module = nullptr;
    IPluginFactory* factory = nullptr;
    IComponent* component = nullptr;
    IAudioProcessor* processor = nullptr;
    IEditController* controller = nullptr;
    IMidiMapping* midiMapping = nullptr;
    IConnectionPoint* compCP = nullptr;
    IConnectionPoint* ctrlCP = nullptr;
    HostApp* host = new HostApp;
    ComponentHandler* handler = new ComponentHandler;
    bool componentInitialized = false, controllerInitialized = false;

    ~Impl() {
        if (compCP && ctrlCP) { compCP->disconnect(ctrlCP); ctrlCP->disconnect(compCP); }
        releaseAndNull(compCP);
        releaseAndNull(ctrlCP);
        releaseAndNull(midiMapping);
        if (controller) {
            controller->setComponentHandler(nullptr);
            if (controllerInitialized) controller->terminate();
        }
        releaseAndNull(controller);
        releaseAndNull(processor);
        if (component && componentInitialized) component->terminate();
        releaseAndNull(component);
        releaseAndNull(factory);
        if (module) {
            using ExitFn = bool (*)();
            if (auto ex = (ExitFn)(void*)GetProcAddress(module, "ExitDll")) ex();
            FreeLibrary(module);
        }
        handler->release();
        host->release();
    }
};

void pumpMessages(int ms) {
    double end = nowMs() + ms;
    do {
        MSG m;
        while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); }
        if (ms > 0) Sleep(1);
    } while (nowMs() < end);
}

Plugin::~Plugin() = default;

std::unique_ptr<Plugin> Plugin::load(const std::wstring& path, std::string& err, LoadTimings& t) {
    auto p = std::make_unique<Plugin>();
    p->impl = std::make_unique<Impl>();
    Impl& I = *p->impl;

    std::wstring dll = findDll(path);
    if (dll.empty()) { err = "no x86_64-win binary found in bundle"; return nullptr; }
    p->dllPath = utf8(dll);

    double t0 = nowMs();
    I.module = LoadLibraryExW(dll.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    t.loadLibraryMs = nowMs() - t0;
    if (!I.module) { err = "LoadLibrary failed: " + lastErrorText(GetLastError()); return nullptr; }

    t0 = nowMs();
    using InitFn = bool (*)();
    if (auto init = (InitFn)(void*)GetProcAddress(I.module, "InitDll"))
        if (!init()) { err = "InitDll returned false"; return nullptr; }
    using GetFactoryFn = IPluginFactory* (PLUGIN_API*)();
    auto getFactory = (GetFactoryFn)(void*)GetProcAddress(I.module, "GetPluginFactory");
    if (!getFactory) { err = "no GetPluginFactory export (not a VST3 module)"; return nullptr; }
    I.factory = getFactory();
    if (!I.factory) { err = "GetPluginFactory returned null"; return nullptr; }
    {
        IPluginFactory3* f3 = nullptr;
        if (I.factory->queryInterface(IPluginFactory3::iid.toTUID(), (void**)&f3) == kResultOk && f3) {
            f3->setHostContext(I.host);
            f3->release();
        }
    }
    PFactoryInfo fi{};
    I.factory->getFactoryInfo(&fi);
    p->vendor = fi.vendor;
    t.factoryMs = nowMs() - t0;

    // First audio module class.
    IPluginFactory2* f2 = nullptr;
    I.factory->queryInterface(IPluginFactory2::iid.toTUID(), (void**)&f2);
    TUID cid{};
    bool found = false;
    for (int32 i = 0; i < I.factory->countClasses() && !found; ++i) {
        PClassInfo ci{};
        if (I.factory->getClassInfo(i, &ci) != kResultOk || strcmp(ci.category, kVstAudioEffectClass) != 0) continue;
        memcpy(cid, ci.cid, sizeof(TUID));
        p->name = ci.name;
        if (f2) {
            PClassInfo2 c2{};
            if (f2->getClassInfo2(i, &c2) == kResultOk) {
                p->subCategories = c2.subCategories;
                p->version = c2.version;
                p->sdkVersion = c2.sdkVersion;
                if (c2.vendor[0]) p->vendor = c2.vendor;
            }
        }
        found = true;
    }
    if (f2) f2->release();
    if (!found) { err = "module has no Audio Module Class"; return nullptr; }
    p->cid = tuidHex(cid);
    p->isInstrument = p->subCategories.find("Instrument") != std::string::npos;

    t0 = nowMs();
    if (I.factory->createInstance(cid, IComponent::iid.toTUID(), (void**)&I.component) != kResultOk || !I.component) {
        err = "createInstance(IComponent) failed"; return nullptr;
    }
    if (I.component->initialize(I.host) != kResultOk) { err = "IComponent::initialize failed"; return nullptr; }
    I.componentInitialized = true;
    if (I.component->queryInterface(IAudioProcessor::iid.toTUID(), (void**)&I.processor) != kResultOk || !I.processor) {
        err = "component has no IAudioProcessor"; return nullptr;
    }
    t.componentMs = nowMs() - t0;

    // Controller: same object, or a separate class.
    t0 = nowMs();
    if (I.component->queryInterface(IEditController::iid.toTUID(), (void**)&I.controller) != kResultOk || !I.controller) {
        I.controller = nullptr;
        TUID ccid{};
        if (I.component->getControllerClassId(ccid) == kResultOk &&
            I.factory->createInstance(ccid, IEditController::iid.toTUID(), (void**)&I.controller) == kResultOk && I.controller) {
            p->separateController = true;
            if (I.controller->initialize(I.host) != kResultOk) { err = "IEditController::initialize failed"; return nullptr; }
            I.controllerInitialized = true;
        }
    }
    if (I.controller) {
        I.controller->setComponentHandler(I.handler);
        if (p->separateController) {
            I.component->queryInterface(IConnectionPoint::iid.toTUID(), (void**)&I.compCP);
            I.controller->queryInterface(IConnectionPoint::iid.toTUID(), (void**)&I.ctrlCP);
            if (I.compCP && I.ctrlCP) { I.compCP->connect(I.ctrlCP); I.ctrlCP->connect(I.compCP); }
            auto* st = new MemoryStream;
            if (I.component->getState(st) == kResultOk) { st->pos = 0; I.controller->setComponentState(st); }
            st->release();
        }
        if (I.controller->queryInterface(IMidiMapping::iid.toTUID(), (void**)&I.midiMapping) != kResultOk) I.midiMapping = nullptr;
        p->hasMidiMapping = I.midiMapping != nullptr;

        for (int32 i = 0; i < I.controller->getParameterCount(); ++i) {
            ParameterInfo pi{};
            if (I.controller->getParameterInfo(i, pi) != kResultOk) continue;
            ParamDesc d{pi.id, utf8(pi.title), utf8(pi.units), "", pi.stepCount, pi.defaultNormalizedValue, pi.flags};
            String128 txt{};
            if (I.controller->getParamStringByValue(pi.id, pi.defaultNormalizedValue, txt) == kResultOk) d.defaultText = utf8(txt);
            p->params.push_back(std::move(d));
        }
    }
    t.controllerMs = nowMs() - t0;

    auto buses = [&](MediaType type, BusDirection dir, std::vector<BusDesc>& out) {
        for (int32 i = 0; i < I.component->getBusCount(type, dir); ++i) {
            BusInfo bi{};
            if (I.component->getBusInfo(type, dir, i, bi) != kResultOk) continue;
            out.push_back({utf8(bi.name), bi.channelCount, bi.busType == kMain, (bi.flags & BusInfo::kDefaultActive) != 0});
        }
    };
    buses(kAudio, kInput, p->audioIn);
    buses(kAudio, kOutput, p->audioOut);
    buses(kEvent, kInput, p->eventIn);
    pumpMessages(20);
    return p;
}

bool Plugin::render(const RenderSettings& s, RenderStats& st, std::string& err) {
    Impl& I = *impl;
    const int32 B = s.blockSize;

    // Stereo main buses where possible; keep whatever the plugin insists on otherwise.
    {
        std::vector<SpeakerArrangement> ins(audioIn.size(), SpeakerArr::kStereo), outs(audioOut.size(), SpeakerArr::kStereo);
        I.processor->setBusArrangements(ins.empty() ? nullptr : ins.data(), (int32)ins.size(),
                                        outs.empty() ? nullptr : outs.data(), (int32)outs.size());
        audioIn.clear(); audioOut.clear();
        for (int32 i = 0; i < I.component->getBusCount(kAudio, kInput); ++i) {
            BusInfo bi{}; I.component->getBusInfo(kAudio, kInput, i, bi);
            audioIn.push_back({utf8(bi.name), bi.channelCount, bi.busType == kMain, (bi.flags & BusInfo::kDefaultActive) != 0});
        }
        for (int32 i = 0; i < I.component->getBusCount(kAudio, kOutput); ++i) {
            BusInfo bi{}; I.component->getBusInfo(kAudio, kOutput, i, bi);
            audioOut.push_back({utf8(bi.name), bi.channelCount, bi.busType == kMain, (bi.flags & BusInfo::kDefaultActive) != 0});
        }
    }
    if (audioOut.empty()) { err = "plugin has no audio output bus"; return false; }
    if (!audioIn.empty()) I.component->activateBus(kAudio, kInput, 0, true);
    I.component->activateBus(kAudio, kOutput, 0, true);
    if (!eventIn.empty()) I.component->activateBus(kEvent, kInput, 0, true);

    ProcessSetup setup{s.realtimeMode ? kRealtime : kOffline, kSample32, B, s.sampleRate};
    if (I.processor->setupProcessing(setup) != kResultOk) { err = "setupProcessing failed"; return false; }
    if (I.component->setActive(true) != kResultOk) { err = "setActive(true) failed"; return false; }
    I.processor->setProcessing(true);  // some plugins return kNotImplemented; that's allowed

    // Length: last MIDI event / input length, plus tail.
    double contentSec = 0;
    for (const auto& e : s.midi) contentSec = std::max(contentSec, e.timeSec);
    if (!s.input.empty()) contentSec = std::max(contentSec, (double)s.input[0].size() / s.sampleRate);
    const int64_t total = (int64_t)std::ceil((contentSec + s.tailSec) * s.sampleRate);

    // Buffers for every bus.
    auto makeBufs = [&](const std::vector<BusDesc>& bd, std::vector<std::vector<float>>& store,
                        std::vector<std::vector<float*>>& ptrs, std::vector<AudioBusBuffers>& abb) {
        ptrs.resize(bd.size()); abb.resize(bd.size());
        for (size_t b = 0; b < bd.size(); ++b) {
            for (int32 c = 0; c < bd[b].channels; ++c) { store.emplace_back(B, 0.0f); ptrs[b].push_back(store.back().data()); }
        }
        for (size_t b = 0; b < bd.size(); ++b) {
            abb[b].numChannels = bd[b].channels;
            abb[b].silenceFlags = 0;
            abb[b].channelBuffers32 = ptrs[b].empty() ? nullptr : ptrs[b].data();
        }
    };
    std::vector<std::vector<float>> inStore, outStore;
    inStore.reserve(64); outStore.reserve(64);
    std::vector<std::vector<float*>> inPtrs, outPtrs;
    std::vector<AudioBusBuffers> inBufs, outBufs;
    makeBufs(audioIn, inStore, inPtrs, inBufs);
    makeBufs(audioOut, outStore, outPtrs, outBufs);

    const int outCh = audioOut[0].channels;
    st.out.assign(outCh, std::vector<float>((size_t)total, 0.0f));

    auto* events = new EventList;
    auto* changes = new ParamChanges;
    ProcessContext ctx{};
    ctx.sampleRate = s.sampleRate;
    ctx.tempo = 120.0;
    ctx.timeSigNumerator = 4;
    ctx.timeSigDenominator = 4;
    ctx.state = ProcessContext::kPlaying | ProcessContext::kTempoValid | ProcessContext::kTimeSigValid |
                ProcessContext::kProjectTimeMusicValid | ProcessContext::kContTimeValid;

    ProcessData pd{};
    pd.processMode = setup.processMode;
    pd.symbolicSampleSize = kSample32;
    pd.numInputs = (int32)inBufs.size();
    pd.numOutputs = (int32)outBufs.size();
    pd.inputs = inBufs.empty() ? nullptr : inBufs.data();
    pd.outputs = outBufs.data();
    pd.inputEvents = events;
    pd.inputParameterChanges = changes;
    pd.processContext = &ctx;

    size_t mi = 0;
    std::vector<double> blockMs;
    blockMs.reserve((size_t)(total / B + 1));
    double t0 = nowMs();
    for (int64_t pos = 0; pos < total; pos += B) {
        const int32 n = (int32)std::min<int64_t>(B, total - pos);
        events->events.clear();
        changes->clear();
        // MIDI for this block.
        while (mi < s.midi.size() && (int64_t)std::floor(s.midi[mi].timeSec * s.sampleRate) < pos + n) {
            const MidiEvent& m = s.midi[mi++];
            const int32 off = (int32)std::clamp<int64_t>((int64_t)std::floor(m.timeSec * s.sampleRate) - pos, 0, n - 1);
            const uint8_t type = m.status & 0xF0;
            const int16 chan = m.status & 0x0F;
            Event e{};
            e.busIndex = 0; e.sampleOffset = off; e.flags = Event::kIsLive;
            if (type == 0x90 && m.d2 > 0) {
                e.type = Event::kNoteOnEvent;
                e.noteOn = {chan, (int16)m.d1, 0.0f, m.d2 / 127.0f, 0, -1};
                events->addEvent(e); ++st.notes;
            } else if (type == 0x80 || (type == 0x90 && m.d2 == 0)) {
                e.type = Event::kNoteOffEvent;
                e.noteOff = {chan, (int16)m.d1, m.d2 / 127.0f, -1, 0.0f};
                events->addEvent(e);
            } else if (type == 0xB0 || type == 0xE0 || type == 0xD0) {
                CtrlNumber cc = type == 0xB0 ? (CtrlNumber)m.d1 : type == 0xE0 ? (CtrlNumber)kPitchBend : (CtrlNumber)kAfterTouch;
                double v = type == 0xB0 ? m.d2 / 127.0 : type == 0xE0 ? ((m.d2 << 7) | m.d1) / 16383.0 : m.d1 / 127.0;
                ParamID pid;
                if (I.midiMapping && I.midiMapping->getMidiControllerAssignment(0, chan, cc, pid) == kResultOk) {
                    int32 qi, pi;
                    changes->addParameterData(pid, qi)->addPoint(off, v, pi);
                    ++st.ccMapped;
                } else {
                    ++st.ccUnmapped;
                }
            }
        }
        // Effect input for this block.
        for (size_t b = 0; b < inPtrs.size(); ++b)
            for (size_t c = 0; c < inPtrs[b].size(); ++c) {
                float* dst = inPtrs[b][c];
                const std::vector<float>* src = (b == 0 && !s.input.empty()) ? &s.input[std::min(c, s.input.size() - 1)] : nullptr;
                for (int32 i = 0; i < n; ++i) dst[i] = src && pos + i < (int64_t)src->size() ? (*src)[(size_t)(pos + i)] : 0.0f;
            }
        for (auto& o : outStore) std::fill(o.begin(), o.end(), 0.0f);

        ctx.projectTimeSamples = pos;
        ctx.continousTimeSamples = pos;
        ctx.projectTimeMusic = (double)pos / s.sampleRate * (ctx.tempo / 60.0);
        pd.numSamples = n;
        for (auto& b : outBufs) b.silenceFlags = 0;

        double b0 = nowMs();
        tresult r = I.processor->process(pd);
        blockMs.push_back(nowMs() - b0);
        if (r != kResultOk && r != kNotImplemented) { /* keep going: record but don't abort */ }

        for (int c = 0; c < outCh; ++c) {
            const float* src = outPtrs[0][(size_t)c];
            for (int32 i = 0; i < n; ++i) {
                float v = src[i];
                if (!std::isfinite(v)) { ++st.nonFinite; v = 0; }
                st.out[(size_t)c][(size_t)(pos + i)] = v;
            }
        }
    }
    st.renderMs = nowMs() - t0;
    st.audioSec = (double)total / s.sampleRate;
    st.blockBudgetMs = B * 1000.0 / s.sampleRate;

    I.processor->setProcessing(false);
    I.component->setActive(false);
    events->release();
    changes->release();

    std::sort(blockMs.begin(), blockMs.end());
    if (!blockMs.empty()) {
        st.blockP50 = blockMs[blockMs.size() / 2];
        st.blockP99 = blockMs[std::min(blockMs.size() - 1, (size_t)(blockMs.size() * 0.99))];
        st.blockMax = blockMs.back();
    }
    for (const auto& ch : st.out) {
        double peak = 0, sum = 0;
        for (float v : ch) { peak = std::max(peak, (double)std::fabs(v)); sum += (double)v * v; }
        double rms = ch.empty() ? 0 : std::sqrt(sum / ch.size());
        st.peakDb.push_back(peak > 0 ? 20 * std::log10(peak) : -999);
        st.rmsDb.push_back(rms > 0 ? 20 * std::log10(rms) : -999);
    }
    return true;
}

}  // namespace vg
