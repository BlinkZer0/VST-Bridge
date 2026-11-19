// SPDX-License-Identifier: LicenseRef-VstGhost-Proprietary
package com.vstghost

import android.content.Context
import android.media.midi.MidiDevice
import android.media.midi.MidiDeviceInfo
import android.media.midi.MidiManager
import android.media.midi.MidiOutputPort
import android.media.midi.MidiReceiver
import android.os.Handler
import android.os.Looper
import android.util.Log

/**
 * Android MIDI input (USB / Bluetooth / virtual devices) -> bridge. Opens every device's
 * output ports and forwards channel-voice messages to the engine.
 * NOT TESTED ON A DEVICE YET: no MIDI keyboard was available during Phase 2.
 */
class MidiInput(context: Context, private val onStatus: (String) -> Unit) {
    /**
     * Settings (⋮ → MIDI settings…): [channel] 0 = all channels, 1–16 = only that one;
     * [routeByChannel] sends channel N to instrument slot N (1–4) instead of the played slots;
     * [velocity] "linear" | "soft" | "hard" | "fixed" (fixed = 100).
     */
    class Settings(ctx: Context) {
        private val p = ctx.getSharedPreferences("midi", Context.MODE_PRIVATE)
        var channel: Int get() = p.getInt("channel", 0); set(v) { p.edit().putInt("channel", v).apply() }
        var routeByChannel: Boolean get() = p.getBoolean("route", false); set(v) { p.edit().putBoolean("route", v).apply() }
        var velocity: String get() = p.getString("velocity", "linear")!!; set(v) { p.edit().putString("velocity", v).apply() }
        fun describe() = (if (channel == 0) "all channels" else "channel $channel") +
            (if (routeByChannel) ", channel → slot" else "") + (if (velocity != "linear") ", $velocity velocity" else "")
    }
    val settings = Settings(context)

    private fun curve(v: Int): Int = when (settings.velocity) {
        "soft" -> Math.round(127.0 * Math.pow(v / 127.0, 0.6)).toInt().coerceIn(1, 127)
        "hard" -> Math.round(127.0 * Math.pow(v / 127.0, 1.6)).toInt().coerceIn(1, 127)
        "fixed" -> 100
        else -> v
    }

    private val mm = context.getSystemService(MidiManager::class.java)
    private val open = HashMap<Int, Pair<MidiDevice, List<MidiOutputPort>>>()
    private val handler = Handler(Looper.getMainLooper())

    private val receiver = object : MidiReceiver() {
        private var running = 0
        override fun onSend(msg: ByteArray, offset: Int, count: Int, timestamp: Long) {
            var i = offset
            val end = offset + count
            while (i < end) {
                val b = msg[i].toInt() and 0xFF
                if (b >= 0xF0) { i++; continue }  // system messages: ignored
                val status = if (b and 0x80 != 0) { running = b; i++; b } else running
                if (status == 0) { i++; continue }
                val len = if ((status and 0xF0) == 0xC0 || (status and 0xF0) == 0xD0) 1 else 2
                if (i + len > end) break
                val d1 = msg[i].toInt() and 0x7F
                var d2 = if (len == 2) msg[i + 1].toInt() and 0x7F else 0
                val ch = (status and 0x0F) + 1
                val kind = status and 0xF0
                if (settings.channel == 0 || settings.channel == ch) {
                    if (kind == 0x90 && d2 > 0) d2 = curve(d2)
                    if (settings.routeByChannel) {
                        if (ch in 1..SlotManager.COUNT) NativeEngine.sendMidi(SlotManager.FIRST_INST + ch - 1, status, d1, d2)
                    } else NativeEngine.sendMidi(status, d1, d2)
                }
                if ((status and 0xF0) == 0x90 && d2 > 0) Log.i(TAG, "MIDI in: note on $d1 vel $d2 ch ${(status and 0x0F) + 1}")
                i += len
            }
        }
    }

    private val callback = object : MidiManager.DeviceCallback() {
        override fun onDeviceAdded(info: MidiDeviceInfo) = openDevice(info)
        override fun onDeviceRemoved(info: MidiDeviceInfo) { closeDevice(info.id); report() }
    }

    fun start() {
        if (mm == null) { onStatus("MIDI: not supported on this device"); return }
        @Suppress("DEPRECATION")
        mm.devices.forEach { openDevice(it) }
        @Suppress("DEPRECATION")
        mm.registerDeviceCallback(callback, handler)
        report()
    }

    fun stop() {
        mm?.unregisterDeviceCallback(callback)
        open.keys.toList().forEach { closeDevice(it) }
    }

    private fun openDevice(info: MidiDeviceInfo) {
        if (info.outputPortCount == 0 || open.containsKey(info.id)) return
        if (info.properties.getString(MidiDeviceInfo.PROPERTY_MANUFACTURER) == "VST gHost") return
        mm?.openDevice(info, { dev ->
            if (dev == null) return@openDevice
            val ports = (0 until info.outputPortCount).mapNotNull { dev.openOutputPort(it) }
            ports.forEach { it.connect(receiver) }
            open[info.id] = dev to ports
            Log.i(TAG, "MIDI opened ${info.properties.getString(MidiDeviceInfo.PROPERTY_NAME)}")
            report()
        }, handler)
    }

    private fun closeDevice(id: Int) {
        open.remove(id)?.let { (dev, ports) -> ports.forEach { runCatching { it.close() } }; runCatching { dev.close() } }
    }

    private fun report() {
        val names = open.values.map { it.first.info.properties.getString(MidiDeviceInfo.PROPERTY_NAME) ?: "device" }
        onStatus(if (names.isEmpty()) "MIDI in: none connected" else "MIDI in: ${names.joinToString()}")
    }
}
