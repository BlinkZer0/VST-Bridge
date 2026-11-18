#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compare two float32 WAVs (e.g. device render vs PC reference). No dependencies.

Prints: identical?, max |diff|, diff RMS in dB relative to the reference, and
correlation. Usage: compare_wav.py <reference.wav> <test.wav>
"""
import math
import struct
import sys


def read_wav(path):
    with open(path, "rb") as f:
        data = f.read()
    assert data[:4] == b"RIFF" and data[8:12] == b"WAVE", "not a WAV"
    p, fmt, ch, rate, bits, samples = 12, None, None, None, None, None
    while p + 8 <= len(data):
        cid, size = data[p:p + 4], struct.unpack_from("<I", data, p + 4)[0]
        body = data[p + 8:p + 8 + size]
        if cid == b"fmt ":
            fmt, ch, rate = struct.unpack_from("<HHI", body)
            bits = struct.unpack_from("<H", body, 14)[0]
        elif cid == b"data":
            assert fmt == 3 and bits == 32, "expects 32-bit float WAV"
            samples = struct.unpack(f"<{len(body) // 4}f", body)
        p += 8 + size + (size & 1)
    return ch, rate, samples


def main(ref_path, test_path):
    ch_a, rate_a, a = read_wav(ref_path)
    ch_b, rate_b, b = read_wav(test_path)
    if (ch_a, rate_a) != (ch_b, rate_b):
        print(f"format differs: {ch_a}ch/{rate_a} vs {ch_b}ch/{rate_b}")
    n = min(len(a), len(b))
    if len(a) != len(b):
        print(f"length differs: {len(a)} vs {len(b)} samples (comparing first {n})")
    identical = len(a) == len(b) and a == b
    max_diff = max((abs(a[i] - b[i]) for i in range(n)), default=0.0)
    e_ref = sum(x * x for x in a[:n])
    e_diff = sum((a[i] - b[i]) ** 2 for i in range(n))
    e_b = sum(x * x for x in b[:n])
    dot = sum(a[i] * b[i] for i in range(n))
    corr = dot / math.sqrt(e_ref * e_b) if e_ref > 0 and e_b > 0 else float("nan")
    rel_db = 10 * math.log10(e_diff / e_ref) if e_diff > 0 and e_ref > 0 else float("-inf")
    print(f"identical={identical} max_abs_diff={max_diff:.3g} diff_vs_ref={rel_db:.1f} dB correlation={corr:.6f}")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
