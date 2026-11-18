/*
 * VST gHost: shared-memory layout between the arm64 audio engine (Android, NDK) and the
 * bridge host (x86_64 Windows exe under Wine + Box64).
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * One file (files/ipc/<slot>.shm) is mmap'd MAP_SHARED by both processes. Positions are
 * monotonic 64-bit counters; ring index = pos & (size - 1). Single producer / single consumer
 * per ring. Every index access uses __atomic builtins with SEQ_CST: on the x86_64 side that
 * compiles to xchg/lock-prefixed instructions, which Box64 translates with real barriers.
 * Plain x86 acquire/release would compile to ordinary moves that ARM may reorder.
 *
 * Wakeups use Linux futexes on 32-bit words in this mapping (not FUTEX_PRIVATE: two
 * processes). The x86_64 side issues the syscall directly; Box64 translates it.
 *
 * Audio ring  (host -> engine): rendered output, host keeps `targetFill` frames buffered.
 * Input ring  (engine -> host): effect input.
 * MIDI ring   (engine -> host): events, rendered at the start of the next block.
 * Control     (engine <-> host): one request/response text mailbox (JSON), main thread.
 */
#ifndef VSTGHOST_IPC_H
#define VSTGHOST_IPC_H

#include <stdint.h>

#define VG_IPC_MAGIC 0x50494756u /* 'VGIP' */
#define VG_IPC_VERSION 1u
#define VG_IPC_CHANNELS 2
#define VG_IPC_RING_FRAMES 8192u /* power of two */
#define VG_IPC_MIDI_SIZE 1024u   /* power of two */
#define VG_IPC_REQ_BYTES 65536u
#define VG_IPC_RESP_BYTES 524288u

/* hostState */
#define VG_HOST_STARTING 0u
#define VG_HOST_LOADING 1u
#define VG_HOST_RUNNING 2u
#define VG_HOST_FAILED 3u
#define VG_HOST_EXITED 4u

/* wakeMode reported by the host */
#define VG_WAKE_FUTEX 1u
#define VG_WAKE_POLL 2u

typedef struct {
    uint8_t status, d1, d2, pad;
    uint32_t seq; /* engine-side sequence number (latency probes) */
} vg_midi_event;

typedef struct {
    /* ---- written once by the engine before starting the host */
    uint32_t magic, version;
    uint32_t sampleRate, blockSize;
    uint32_t channels, inChannels;
    uint32_t ringFrames, midiSize;

    /* ---- host status (host writes) */
    uint32_t hostState;
    uint32_t wakeMode;
    uint32_t hostPid;
    uint32_t pad0;

    /* ---- futex words */
    uint32_t hostWake;   /* engine bumps + FUTEX_WAKE when it consumed audio or queued MIDI */
    uint32_t ctrlWake;   /* engine bumps + FUTEX_WAKE when a control request is posted */
    uint32_t engineWake; /* host bumps + FUTEX_WAKE when a control response is ready */
    uint32_t stopFlag;   /* engine sets 1 to ask the host to exit */

    /* ---- audio out ring (host produces, engine consumes) */
    uint64_t outWrite, outRead;
    /* ---- effect input ring (engine produces, host consumes) */
    uint64_t inWrite, inRead;
    /* ---- MIDI ring (engine produces, host consumes) */
    uint64_t midiWrite, midiRead;

    /* ---- engine -> host settings */
    uint32_t targetFill; /* frames the host keeps buffered ahead of the engine */
    uint32_t pad1;

    /* ---- host stats */
    uint64_t blocksRendered;
    uint64_t renderNsTotal;
    uint32_t lastBlockNs, maxBlockNs;
    uint64_t lastProbeSeq;   /* seq of the last MIDI event rendered */
    uint64_t lastProbeFrame; /* outWrite position of the block it was rendered in */

    /* ---- control mailbox */
    uint32_t reqSeq, respSeq;
    uint32_t reqLen, respLen;
    char req[VG_IPC_REQ_BYTES];
    char resp[VG_IPC_RESP_BYTES];

    /* ---- rings */
    vg_midi_event midi[VG_IPC_MIDI_SIZE];
    float out[VG_IPC_CHANNELS][VG_IPC_RING_FRAMES];
    float in[VG_IPC_CHANNELS][VG_IPC_RING_FRAMES];
} vg_ipc;

#define VG_LOAD(p) __atomic_load_n((p), __ATOMIC_SEQ_CST)
#define VG_STORE(p, v) __atomic_store_n((p), (v), __ATOMIC_SEQ_CST)
#define VG_ADD(p, v) __atomic_add_fetch((p), (v), __ATOMIC_SEQ_CST)

#ifdef __cplusplus
#include <cstddef>
static_assert(sizeof(vg_midi_event) == 8, "layout");
static_assert(offsetof(vg_ipc, hostWake) == 48, "layout");
static_assert(offsetof(vg_ipc, outWrite) == 64, "layout");
static_assert(offsetof(vg_ipc, targetFill) == 112, "layout");
static_assert(offsetof(vg_ipc, reqSeq) == 160, "layout");
static_assert(offsetof(vg_ipc, req) == 176, "layout");
static_assert(sizeof(vg_ipc) == 176 + VG_IPC_REQ_BYTES + VG_IPC_RESP_BYTES + 8 * VG_IPC_MIDI_SIZE +
                                    2 * 4 * VG_IPC_CHANNELS * VG_IPC_RING_FRAMES, "layout");
#endif

#endif
