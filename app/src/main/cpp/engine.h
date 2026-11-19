// SPDX-License-Identifier: LicenseRef-VstGhost-Proprietary
//
// Multi-slot real-time engine. Each slot owns one bridge (one plugin host process).
//   slot 0     : the app's own player
//   slots 1..4 : AAP / virtual-MIDI instrument slots
//   slots 5..8 : AAP effect slots
// A slot's audio goes either to the app's Oboe output (mixed with the other slots) or, while an
// AAP host has it instantiated, to that host's process() call.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "bridge_client.h"
#include "native_host.h"

namespace vg {

constexpr int kSlots = 9;

// Native plugin of a slot (may be null): when set, it replaces the bridge.
std::shared_ptr<NativeInstance> slotNative(int slot);
// Bridge of a slot (may be null). Lock-free read.
std::shared_ptr<BridgeClient> slotBridge(int slot);
// Route a slot's audio to an AAP host (true) or to the Oboe mix (false).
void setSlotAapOwned(int slot, bool owned);
// Grows the slot's target buffer so a consumer reading `frames` at a time never runs short
// (+ self-healing growth after underruns). Call from the consumer's audio thread.
void ensureSlotFill(int slot, BridgeClient& b, uint32_t frames, uint32_t routeBufferFrames);

}  // namespace vg
