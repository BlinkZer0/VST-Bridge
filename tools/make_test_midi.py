#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-VstGhost-Proprietary
"""Writes the Phase 1 test MIDI file (format 0, 120 BPM, ~20 s).

Sections (seconds):
   0-4   C major scale up/down in eighths, velocity ramp 30 -> 127   (velocity response)
   4-12  Cmaj7 / Am9 / Dm7 / G13, keys held 1 s, sustain pedal 1.9 s (CC64 via IMidiMapping)
  12-14  sixteenths alternating C2 / C6                              (retrigger, range)
  14-18  held A3 with pitch-bend sweep and mod-wheel ramp            (CC1, pitch bend)
  18-20  wide C major chord                                          (polyphony, release tail)
Usage: make_test_midi.py <out.mid>
"""
import struct
import sys

PPQ = 480
SEC = PPQ * 2  # ticks per second at 120 BPM

events = []  # (tick, order, bytes)


def ev(t_sec, *data):
    events.append((round(t_sec * SEC), len(events), bytes(data)))


def note(t, dur, pitch, vel):
    ev(t, 0x90, pitch, vel)
    ev(t + dur, 0x80, pitch, 0)


# 1. scale with velocity ramp
scale = [60, 62, 64, 65, 67, 69, 71, 72, 71, 69, 67, 65, 64, 62, 60, 60]
for i, p in enumerate(scale):
    note(i * 0.25, 0.23, p, 30 + round(97 * i / (len(scale) - 1)))

# 2. chords with sustain pedal
chords = [[48, 52, 55, 59, 64], [45, 52, 55, 59, 71], [50, 53, 57, 60, 65], [43, 53, 57, 59, 64]]
for k, ch in enumerate(chords):
    t = 4 + 2 * k
    ev(t, 0xB0, 64, 127)
    for j, p in enumerate(ch):
        note(t + 0.01 * j, 1.0, p, 80 + 8 * j)
    ev(t + 1.9, 0xB0, 64, 0)

# 3. fast repeated notes, low/high
for i in range(16):
    note(12 + i * 0.125, 0.1, 36 if i % 2 == 0 else 84, 100)

# 4. pitch bend + mod wheel on a held note
note(14, 4.0, 57, 100)
steps = 200
for i in range(steps + 1):
    t = 14 + 4.0 * i / steps
    x = i / steps
    bend = 8192 + round(8191 * (x * 2 if x < 0.5 else 2 - x * 2))  # up to max and back
    bend = min(bend, 16383)
    ev(t, 0xE0, bend & 0x7F, bend >> 7)
    ev(t, 0xB0, 1, round(127 * x))
ev(18, 0xE0, 0x00, 0x40)
ev(18, 0xB0, 1, 0)

# 5. final chord
for j, p in enumerate([36, 43, 48, 52, 55, 60, 64]):
    note(18, 2.0, p, 90)


def vlq(n):
    out = [n & 0x7F]
    n >>= 7
    while n:
        out.insert(0, 0x80 | (n & 0x7F))
        n >>= 7
    return bytes(out)


events.sort(key=lambda e: (e[0], e[1]))
track = bytearray()
track += b"\x00\xFF\x51\x03" + (500000).to_bytes(3, "big")  # 120 BPM
track += b"\x00\xFF\x58\x04\x04\x02\x18\x08"                  # 4/4
prev = 0
for tick, _, data in events:
    track += vlq(tick - prev) + data
    prev = tick
track += b"\x00\xFF\x2F\x00"

with open(sys.argv[1], "wb") as f:
    f.write(b"MThd" + struct.pack(">IHHH", 6, 0, 1, PPQ))
    f.write(b"MTrk" + struct.pack(">I", len(track)) + track)
print(f"wrote {sys.argv[1]}: {len(events)} events, {prev / SEC:.1f} s")
