// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <vector>

namespace vg {

// Writes non-interleaved float channels as a 32-bit float WAV.
bool writeWavFloat(const std::wstring& path, const std::vector<std::vector<float>>& ch, int sampleRate);

// Reads 16/24/32-bit PCM or 32-bit float WAV into float channels.
bool readWav(const std::wstring& path, std::vector<std::vector<float>>& ch, int& sampleRate, std::string& err);

}  // namespace vg
