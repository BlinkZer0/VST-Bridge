// SPDX-License-Identifier: LicenseRef-VstGhost-Proprietary
#include "wav.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace vg {
namespace {
void put32(FILE* f, uint32_t v) { fwrite(&v, 4, 1, f); }
void put16(FILE* f, uint16_t v) { fwrite(&v, 2, 1, f); }
}  // namespace

bool writeWavFloat(const std::wstring& path, const std::vector<std::vector<float>>& ch, int sampleRate) {
    FILE* f = _wfopen(path.c_str(), L"wb");
    if (!f) return false;
    const uint16_t nch = (uint16_t)ch.size();
    const uint32_t frames = nch ? (uint32_t)ch[0].size() : 0;
    const uint32_t dataBytes = frames * nch * 4;
    fwrite("RIFF", 1, 4, f); put32(f, 4 + (8 + 18) + (8 + 4) + (8 + dataBytes)); fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f); put32(f, 18);
    put16(f, 3 /* IEEE float */); put16(f, nch); put32(f, (uint32_t)sampleRate);
    put32(f, (uint32_t)sampleRate * nch * 4); put16(f, (uint16_t)(nch * 4)); put16(f, 32); put16(f, 0);
    fwrite("fact", 1, 4, f); put32(f, 4); put32(f, frames);
    fwrite("data", 1, 4, f); put32(f, dataBytes);
    std::vector<float> inter((size_t)frames * nch);
    for (uint32_t i = 0; i < frames; ++i)
        for (uint16_t c = 0; c < nch; ++c) inter[(size_t)i * nch + c] = ch[c][i];
    fwrite(inter.data(), 4, inter.size(), f);
    return fclose(f) == 0;
}

bool readWav(const std::wstring& path, std::vector<std::vector<float>>& ch, int& sampleRate, std::string& err) {
    FILE* f = _wfopen(path.c_str(), L"rb");
    if (!f) { err = "cannot open input WAV"; return false; }
    char id[4]; uint32_t size;
    if (fread(id, 1, 4, f) != 4 || memcmp(id, "RIFF", 4) || fread(&size, 4, 1, f) != 1 ||
        fread(id, 1, 4, f) != 4 || memcmp(id, "WAVE", 4)) { fclose(f); err = "not a WAV file"; return false; }
    uint16_t fmt = 0, nch = 0, bits = 0;
    uint32_t rate = 0;
    std::vector<uint8_t> data;
    while (fread(id, 1, 4, f) == 4 && fread(&size, 4, 1, f) == 1) {
        if (!memcmp(id, "fmt ", 4)) {
            std::vector<uint8_t> b(size);
            if (fread(b.data(), 1, size, f) != size) break;
            memcpy(&fmt, &b[0], 2); memcpy(&nch, &b[2], 2); memcpy(&rate, &b[4], 4); memcpy(&bits, &b[14], 2);
            if (fmt == 0xFFFE && size >= 26) memcpy(&fmt, &b[24], 2);  // WAVE_FORMAT_EXTENSIBLE subformat
        } else if (!memcmp(id, "data", 4)) {
            data.resize(size);
            data.resize(fread(data.data(), 1, size, f));
        } else {
            fseek(f, (long)(size + (size & 1)), SEEK_CUR);
        }
    }
    fclose(f);
    if (!nch || data.empty()) { err = "WAV has no fmt/data"; return false; }
    const int bps = bits / 8;
    const size_t frames = data.size() / ((size_t)bps * nch);
    ch.assign(nch, std::vector<float>(frames));
    for (size_t i = 0; i < frames; ++i)
        for (int c = 0; c < nch; ++c) {
            const uint8_t* s = &data[(i * nch + c) * bps];
            float v;
            if (fmt == 3 && bits == 32) memcpy(&v, s, 4);
            else if (fmt == 1 && bits == 16) v = (int16_t)(s[0] | s[1] << 8) / 32768.0f;
            else if (fmt == 1 && bits == 24) v = (float)((int32_t)((uint32_t)s[0] << 8 | (uint32_t)s[1] << 16 | (uint32_t)s[2] << 24) >> 8) / 8388608.0f;
            else if (fmt == 1 && bits == 32) { int32_t x; memcpy(&x, s, 4); v = (float)x / 2147483648.0f; }
            else { err = "unsupported WAV sample format"; return false; }
            ch[c][i] = v;
        }
    sampleRate = (int)rate;
    return true;
}

}  // namespace vg
