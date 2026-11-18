// SPDX-License-Identifier: GPL-3.0-or-later
//
// ipc_sim: plays the Android engine's role on a Linux PC, against the real bridge host running
// under Wine. Tests the shared-memory protocol, futex wakeups from PE code, control commands,
// and real-time streaming, before anything runs on the phone.
//
// Build: g++ -O2 -std=c++20 -I shared/ipc tools/ipc_sim.cpp shared/ipc/bridge_client.cpp -o ipc_sim
// Run:   ipc_sim --shm /tmp/x.shm --block 256 --fill 2 --secs 10 --out o.wav -- wine host.exe serve P --shm /tmp/x.shm
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

#include "bridge_client.h"

using namespace vg;

static void writeWav(const char* path, const std::vector<float>& l, const std::vector<float>& r, int rate) {
    FILE* f = fopen(path, "wb");
    if (!f) return;
    uint32_t n = (uint32_t)l.size(), data = n * 8;
    auto u32 = [&](uint32_t v) { fwrite(&v, 4, 1, f); };
    auto u16 = [&](uint16_t v) { fwrite(&v, 2, 1, f); };
    fwrite("RIFF", 1, 4, f); u32(36 + data); fwrite("WAVEfmt ", 1, 8, f); u32(16);
    u16(3); u16(2); u32(rate); u32(rate * 8); u16(8); u16(32);
    fwrite("data", 1, 4, f); u32(data);
    for (uint32_t i = 0; i < n; ++i) { fwrite(&l[i], 4, 1, f); fwrite(&r[i], 4, 1, f); }
    fclose(f);
}

int main(int argc, char** argv) {
    std::string shm = "/tmp/vstghost.shm", out;
    uint32_t block = 256, fillBlocks = 2, rate = 48000;
    double secs = 10;
    int cmdStart = -1;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--") { cmdStart = i + 1; break; }
        if (a == "--shm") shm = argv[++i];
        else if (a == "--block") block = (uint32_t)atoi(argv[++i]);
        else if (a == "--fill") fillBlocks = (uint32_t)atoi(argv[++i]);
        else if (a == "--secs") secs = atof(argv[++i]);
        else if (a == "--out") out = argv[++i];
    }
    if (cmdStart < 0 || cmdStart >= argc) { fprintf(stderr, "usage: ipc_sim [opts] -- <host command>\n"); return 1; }

    BridgeClient bc;
    std::string err;
    if (!bc.create(shm, rate, block, block * fillBlocks, err)) { fprintf(stderr, "create: %s\n", err.c_str()); return 1; }

    pid_t child = fork();
    if (child == 0) { execvp(argv[cmdStart], argv + cmdStart); perror("exec"); _exit(127); }

    timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    bool up = bc.waitForState(VG_HOST_RUNNING, 120000);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double startMs = (t1.tv_sec - t0.tv_sec) * 1e3 + (t1.tv_nsec - t0.tv_nsec) / 1e6;
    printf("host running=%d after %.0f ms, wakeMode=%u (1=futex 2=poll), hostPid=%u\n", up, startMs,
           bc.ipc()->wakeMode, bc.ipc()->hostPid);
    if (!up) { printf("resp: %s\n", bc.ipc()->resp); kill(child, SIGKILL); return 2; }

    std::string resp;
    for (const char* req : {"ping", "info"}) {
        clock_gettime(CLOCK_MONOTONIC, &t0);
        bool ok = bc.request(req, resp, 5000);
        clock_gettime(CLOCK_MONOTONIC, &t1);
        printf("%s: ok=%d %.2f ms  %s\n", req, ok, (t1.tv_sec - t0.tv_sec) * 1e3 + (t1.tv_nsec - t0.tv_nsec) / 1e6, resp.substr(0, 300).c_str());
    }
    bc.request("params", resp, 10000);
    printf("params: %zu bytes of JSON\n", resp.size());
    unsigned firstId = 0;
    if (auto p = resp.find("\"id\":"); p != std::string::npos) firstId = (unsigned)strtoul(resp.c_str() + p + 5, nullptr, 10);
    bc.request("set\n" + std::to_string(firstId) + "\n0.5", resp, 5000);
    printf("set: %s\n", resp.c_str());
    bc.request("get_state", resp, 10000);
    printf("get_state: %zu bytes\n", resp.size());

    // Real-time consumer: one block per period, chords every 0.5 s, a latency probe every second.
    std::vector<float> L, R, bl(block), br(block);
    const uint64_t blocks = (uint64_t)(secs * rate / block);
    const int64_t periodNs = (int64_t)block * 1000000000LL / rate;
    timespec next;
    clock_gettime(CLOCK_MONOTONIC, &next);
    std::vector<double> lat;
    uint32_t minFill = UINT32_MAX;
    const uint8_t chord[] = {48, 55, 60, 64};
    int probing = 0;
    for (uint64_t b = 0; b < blocks; ++b) {
        next.tv_nsec += periodNs;
        while (next.tv_nsec >= 1000000000) { next.tv_nsec -= 1000000000; next.tv_sec++; }
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, nullptr);
        minFill = std::min(minFill, bc.fill());
        bc.read(bl.data(), br.data(), block);
        bc.wakeHost();
        L.insert(L.end(), bl.begin(), bl.end());
        R.insert(R.end(), br.begin(), br.end());
        double t = (double)b * block / rate;
        uint64_t blocksPerHalf = rate / 2 / block;
        if (b % (2 * blocksPerHalf) == 0 && b > 0) {            // every second: all off, then probe
            for (uint8_t n : chord) bc.sendMidi(0x80, n, 0);
            bc.sendMidi(0x80, 84, 0);
            probing = 1;
        } else if (probing == 1 && b % (2 * blocksPerHalf) == blocksPerHalf / 2) {
            bc.startProbe(84, 127, -50.0f);                      // after 0.25 s of release
            probing = 2;
        } else if (b % (2 * blocksPerHalf) == blocksPerHalf) {
            int64_t fr;
            if (probing == 2 && bc.probeResult(fr)) lat.push_back(fr * 1000.0 / rate);
            probing = 0;
            bc.sendMidi(0x80, 84, 0);
            for (uint8_t n : chord) bc.sendMidi(0x90, n, 90);
        }
        (void)t;
    }
    bc.requestStop();
    int status = 0;
    for (int i = 0; i < 100 && waitpid(child, &status, WNOHANG) == 0; ++i) usleep(50000);
    if (waitpid(child, &status, WNOHANG) == 0) { kill(child, SIGKILL); waitpid(child, &status, 0); }

    vg_ipc* s = bc.ipc();
    std::sort(lat.begin(), lat.end());
    double peak = 0;
    for (float v : L) peak = std::max(peak, (double)std::fabs(v));
    printf("streamed %.1f s, block %u, fill %u blocks: underruns=%llu minFill=%u frames, host blocks=%llu, "
           "avg render %.3f ms, max %.3f ms (budget %.3f ms), peak %.1f dBFS\n",
           secs, block, fillBlocks, (unsigned long long)bc.underruns(), minFill,
           (unsigned long long)s->blocksRendered, s->blocksRendered ? s->renderNsTotal / 1e6 / s->blocksRendered : 0.0,
           s->maxBlockNs / 1e6, block * 1000.0 / rate, peak > 0 ? 20 * log10(peak) : -999.0);
    printf("probe latency (note-on -> output onset, ms): n=%zu", lat.size());
    if (!lat.empty()) printf(" min %.2f median %.2f max %.2f", lat.front(), lat[lat.size() / 2], lat.back());
    printf("\nhost exit status %d, final state %u\n", WIFEXITED(status) ? WEXITSTATUS(status) : -1, s->hostState);
    if (!out.empty()) writeWav(out.c_str(), L, R, (int)rate);
    return 0;
}
