// SPDX-License-Identifier: GPL-3.0-or-later
// Standard MIDI File (format 0/1) reader: merges all tracks and converts ticks to seconds
// using the file's tempo map.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace vg {

struct MidiEvent {
    double timeSec;
    uint8_t status;  // includes channel
    uint8_t d1, d2;
};

// Returns events sorted by time (channel-voice messages only). err is set on failure.
std::vector<MidiEvent> readMidiFile(const std::wstring& path, std::string& err);

}  // namespace vg
