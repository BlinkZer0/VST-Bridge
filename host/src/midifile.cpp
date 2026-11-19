// SPDX-License-Identifier: LicenseRef-VstGhost-Proprietary
#include "midifile.h"

#include <algorithm>
#include <cstdio>
#include <map>

namespace vg {
namespace {

struct Reader {
    const std::vector<uint8_t>& d;
    size_t p, end;
    bool ok = true;
    uint8_t u8() { if (p >= end) { ok = false; return 0; } return d[p++]; }
    uint32_t be(int n) { uint32_t v = 0; while (n--) v = (v << 8) | u8(); return v; }
    uint32_t vlq() { uint32_t v = 0; for (int i = 0; i < 4; ++i) { uint8_t b = u8(); v = (v << 7) | (b & 0x7f); if (!(b & 0x80)) break; } return v; }
};

struct TickEvent { uint64_t tick; int order; uint8_t status, d1, d2; };

}  // namespace

std::vector<MidiEvent> readMidiFile(const std::wstring& path, std::string& err) {
    std::vector<MidiEvent> out;
    FILE* f = _wfopen(path.c_str(), L"rb");
    if (!f) { err = "cannot open MIDI file"; return out; }
    std::vector<uint8_t> data;
    uint8_t buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) data.insert(data.end(), buf, buf + n);
    fclose(f);

    Reader r{data, 0, data.size()};
    if (r.be(4) != 0x4D546864 /* MThd */) { err = "not a Standard MIDI File"; return out; }
    uint32_t hlen = r.be(4);
    uint16_t format = (uint16_t)r.be(2), ntracks = (uint16_t)r.be(2), division = (uint16_t)r.be(2);
    r.p += hlen - 6;
    if (division & 0x8000) { err = "SMPTE time division not supported"; return out; }
    if (format > 1) { err = "MIDI format 2 not supported"; return out; }

    std::vector<TickEvent> evs;
    std::map<uint64_t, uint32_t> tempo;  // tick -> microseconds per quarter
    tempo[0] = 500000;
    int order = 0;
    for (int t = 0; t < ntracks && r.ok; ++t) {
        uint32_t id = r.be(4), len = r.be(4);
        size_t trackEnd = r.p + len;
        if (id != 0x4D54726B /* MTrk */) { r.p = trackEnd; continue; }
        Reader tr{data, r.p, std::min(trackEnd, data.size())};
        uint64_t tick = 0;
        uint8_t running = 0;
        while (tr.ok && tr.p < tr.end) {
            tick += tr.vlq();
            uint8_t b = tr.u8();
            if (b == 0xFF) {  // meta
                uint8_t type = tr.u8();
                uint32_t mlen = tr.vlq();
                if (type == 0x51 && mlen == 3) tempo[tick] = tr.be(3);
                else tr.p += mlen;
                if (type == 0x2F) break;
                continue;
            }
            if (b == 0xF0 || b == 0xF7) { tr.p += tr.vlq(); continue; }  // sysex
            uint8_t status, d1;
            if (b & 0x80) { status = b; running = b; d1 = tr.u8(); }
            else { status = running; d1 = b; }
            if (!status) { err = "corrupt track (data byte without status)"; return out; }
            uint8_t type = status & 0xF0, d2 = 0;
            if (type != 0xC0 && type != 0xD0) d2 = tr.u8();
            evs.push_back({tick, order++, status, d1, d2});
        }
        r.p = trackEnd;
    }
    if (!r.ok) { err = "truncated MIDI file"; return out; }

    std::stable_sort(evs.begin(), evs.end(), [](const TickEvent& a, const TickEvent& b) {
        return a.tick != b.tick ? a.tick < b.tick : a.order < b.order;
    });
    // Tick -> seconds via the tempo map.
    for (const auto& e : evs) {
        double sec = 0;
        uint64_t prevTick = 0;
        uint32_t us = 500000;
        for (const auto& [tk, t] : tempo) {
            if (tk >= e.tick) break;
            sec += (double)(tk - prevTick) * us / 1e6 / division;
            prevTick = tk;
            us = t;
        }
        sec += (double)(e.tick - prevTick) * us / 1e6 / division;
        out.push_back({sec, e.status, e.d1, e.d2});
    }
    return out;
}

}  // namespace vg
