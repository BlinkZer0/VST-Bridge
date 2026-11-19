// SPDX-License-Identifier: LicenseRef-VstGhost-Proprietary
package com.vstghost

import android.content.Context
import android.os.Build
import android.os.SystemClock
import org.json.JSONObject
import java.io.File
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

/**
 * Phase 2 automated measurements (adb autorun, --ei phase 2):
 *   1. ring integrity under Box64 (host writes a counter; engine checks every sample)
 *   2. per plugin × block size: smallest buffering with 0 underruns over 15 s of busy playing,
 *      then note-on → output latency (probes), host CPU, callback stats
 *   3. crash isolation: kill the host mid-play, check audio keeps running, restart with state
 *   4. preset round trip (get_state / set_state)
 * Latency here is measured inside the engine (exact frames). No acoustic loopback was used.
 */
class Phase2(private val context: Context, private val log: (String) -> Unit) {
    private val bridge = Bridge.get(context)
    private val report = StringBuilder()
    private fun md(s: String = "") { report.appendLine(s); log(s) }
    private val pluginDir = File(context.filesDir, "plugins")
    fun reportFile() = File(context.getExternalFilesDir(null) ?: context.filesDir, "phase2-report.md")

    fun runAll(plugins: List<String> = listOf("E-Piano", "Odin2"), blocks: List<Int> = listOf(128, 256, 512),
               playSec: Int = 15, reinstall: Boolean = false): String {
        md("# VST gHost: Phase 2 report (real-time)")
        md()
        md("Generated ${SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.US).format(Date())} on ${Build.MANUFACTURER} ${Build.MODEL}, " +
           "Android ${Build.VERSION.RELEASE}, edition **${BuildConfig.EDITION}**. Measured on this device in this run.")
        md()
        if (reinstall || bridge.payload.installedVersion == null) bridge.payload.install(null)
        if (bridge.payload.isPlay) bridge.payload.buildShim()
        Phase1(context) {}.syncPluginsPublic()
        bridge.ensureSession()
        if (!NativeEngine.startAudio()) return finish("Oboe output stream could not be opened.")
        Thread.sleep(300)
        val s0 = NativeEngine.statsNamed()
        md("Audio output: ${s0.sampleRate} Hz, burst ${s0.burst} frames, buffer ${s0.bufferFrames}/${s0.bufferCapacity}, " +
           "${if (s0.aaudio) "AAudio" else "OpenSL ES"}, exclusive=${s0.exclusive}, low-latency=${s0.lowLatency}; " +
           "Oboe-reported output latency ${"%.1f".format(s0.oboeLatencyMs)} ms")
        md()

        ringIntegrity()
        val rows = mutableListOf<String>()
        for (name in plugins) for (b in blocks) rows += latencyRun(name, b, playSec)
        md("## Latency and stability")
        md()
        md("| Plugin | Block | Smallest stable buffer | Bridge latency (note-on → output, median / max) | + Oboe output latency | ≈ key → speaker | Host CPU (% of one core) | Host render avg / max | Underruns / Oboe xruns |")
        md("|---|---|---|---|---|---|---|---|---|")
        rows.forEach { md(it) }
        md()
        md("Bridge latency is measured inside the engine: frames between sending the note and the first output sample above −50 dBFS (includes the plugin's own attack). " +
           "Oboe output latency is AAudio's estimate of buffer → speaker. Touch-screen latency is not included. No acoustic loopback was available.")
        md()
        crashIsolation()
        presetRoundTrip()
        bridge.stop()
        return finish()
    }

    private fun ringIntegrity() {
        md("## Ring integrity under Box64")
        val r = bridge.load(null, 128, 2, pattern = true)
        if (!r.ok) { md("- could not start pattern host: ${r.message}"); return }
        NativeEngine.resetStats()
        NativeEngine.setVerifyPattern(true)
        Thread.sleep(20_000)
        val st = NativeEngine.statsNamed()
        NativeEngine.setVerifyPattern(false)
        md("- 20 s at 128-frame blocks, host writes frame index as samples, engine checks every sample: " +
           "**${st.patternMismatches} mismatches in ${st.patternVerified} samples**, underruns ${st.underruns}, " +
           "wakeups ${if (st.futex) "futex" else "polling"}")
        md("- Box64 settings: defaults (no STRONGMEM); ring indices use seq_cst atomics")
        md()
        bridge.stop()
    }

    private fun play(seconds: Int, stopAt: () -> Boolean = { false }) {
        // Busy pattern: 4-note chords every 2 beats, 16th-note arpeggio on top, sustain every bar. 120 BPM.
        val chords = listOf(listOf(48, 55, 60, 64), listOf(45, 52, 57, 60), listOf(41, 48, 53, 57), listOf(43, 50, 55, 59))
        val t0 = SystemClock.elapsedRealtime()
        var step = 0
        while (SystemClock.elapsedRealtime() - t0 < seconds * 1000L && !stopAt()) {
            val chord = chords[(step / 8) % 4]
            if (step % 8 == 0) {
                NativeEngine.sendMidi(0xB0, 64, if ((step / 8) % 2 == 0) 127 else 0)
                chords[((step / 8) + 3) % 4].forEach { NativeEngine.sendMidi(0x80, it, 0) }
                chord.forEach { NativeEngine.sendMidi(0x90, it, 90) }
            }
            val n = chord[step % 4] + 24
            NativeEngine.sendMidi(0x90, n, 70 + (step % 4) * 10)
            Thread.sleep(60)
            NativeEngine.sendMidi(0x80, n, 0)
            Thread.sleep(65)
            step++
        }
        chords.flatten().forEach { NativeEngine.sendMidi(0x80, it, 0) }
        NativeEngine.sendMidi(0xB0, 64, 0)
    }

    private fun probes(n: Int): List<Double> {
        val out = mutableListOf<Double>()
        repeat(n) {
            for (k in 0..127) NativeEngine.sendMidi(0x80, k, 0)
            NativeEngine.sendMidi(0xB0, 64, 0)
            Thread.sleep(700)  // let the release tail die away
            NativeEngine.startProbe(84, 127, -50f)
            val t0 = SystemClock.elapsedRealtime()
            var f = -1L
            while (f < 0 && SystemClock.elapsedRealtime() - t0 < 1000) { Thread.sleep(2); f = NativeEngine.probeResult() }
            if (f >= 0) out += f * 1000.0 / 48000
            NativeEngine.sendMidi(0x80, 84, 0)
        }
        return out.sorted()
    }

    private fun latencyRun(name: String, block: Int, playSec: Int): String {
        val plugin = File(pluginDir, "$name.vst3")
        md("## $name @ $block frames")
        if (!plugin.exists()) { md("- plugin not found"); return "| $name | $block | not found | | | | | | |" }
        for (fill in 2..8) {
            val r = bridge.load(plugin, block, fill)
            if (!r.ok) { md("- load failed: ${r.message}"); return "| $name | $block | load failed | | | | | | |" }
            Thread.sleep(500)
            NativeEngine.resetStats()
            val cpu0 = bridge.hostCpuSeconds(); val w0 = SystemClock.elapsedRealtime()
            val xr0 = NativeEngine.statsNamed().oboeXruns
            play(playSec)
            val cpu1 = bridge.hostCpuSeconds(); val w1 = SystemClock.elapsedRealtime()
            val st = NativeEngine.statsNamed()
            val cpuPct = if (cpu0 != null && cpu1 != null) (cpu1 - cpu0) / ((w1 - w0) / 1000.0) * 100 else Double.NaN
            val xr = st.oboeXruns - xr0
            md("- buffer $fill × $block = ${fill * block} frames: load ${r.ms} ms, underruns ${st.underruns}, Oboe xruns $xr, " +
               "host render avg ${"%.0f".format(st.hostAvgUs)} µs max ${"%.0f".format(st.hostMaxUs)} µs, " +
               "callback avg ${"%.0f".format(st.cbAvgUs)} µs max ${"%.0f".format(st.cbMaxUs)} µs, host CPU ${"%.0f".format(cpuPct)}%")
            if (st.underruns == 0L) {
                val lat = probes(10)
                val med = lat.getOrNull(lat.size / 2) ?: Double.NaN
                val max = lat.lastOrNull() ?: Double.NaN
                val total = med + st.oboeLatencyMs
                md("- latency probes (ms): ${lat.joinToString { "%.1f".format(it) }}")
                md()
                val eff = st.targetFill
                return "| $name | $block | $fill × $block requested, **$eff frames used** (${"%.1f".format(eff / 48.0)} ms; callback ${st.callbackFrames}, ${if (st.lowLatency) "low-latency" else "NOT low-latency"}) | " +
                       "${"%.1f".format(med)} / ${"%.1f".format(max)} ms | ${"%.1f".format(st.oboeLatencyMs)} ms | **${"%.0f".format(total)} ms** | " +
                       "${"%.0f".format(cpuPct)}% | ${"%.0f".format(st.hostAvgUs)} / ${"%.0f".format(st.hostMaxUs)} µs | 0 / $xr |"
            }
        }
        md()
        return "| $name | $block | none up to 8 blocks | | | | | | |"
    }

    private fun crashIsolation() {
        md("## Crash isolation")
        val plugin = File(pluginDir, "E-Piano.vst3")
        val r = bridge.load(plugin, 256, 2)
        if (!r.ok) { md("- load failed: ${r.message}"); return }
        var exitSeen = -999
        bridge.onHostExit = { exitSeen = it }
        val state = bridge.request("get_state")
        Thread { play(2) }.start()
        Thread.sleep(1000)
        val pid = NativeEngine.statsNamed().hostPid
        val cb0 = NativeEngine.statsNamed().callbacks
        android.os.Process.sendSignal(pid, 9)  // SIGKILL the plugin host
        md("- killed host pid $pid with SIGKILL while playing")
        Thread.sleep(2000)
        val cb1 = NativeEngine.statsNamed().callbacks
        md("- app still running: yes (this report); audio callbacks continued: ${cb1 - cb0} callbacks in the 2 s after the kill " +
           "(${if (cb1 > cb0) "PASS" else "FAIL"}); host exit noticed: ${if (exitSeen != -999) "yes (code $exitSeen)" else "no"}")
        val t0 = SystemClock.elapsedRealtime()
        bridge.onHostExit = null
        val r2 = bridge.load(plugin, 256, 2)
        var restored = false
        if (r2.ok && state != null) {
            val j = JSONObject(state)
            restored = bridge.request("set_state\n${j.optString("component")}\n${j.optString("controller")}")?.let { JSONObject(it).optBoolean("ok") } == true
        }
        val lat = if (r2.ok) probes(3) else emptyList()
        md("- restart + restore state: ${if (r2.ok) "OK" else "FAILED (${r2.message})"} in ${SystemClock.elapsedRealtime() - t0} ms; " +
           "state restored: $restored; plays again: ${if (lat.isNotEmpty()) "yes (probe ${"%.1f".format(lat[0])} ms)" else "no"}")
        md()
    }

    private fun presetRoundTrip() {
        md("## Preset (state) round trip")
        val params = bridge.request("params")?.let { JSONObject(it).optJSONArray("params") }
        if (params == null || params.length() == 0) { md("- no params"); return }
        val p0 = params.getJSONObject(0)
        val id = p0.getLong("id"); val orig = p0.getDouble("value")
        val saved = bridge.request("get_state")?.let { JSONObject(it) } ?: run { md("- get_state failed"); return }
        val changed = bridge.request("set\n$id\n${if (orig < 0.5) 0.9 else 0.1}")?.let { JSONObject(it).getDouble("value") }
        bridge.request("set_state\n${saved.optString("component")}\n${saved.optString("controller")}")
        val after = bridge.request("params")?.let { JSONObject(it).getJSONArray("params").getJSONObject(0).getDouble("value") }
        md("- `${p0.optString("title")}`: original ${"%.3f".format(orig)}, changed to ${"%.3f".format(changed ?: Double.NaN)}, " +
           "after restoring the saved state ${"%.3f".format(after ?: Double.NaN)} → **${if (after != null && Math.abs(after - orig) < 1e-6) "PASS" else "FAIL"}** " +
           "(state ${saved.optString("component").length + saved.optString("controller").length} base64 chars)")
        md()
    }

    private fun finish(note: String? = null): String {
        if (note != null) md("**$note**")
        val text = report.toString()
        listOf(File(context.filesDir, "phase2-report.md"), reportFile()).forEach { runCatching { it.parentFile?.mkdirs(); it.writeText(text) } }
        log("report written: ${reportFile()}")
        return text
    }
}
