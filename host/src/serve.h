// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>

namespace vg {
// Live mode: serve one plugin over the shared-memory protocol in shared/ipc/vg_ipc.h.
// pattern: no plugin; write frame index as samples (ring integrity test).
int cmdServe(const std::wstring& pluginPath, const std::wstring& shmPath, bool quiet, bool pattern);
}
