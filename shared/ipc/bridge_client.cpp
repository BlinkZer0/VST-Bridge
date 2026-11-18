// SPDX-License-Identifier: GPL-3.0-or-later
#include "bridge_client.h"

#include <fcntl.h>
#include <linux/futex.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstring>
#include <ctime>
#include <thread>

namespace vg {

long futexWake(uint32_t* addr, int n) { return syscall(SYS_futex, addr, FUTEX_WAKE, n, nullptr, nullptr, 0); }

long futexWait(uint32_t* addr, uint32_t expected, int64_t timeoutNs) {
    timespec ts{(time_t)(timeoutNs / 1000000000), (long)(timeoutNs % 1000000000)};
    return syscall(SYS_futex, addr, FUTEX_WAIT, expected, &ts, nullptr, 0);
}

bool BridgeClient::create(const std::string& path, uint32_t rate, uint32_t block, uint32_t targetFill, std::string& err) {
    close();
    size_ = (sizeof(vg_ipc) + 4095) & ~size_t(4095);
    fd_ = ::open(path.c_str(), O_RDWR | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd_ < 0) { err = "open " + path + ": " + strerror(errno); return false; }
    if (ftruncate(fd_, (off_t)size_) != 0) { err = std::string("ftruncate: ") + strerror(errno); return false; }
    void* p = mmap(nullptr, size_, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
    if (p == MAP_FAILED) { err = std::string("mmap: ") + strerror(errno); return false; }
    s_ = static_cast<vg_ipc*>(p);
    memset(s_, 0, sizeof(vg_ipc));
    s_->version = VG_IPC_VERSION;
    s_->sampleRate = rate;
    s_->blockSize = block;
    s_->channels = VG_IPC_CHANNELS;
    s_->inChannels = VG_IPC_CHANNELS;
    s_->ringFrames = VG_IPC_RING_FRAMES;
    s_->midiSize = VG_IPC_MIDI_SIZE;
    s_->targetFill = std::min(targetFill, VG_IPC_RING_FRAMES - block);
    VG_STORE(&s_->hostState, VG_HOST_STARTING);
    VG_STORE(&s_->magic, VG_IPC_MAGIC);  // last: header complete
    underruns_ = 0;
    framesRead_ = 0;
    primed_ = false;
    return true;
}

void BridgeClient::close() {
    if (s_) munmap(s_, size_);
    if (fd_ >= 0) ::close(fd_);
    s_ = nullptr;
    fd_ = -1;
}

uint32_t BridgeClient::fill() const {
    if (!s_) return 0;
    return (uint32_t)(VG_LOAD(&s_->outWrite) - VG_LOAD(&s_->outRead));
}

uint32_t BridgeClient::read(float* l, float* r, uint32_t frames) {
    if (!s_) { memset(l, 0, frames * 4); memset(r, 0, frames * 4); return 0; }
    const uint64_t rd = VG_LOAD(&s_->outRead), wr = VG_LOAD(&s_->outWrite);
    const uint32_t avail = (uint32_t)std::min<uint64_t>(wr - rd, frames);
    const uint32_t mask = VG_IPC_RING_FRAMES - 1;
    for (uint32_t i = 0; i < avail; ++i) {
        l[i] = s_->out[0][(rd + i) & mask];
        r[i] = s_->out[1][(rd + i) & mask];
    }
    // "Primed" once the ring first reaches its target: the empty ring right after a host starts
    // is expected and must not count as an underrun (it would also inflate the adaptive buffer).
    if (!primed_ && wr - rd >= VG_LOAD(&s_->targetFill)) primed_ = true;
    if (avail < frames) {
        memset(l + avail, 0, (frames - avail) * 4);
        memset(r + avail, 0, (frames - avail) * 4);
        if (primed_ && VG_LOAD(&s_->hostState) == VG_HOST_RUNNING) underruns_.fetch_add(1);
    }
    // Latency probe: first frame whose level crosses the threshold after the note was sent.
    if (probeState_.load(std::memory_order_acquire) == 1) {
        for (uint32_t i = 0; i < avail; ++i) {
            if (std::fabs(l[i]) > probeThreshold_ || std::fabs(r[i]) > probeThreshold_) {
                probeFrames_.store((int64_t)(framesRead_.load() + i) - (int64_t)probeStart_.load());
                probeState_.store(2, std::memory_order_release);
                break;
            }
        }
    }
    if (verify_.load(std::memory_order_relaxed)) {
        uint64_t bad = 0;
        for (uint32_t i = 0; i < avail; ++i) {
            const float want = (float)((rd + i) & 0xFFFFFF);
            bad += (l[i] != want) + (r[i] != want);
        }
        mismatches_.fetch_add(bad);
        verified_.fetch_add(avail * 2ull);
    }
    VG_STORE(&s_->outRead, rd + avail);
    framesRead_.fetch_add(frames);
    return avail;
}

void BridgeClient::writeInput(const float* l, const float* r, uint32_t frames) {
    if (!s_) return;
    const uint64_t w = VG_LOAD(&s_->inWrite), rd = VG_LOAD(&s_->inRead);
    const uint32_t space = (uint32_t)(VG_IPC_RING_FRAMES - (w - rd));
    const uint32_t n = std::min(space, frames);
    const uint32_t mask = VG_IPC_RING_FRAMES - 1;
    for (uint32_t i = 0; i < n; ++i) { s_->in[0][(w + i) & mask] = l[i]; s_->in[1][(w + i) & mask] = r[i]; }
    VG_STORE(&s_->inWrite, w + n);
}

void BridgeClient::wakeHost() {
    if (!s_) return;
    VG_ADD(&s_->hostWake, 1u);
    futexWake(&s_->hostWake, 1);
}

bool BridgeClient::sendMidi(uint8_t st, uint8_t d1, uint8_t d2, uint32_t seq) {
    if (!s_) return false;
    std::lock_guard<std::mutex> g(midiMu_);
    const uint64_t w = VG_LOAD(&s_->midiWrite), rd = VG_LOAD(&s_->midiRead);
    if (w - rd >= VG_IPC_MIDI_SIZE) return false;
    vg_midi_event& e = s_->midi[w & (VG_IPC_MIDI_SIZE - 1)];
    e.status = st; e.d1 = d1; e.d2 = d2; e.pad = 0; e.seq = seq;
    VG_STORE(&s_->midiWrite, w + 1);
    wakeHost();
    return true;
}

void BridgeClient::setTargetFill(uint32_t frames) {
    if (s_) VG_STORE(&s_->targetFill, std::min(frames, VG_IPC_RING_FRAMES - s_->blockSize));
}

uint32_t BridgeClient::hostState() const { return s_ ? VG_LOAD(&s_->hostState) : VG_HOST_EXITED; }

bool BridgeClient::waitForState(uint32_t state, int timeoutMs) {
    auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < end) {
        uint32_t seen = VG_LOAD(&s_->engineWake);
        uint32_t st = hostState();
        if (st == state) return true;
        if (st == VG_HOST_FAILED || st == VG_HOST_EXITED) return false;
        futexWait(&s_->engineWake, seen, 20'000'000);
    }
    return false;
}

bool BridgeClient::request(const std::string& req, std::string& resp, int timeoutMs) {
    if (!s_ || req.size() > VG_IPC_REQ_BYTES) return false;
    std::lock_guard<std::mutex> g(ctrlMu_);
    memcpy(s_->req, req.data(), req.size());
    VG_STORE(&s_->reqLen, (uint32_t)req.size());
    const uint32_t seq = VG_LOAD(&s_->reqSeq) + 1;
    VG_STORE(&s_->reqSeq, seq);
    VG_ADD(&s_->ctrlWake, 1u);
    futexWake(&s_->ctrlWake, 1);
    auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < end) {
        uint32_t seen = VG_LOAD(&s_->engineWake);
        if (VG_LOAD(&s_->respSeq) == seq) {
            uint32_t len = std::min<uint32_t>(VG_LOAD(&s_->respLen), VG_IPC_RESP_BYTES);
            resp.assign(s_->resp, len);
            return true;
        }
        uint32_t st = hostState();
        if (st == VG_HOST_FAILED || st == VG_HOST_EXITED) return false;
        futexWait(&s_->engineWake, seen, 20'000'000);
    }
    return false;
}

void BridgeClient::requestStop() {
    if (!s_) return;
    VG_STORE(&s_->stopFlag, 1u);
    VG_ADD(&s_->ctrlWake, 1u);
    futexWake(&s_->ctrlWake, 8);
    wakeHost();
}

void BridgeClient::startProbe(uint8_t note, uint8_t velocity, float thresholdDb) {
    probeThreshold_ = std::pow(10.0f, thresholdDb / 20.0f);
    probeStart_.store(framesRead_.load());
    probeState_.store(1, std::memory_order_release);
    sendMidi(0x90, note, velocity, 1);
}

bool BridgeClient::probeResult(int64_t& frames) const {
    if (probeState_.load(std::memory_order_acquire) != 2) return false;
    frames = probeFrames_.load();
    return true;
}

}  // namespace vg
