// SPDX-License-Identifier: LicenseRef-VstGhost-Proprietary
//
// vgn: the C interface every native (Android) plugin library exports. VST gHost's engine loads a
// plugin library (lib vg_<name>.so, built from the plugin's own JUCE source by native/CMakeLists.txt)
// and drives it through these functions only, so plugins built with different JUCE versions can
// live in one process.
//
// Threads: vgn_init, vgn_create, vgn_destroy, vgn_prepare and the editor functions run on the
// Android main thread (JUCE's message thread). vgn_process runs on the audio thread. Parameter
// get/set and state are safe from any thread.
#pragma once

#include <jni.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VGN_API_VERSION 2
#define VGN_EXPORT __attribute__((visibility("default")))

typedef struct vgn_plugin vgn_plugin;

typedef struct vgn_midi {
    int32_t offset;  // frame within the block
    uint8_t status, d1, d2, pad;
} vgn_midi;

VGN_EXPORT int vgn_api_version(void);
// {"name","vendor","version","instrument":bool,"inputs":N,"outputs":N,"juce":"x.y.z"}
VGN_EXPORT const char* vgn_info_json(void);
// Once per process, main thread: initialises this library's copy of JUCE.
VGN_EXPORT int vgn_init(JNIEnv* env, jobject appContext);

VGN_EXPORT vgn_plugin* vgn_create(void);
VGN_EXPORT void vgn_destroy(vgn_plugin*);
VGN_EXPORT int vgn_prepare(vgn_plugin*, double sampleRate, int32_t maxBlock);
// out: 2 channel pointers (stereo); in: 2 pointers for effects, else NULL.
VGN_EXPORT void vgn_process(vgn_plugin*, const float* const* in, float* const* out, int32_t frames,
                            const vgn_midi* midi, int32_t numMidi);

VGN_EXPORT int32_t vgn_param_count(vgn_plugin*);
// Writes "name\tunits\tsteps\tdefault" (steps 0 = continuous) into buf.
VGN_EXPORT void vgn_param_info(vgn_plugin*, int32_t index, char* buf, int32_t bufSize);
VGN_EXPORT float vgn_param_get(vgn_plugin*, int32_t index);
VGN_EXPORT void vgn_param_set(vgn_plugin*, int32_t index, float normalized);
VGN_EXPORT void vgn_param_text(vgn_plugin*, int32_t index, float normalized, char* buf, int32_t bufSize);

// Programs (JUCE AudioProcessor programs = the plugin's factory presets).
VGN_EXPORT int32_t vgn_program_count(vgn_plugin*);
VGN_EXPORT int32_t vgn_program_get(vgn_plugin*);
VGN_EXPORT void vgn_program_name(vgn_plugin*, int32_t index, char* buf, int32_t bufSize);
VGN_EXPORT void vgn_program_set(vgn_plugin*, int32_t index);

// State: returns a malloc'd blob (free with vgn_free) and its size.
VGN_EXPORT void* vgn_get_state(vgn_plugin*, int32_t* size);
VGN_EXPORT int vgn_set_state(vgn_plugin*, const void* data, int32_t size);
VGN_EXPORT void vgn_free(void*);

// Editor: attaches the plugin's own editor (JUCE AudioProcessorEditor) into `viewGroup`, scaled so
// it fits availW x availH physical pixels but never below its logical size (the caller scales a
// larger view down as a whole). Returns 1 on success; *w, *h = the editor's size in
// physical pixels after scaling.
VGN_EXPORT int vgn_editor_open(vgn_plugin*, JNIEnv* env, jobject viewGroup, int32_t availW, int32_t availH,
                               int32_t* w, int32_t* h);
VGN_EXPORT void vgn_editor_close(vgn_plugin*);

#ifdef __cplusplus
}
#endif
