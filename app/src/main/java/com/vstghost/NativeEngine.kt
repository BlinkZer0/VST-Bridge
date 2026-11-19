// SPDX-License-Identifier: GPL-3.0-or-later
package com.vstghost

/**
 * JNI surface of app/src/main/cpp/engine.cpp: Oboe output mixing up to [SLOTS] bridge slots.
 * Slot 0 = the app's player, 1..4 = instrument slots, 5..8 = effect slots (AAP / virtual MIDI).
 * Overloads without a slot argument act on slot 0.
 */
object NativeEngine {
    init { System.loadLibrary("vstghost") }

    const val SLOTS = 9
    const val HOST_STARTING = 0
    const val HOST_LOADING = 1
    const val HOST_RUNNING = 2
    const val HOST_FAILED = 3
    const val HOST_EXITED = 4

    @JvmStatic external fun startAudio(): Boolean
    @JvmStatic external fun stopAudio()
    @JvmStatic external fun setBufferBursts(n: Int)
    @JvmStatic external fun setGain(gain: Float)

    /** Creates the slot's shared-memory file. rate 0 = the Oboe stream's rate. Null on success. */
    @JvmStatic external fun createBridge(slot: Int, path: String, rate: Int, block: Int, fillFrames: Int): String?
    @JvmStatic external fun closeBridge(slot: Int)
    @JvmStatic external fun hostState(slot: Int): Int
    @JvmStatic external fun lastResponse(slot: Int): String?
    /** Blocking control request (see host/src/serve.cpp). Null on timeout/host failure. */
    @JvmStatic external fun request(slot: Int, req: String, timeoutMs: Int): String?
    @JvmStatic external fun sendMidi(slot: Int, status: Int, d1: Int, d2: Int): Boolean
    @JvmStatic external fun setTargetFill(slot: Int, frames: Int)
    /** 0 = silence, 1 = built-in test signal (effects played in the app). */
    @JvmStatic external fun setInputMode(slot: Int, mode: Int)
    /** 0 = instrument, 1 = effect: effects form a serial chain fed by the instruments (engine.cpp). */
    @JvmStatic external fun setSlotKind(slot: Int, kind: Int)
    /** What feeds the effect chain: 0 the instruments, 1 the built-in test signal. */
    @JvmStatic external fun setFxInput(mode: Int)
    /** True while an AAP host reads this slot's audio (it is then left out of the Oboe mix). */
    @JvmStatic external fun setAapOwned(slot: Int, owned: Boolean)
    @JvmStatic external fun startProbe(slot: Int, note: Int, velocity: Int, thresholdDb: Float)
    @JvmStatic external fun probeResult(slot: Int): Long
    @JvmStatic external fun setVerifyPattern(slot: Int, on: Boolean)
    @JvmStatic external fun stats(slot: Int): DoubleArray

    // ---- Phase 4: in-app Wayland compositor for plugin editors (app/src/main/cpp/wl_jni.cpp)
    /** Null on success. */
    @JvmStatic external fun wlStart(socketPath: String, width: Int, height: Int): String?
    /** See wl_jni.cpp: serial, `have` if unchanged, -1 = buffer too small, -2 = not running. */
    @JvmStatic external fun wlSnapshot(have: Long, buf: IntArray?, meta: IntArray): Long
    @JvmStatic external fun wlPointer(x: Float, y: Float, button: Int)
    @JvmStatic external fun wlScroll(notches: Float)
    @JvmStatic external fun wlStats(): String
    /** Test hook: current picture as BMP; returns "WxH" or null. */
    @JvmStatic external fun wlDump(path: String): String?

    // ---- Phase 5: native plugins (app/src/main/cpp/native_host.cpp). Main thread only.
    /** Library already loaded with System.loadLibrary. Returns its info JSON or "ERROR: ...". */
    @JvmStatic external fun nativeInit(lib: String, context: android.content.Context): String
    /** Null on success. rate 0 = the output stream's rate. */
    @JvmStatic external fun nativeOpen(slot: Int, lib: String, rate: Int, maxBlock: Int): String?
    @JvmStatic external fun nativeClose(slot: Int)
    @JvmStatic external fun isNative(slot: Int): Boolean
    /** Attaches the plugin's own editor into [parent], fitted to w x h px. Returns [w, h] or null. */
    @JvmStatic external fun nativeEditorOpen(slot: Int, parent: android.view.ViewGroup, w: Int, h: Int): IntArray?
    @JvmStatic external fun nativeEditorClose(slot: Int)
    @JvmStatic external fun resetStats(slot: Int)

    /**
     * The slot the on-screen keyboard and USB MIDI play: the player's selected instrument slot.
     * 0 (the test harness's slot) until the player picks one.
     */
    @Volatile var playerSlot = 0

    // ---- conveniences for the player slot (tests leave it at 0)
    fun hostState() = hostState(playerSlot)
    fun lastResponse() = lastResponse(playerSlot)
    fun request(req: String, timeoutMs: Int) = request(playerSlot, req, timeoutMs)
    fun sendMidi(status: Int, d1: Int, d2: Int) = sendMidi(playerSlot, status, d1, d2)
    fun setTargetFill(frames: Int) = setTargetFill(playerSlot, frames)
    fun setInputMode(mode: Int) = setInputMode(playerSlot, mode)
    fun startProbe(note: Int, velocity: Int, thresholdDb: Float) = startProbe(playerSlot, note, velocity, thresholdDb)
    fun probeResult() = probeResult(playerSlot)
    fun setVerifyPattern(on: Boolean) = setVerifyPattern(playerSlot, on)
    fun resetStats() = resetStats(playerSlot)
    fun statsNamed(slot: Int = playerSlot) = Stats(stats(slot))

    /** Named view of [stats]. */
    data class Stats(val v: DoubleArray) {
        val sampleRate get() = v[0].toInt()
        val burst get() = v[1].toInt()
        val bufferFrames get() = v[2].toInt()
        val bufferCapacity get() = v[3].toInt()
        val oboeLatencyMs get() = v[4]
        val oboeXruns get() = v[5].toLong()
        val underruns get() = v[6].toLong()
        val fill get() = v[7].toInt()
        val hostBlocks get() = v[8].toLong()
        val hostAvgUs get() = v[9]
        val hostMaxUs get() = v[10]
        val cbAvgUs get() = v[11]
        val cbMaxUs get() = v[12]
        val callbacks get() = v[13].toLong()
        val exclusive get() = v[14] > 0
        val lowLatency get() = v[15] > 0
        val aaudio get() = v[16] > 0
        val block get() = v[17].toInt()
        val targetFill get() = v[18].toInt()
        val futex get() = v[19].toInt() == 1
        val hostPid get() = v[20].toInt()
        val patternMismatches get() = v[21].toLong()
        val patternVerified get() = v[22].toLong()
        val hostState get() = v[23].toInt()
        val callbackFrames get() = v[24].toInt()
        val streamRestarts get() = v[25].toLong()
        val midiConsumed get() = v[26].toLong()
        val peakDb get() = if (v[27] > 0) 20 * Math.log10(v[27]) else -999.0
    }
}
