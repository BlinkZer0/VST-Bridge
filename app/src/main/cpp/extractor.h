// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace vg {

struct ExtractStats {
    uint64_t files = 0, dirs = 0, symlinks = 0, hardlinks = 0;
    uint64_t bytesWritten = 0;
    uint64_t interpRewrites = 0, shebangRewrites = 0, symlinkRewrites = 0;
    double elapsedMs = 0;
};

struct ExtractOptions {
    int fd = -1;              // archive (tar.zst) file descriptor
    int64_t offset = 0;       // start of archive within fd (APK assets)
    int64_t length = 0;       // archive length in bytes
    std::string destDir;      // absolute; created if missing
    std::string placeholder;  // build-time prefix baked into the payload
    std::string realRoot;     // what the placeholder becomes on this device
    // Progress: (compressed bytes consumed, total). Return false to cancel.
    std::function<bool(int64_t, int64_t)> progress;
};

// Returns an empty string on success, otherwise an error message.
std::string extractTarZst(const ExtractOptions& opt, ExtractStats& stats);

}  // namespace vg
