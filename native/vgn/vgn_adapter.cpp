// SPDX-License-Identifier: GPL-3.0-or-later
//
// vgn.h implemented on top of a JUCE AudioProcessor. Compiled into each native plugin library with
// that plugin's JucePlugin_* definitions; the plugin's own createPluginFilter() makes the instance.
#include "vgn.h"

#define JUCE_CORE_INCLUDE_JNI_HELPERS 1  // juce::JNIClassBase, for vgn_init (as juce_core.cpp does)
#include <juce_core/juce_core.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <memory>

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

struct vgn_plugin {
    std::unique_ptr<juce::AudioProcessor> proc;
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    juce::AudioBuffer<float> buf;
    juce::MidiBuffer midi;
    int inCh = 0, outCh = 2, maxBlock = 0;
    bool prepared = false;
};

namespace {

void copyStr(const juce::String& s, char* buf, int32_t size) {
    if (!buf || size <= 0) return;
    const auto utf8 = s.toStdString();
    const size_t n = std::min(utf8.size(), (size_t)size - 1);
    memcpy(buf, utf8.data(), n);
    buf[n] = 0;
}

juce::AudioProcessorParameter* param(vgn_plugin* p, int32_t i) {
    const auto& ps = p->proc->getParameters();
    return i >= 0 && i < ps.size() ? ps[i] : nullptr;
}

}  // namespace

extern "C" {

int vgn_api_version(void) { return VGN_API_VERSION; }

const char* vgn_info_json(void) {
    static const std::string s = [] {
        juce::DynamicObject::Ptr o = new juce::DynamicObject();
        o->setProperty("name", JucePlugin_Name);
        o->setProperty("vendor", JucePlugin_Manufacturer);
        o->setProperty("version", JucePlugin_VersionString);
        o->setProperty("instrument", (bool)JucePlugin_IsSynth);
        o->setProperty("inputs", JucePlugin_IsSynth ? 0 : 2);
        o->setProperty("outputs", 2);
        o->setProperty("juce", juce::String(JUCE_MAJOR_VERSION) + "." + juce::String(JUCE_MINOR_VERSION) + "." +
                                   juce::String(JUCE_BUILDNUMBER));
        return juce::JSON::toString(juce::var(o.get()), true).toStdString();
    }();
    return s.c_str();
}

int vgn_init(JNIEnv* env, jobject appContext) {
    static bool done = false;
    if (done) return 1;
    // Same as JUCE's own Java entry point (juce_JavainitialiseJUCE): class table first, then JUCE.
    juce::JNIClassBase::initialiseAllClasses(env, appContext);
    juce::Thread::initialiseJUCE(env, appContext);
    juce::initialiseJuce_GUI();  // MessageManager on this (the Android main) thread
    done = true;
    return 1;
}

vgn_plugin* vgn_create(void) {
    auto* p = new vgn_plugin;
    p->proc.reset(createPluginFilter());
    if (!p->proc) { delete p; return nullptr; }
    p->inCh = JucePlugin_IsSynth ? 0 : 2;
    p->outCh = 2;
    return p;
}

void vgn_destroy(vgn_plugin* p) {
    if (!p) return;
    vgn_editor_close(p);
    if (p->prepared) p->proc->releaseResources();
    delete p;
}

int vgn_prepare(vgn_plugin* p, double sampleRate, int32_t maxBlock) {
    if (!p) return 0;
    if (p->prepared) p->proc->releaseResources();
    auto layout = p->proc->getBusesLayout();
    if (p->inCh > 0 && p->proc->getBusCount(true) > 0) layout.getChannelSet(true, 0) = juce::AudioChannelSet::stereo();
    if (p->proc->getBusCount(false) > 0) layout.getChannelSet(false, 0) = juce::AudioChannelSet::stereo();
    p->proc->setBusesLayout(layout);
    p->proc->setRateAndBufferSizeDetails(sampleRate, maxBlock);
    p->proc->setNonRealtime(false);
    p->proc->prepareToPlay(sampleRate, maxBlock);
    const int ch = std::max({p->proc->getTotalNumInputChannels(), p->proc->getTotalNumOutputChannels(), 2});
    p->buf.setSize(ch, maxBlock);
    p->midi.ensureSize(4096);
    p->maxBlock = maxBlock;
    p->prepared = true;
    return 1;
}

void vgn_process(vgn_plugin* p, const float* const* in, float* const* out, int32_t frames, const vgn_midi* midi,
                 int32_t numMidi) {
    if (!p || !p->prepared) {
        for (int c = 0; c < 2; ++c) memset(out[c], 0, sizeof(float) * (size_t)frames);
        return;
    }
    int32_t done = 0;
    int32_t m = 0;
    while (done < frames) {
        const int32_t n = std::min(frames - done, p->maxBlock);
        p->midi.clear();
        for (; m < numMidi && midi[m].offset < done + n; ++m) {
            const uint8_t b[3] = {midi[m].status, midi[m].d1, midi[m].d2};
            const int len = (midi[m].status & 0xF0) == 0xC0 || (midi[m].status & 0xF0) == 0xD0 ? 2 : 3;
            p->midi.addEvent(b, len, std::max(0, midi[m].offset - done));
        }
        const int ch = p->buf.getNumChannels();
        juce::AudioBuffer<float> view(p->buf.getArrayOfWritePointers(), ch, n);
        for (int c = 0; c < ch; ++c) {
            if (in && c < 2 && p->inCh > 0) memcpy(view.getWritePointer(c), in[c] + done, sizeof(float) * (size_t)n);
            else view.clear(c, 0, n);
        }
        {
            const juce::ScopedLock lock(p->proc->getCallbackLock());
            if (p->proc->isSuspended()) view.clear();
            else p->proc->processBlock(view, p->midi);
        }
        const int outs = p->proc->getTotalNumOutputChannels();
        for (int c = 0; c < 2; ++c) {
            const int src = outs == 1 ? 0 : c;
            memcpy(out[c] + done, view.getReadPointer(std::min(src, ch - 1)), sizeof(float) * (size_t)n);
        }
        done += n;
    }
}

int32_t vgn_param_count(vgn_plugin* p) { return p ? p->proc->getParameters().size() : 0; }

void vgn_param_info(vgn_plugin* p, int32_t i, char* buf, int32_t size) {
    auto* q = p ? param(p, i) : nullptr;
    if (!q) { copyStr({}, buf, size); return; }
    const int steps = q->isDiscrete() || q->isBoolean() ? q->getNumSteps() : 0;
    copyStr(q->getName(128) + "\t" + q->getLabel() + "\t" + juce::String(steps) + "\t" + juce::String(q->getDefaultValue()),
            buf, size);
}

float vgn_param_get(vgn_plugin* p, int32_t i) {
    auto* q = p ? param(p, i) : nullptr;
    return q ? q->getValue() : 0.0f;
}

void vgn_param_set(vgn_plugin* p, int32_t i, float v) {
    if (auto* q = p ? param(p, i) : nullptr) q->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, v));
}

void vgn_param_text(vgn_plugin* p, int32_t i, float v, char* buf, int32_t size) {
    auto* q = p ? param(p, i) : nullptr;
    copyStr(q ? q->getText(v, 64) : juce::String(), buf, size);
}

int32_t vgn_program_count(vgn_plugin* p) { return p ? p->proc->getNumPrograms() : 0; }
int32_t vgn_program_get(vgn_plugin* p) { return p ? p->proc->getCurrentProgram() : 0; }
void vgn_program_name(vgn_plugin* p, int32_t i, char* buf, int32_t size) {
    copyStr(p && i >= 0 && i < p->proc->getNumPrograms() ? p->proc->getProgramName(i) : juce::String(), buf, size);
}
void vgn_program_set(vgn_plugin* p, int32_t i) {
    if (p && i >= 0 && i < p->proc->getNumPrograms()) p->proc->setCurrentProgram(i);
}

void* vgn_get_state(vgn_plugin* p, int32_t* size) {
    *size = 0;
    if (!p) return nullptr;
    juce::MemoryBlock mb;
    p->proc->getStateInformation(mb);
    void* out = malloc(std::max<size_t>(mb.getSize(), 1));
    if (mb.getSize()) memcpy(out, mb.getData(), mb.getSize());
    *size = (int32_t)mb.getSize();
    return out;
}

int vgn_set_state(vgn_plugin* p, const void* data, int32_t size) {
    if (!p || !data || size <= 0) return 0;
    p->proc->setStateInformation(data, size);
    return 1;
}

void vgn_free(void* d) { free(d); }

int vgn_editor_open(vgn_plugin* p, JNIEnv*, jobject viewGroup, int32_t availW, int32_t availH, int32_t* w, int32_t* h) {
    if (!p || !p->proc->hasEditor()) return 0;
    if (!p->editor) {
        p->editor.reset(p->proc->createEditorIfNeeded());
        if (!p->editor) return 0;
    }
    auto* ed = p->editor.get();
    // JUCE works in logical pixels (physical / display density). Scale the whole UI so the editor
    // fits the space we were given.
    const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
    const double density = display ? display->scale : 1.0;
    const int lw = std::max(ed->getWidth(), 1), lh = std::max(ed->getHeight(), 1);
    const double fit = std::min(availW / (lw * density), availH / (lh * density));
    juce::Desktop::getInstance().setGlobalScaleFactor((float)std::max(0.1, fit));
    if (ed->isOnDesktop()) ed->removeFromDesktop();  // moving to a new screen: re-parent the view
    ed->setVisible(true);
    ed->addToDesktop(0, viewGroup);
    ed->setTopLeftPosition(0, 0);
    ed->toFront(true);
    *w = (int32_t)(lw * density * fit);
    *h = (int32_t)(lh * density * fit);
    return 1;
}

void vgn_editor_close(vgn_plugin* p) {
    if (!p || !p->editor) return;
    p->editor->removeFromDesktop();
    // The host must tell the processor first (~AudioProcessorEditor only asserts it happened);
    // otherwise JUCE keeps a stale "active editor" and refuses to create the next one.
    p->proc->editorBeingDeleted(p->editor.get());
    p->editor.reset();
}

}  // extern "C"
