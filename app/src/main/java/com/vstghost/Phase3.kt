// SPDX-License-Identifier: LicenseRef-VstGhost-Proprietary
package com.vstghost

import android.content.Context
import android.media.midi.MidiDevice
import android.media.midi.MidiDeviceInfo
import android.media.midi.MidiInputPort
import android.media.midi.MidiManager
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import java.io.File
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit

/**
 * Phase 3 self-test of the virtual MIDI devices: opens "VST gHost Inst 1" and "Inst 2" through
 * Android's MidiManager (the same public API a third-party app uses), plays both at once, and
 * checks per slot that the Windows host started on demand, consumed the MIDI and produced sound.
 * (The AAP path needs an AAP host app; see docs/phase3-report.md for that test.)
 */
class Phase3(private val context: Context, private val log: (String) -> Unit) {
    private val report = StringBuilder()
    private fun md(s: String = "") { report.appendLine(s); log(s) }
    fun reportFile() = File(context.getExternalFilesDir(null) ?: context.filesDir, "phase3-report.md")

    fun runAll(slots: List<Int> = listOf(1, 2)): String {
        md("# VST gHost: Phase 3 self-test (virtual MIDI devices)")
        md()
        md("Generated ${SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.US).format(Date())} on ${Build.MANUFACTURER} ${Build.MODEL}, " +
           "Android ${Build.VERSION.RELEASE}, edition **${BuildConfig.EDITION}**.")
        md()
        md("Slot assignments: " + SlotManager.assignments().entries.sortedBy { it.key }.joinToString { "${SlotManager.label(it.key)} = ${it.value}" })
        md()
        val mm = context.getSystemService(MidiManager::class.java) ?: return finish("No MidiManager on this device.")
        @Suppress("DEPRECATION")
        // Only this edition's devices (sideload and Play can be installed side by side, same names).
        val devices = mm.devices.filter {
            it.properties.getString(MidiDeviceInfo.PROPERTY_MANUFACTURER) == "VST gHost" &&
                (it.properties.getParcelable<android.content.pm.ServiceInfo>("service_info")?.packageName ?: context.packageName) == context.packageName
        }
        md("- Android MIDI service lists ${devices.size} VST gHost devices: " +
           devices.joinToString { it.properties.getString(MidiDeviceInfo.PROPERTY_NAME) ?: "?" })
        slots.forEach { Bridge.get(context, it).stop() }  // start cold, like a fresh app
        NativeEngine.startAudio()

        val handler = Handler(Looper.getMainLooper())
        val opened = HashMap<Int, Pair<MidiDevice, MidiInputPort>>()
        for (slot in slots) {
            val info = devices.firstOrNull { it.properties.getString(MidiDeviceInfo.PROPERTY_NAME) == SlotManager.label(slot) }
                ?: return finish("Device ${SlotManager.label(slot)} not found.")
            val latch = CountDownLatch(1)
            var dev: MidiDevice? = null
            mm.openDevice(info, { d -> dev = d; latch.countDown() }, handler)
            latch.await(5, TimeUnit.SECONDS)
            val port = dev?.openInputPort(0) ?: return finish("Could not open ${SlotManager.label(slot)}.")
            opened[slot] = dev!! to port
        }
        md("- opened ${opened.size} devices via MidiManager.openDevice + openInputPort(0)")

        fun send(slot: Int, vararg b: Int) {
            val bytes = ByteArray(b.size) { b[it].toByte() }
            opened[slot]?.second?.send(bytes, 0, bytes.size)
        }

        // First note starts each slot's host (lazily); measure until it runs.
        val t0 = SystemClock.elapsedRealtime()
        slots.forEach { send(it, 0x90, 60, 100) }
        val startMs = HashMap<Int, Long>()
        while (SystemClock.elapsedRealtime() - t0 < 60_000 && startMs.size < slots.size) {
            slots.filter { it !in startMs && NativeEngine.hostState(it) == NativeEngine.HOST_RUNNING }
                .forEach { startMs[it] = SystemClock.elapsedRealtime() - t0 }
            Thread.sleep(20)
        }
        slots.forEach { send(it, 0x80, 60, 0) }
        slots.forEach { md("- ${SlotManager.label(it)}: host running ${startMs[it]?.let { ms -> "$ms ms after the first MIDI note" } ?: "**NOT within 60 s**"}") }
        Thread.sleep(500)

        // Play both slots at once for 4 s.
        slots.forEach { NativeEngine.resetStats(it) }
        val m0 = slots.associateWith { NativeEngine.statsNamed(it).midiConsumed }
        val chords = listOf(listOf(48, 55, 64), listOf(45, 52, 60), listOf(41, 48, 57), listOf(43, 50, 59))
        val playStart = SystemClock.elapsedRealtime()
        var sent = 0
        for (rep in 0 until 4) for ((i, ch) in chords.withIndex()) {
            slots.forEachIndexed { k, slot -> ch.forEach { n -> send(slot, 0x90, n + 12 * k, 90); sent++ } }
            Thread.sleep(220)
            slots.forEachIndexed { k, slot -> ch.forEach { n -> send(slot, 0x80, n + 12 * k, 0) } }
            Thread.sleep(30)
            if (i == 3 && rep == 3) break
        }
        Thread.sleep(600)
        md()
        md("| Slot | VST | MIDI messages consumed by the host | Output peak (app mix) | Underruns | Result |")
        md("|---|---|---|---|---|---|")
        var allOk = true
        for (slot in slots) {
            val st = NativeEngine.statsNamed(slot)
            val consumed = st.midiConsumed - (m0[slot] ?: 0)
            val ok = consumed > 0 && st.peakDb > -60
            allOk = allOk && ok
            md("| ${SlotManager.label(slot)} | ${SlotManager.assigned(slot)} | $consumed | ${"%.1f".format(st.peakDb)} dBFS | ${st.underruns} | ${if (ok) "**PASS**" else "**FAIL**"} |")
        }
        md()
        md("- ${sent} note-ons sent over ${(SystemClock.elapsedRealtime() - playStart) / 1000.0} s to ${slots.size} devices at the same time (multi-slot mix)")
        md("- overall: **${if (allOk) "PASS" else "FAIL"}**")
        opened.values.forEach { (d, p) -> runCatching { p.close(); d.close() } }
        return finish()
    }

    private fun finish(note: String? = null): String {
        if (note != null) md("**$note**")
        val text = report.toString()
        listOf(File(context.filesDir, "phase3-report.md"), reportFile()).forEach { runCatching { it.parentFile?.mkdirs(); it.writeText(text) } }
        log("report written: ${reportFile()}")
        return text
    }
}
