// SPDX-License-Identifier: GPL-3.0-or-later
package com.vstghost

import android.media.midi.MidiDeviceService
import android.media.midi.MidiReceiver
import android.util.Log

/** Parses a MIDI 1.0 byte stream (with running status) into channel-voice messages. */
class MidiParser(private val onMessage: (status: Int, d1: Int, d2: Int) -> Unit) {
    private var running = 0
    fun feed(msg: ByteArray, offset: Int, count: Int) {
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
            val d2 = if (len == 2) msg[i + 1].toInt() and 0x7F else 0
            onMessage(status, d1, d2)
            i += len
        }
    }
}

/**
 * Virtual MIDI device for one instrument slot ("VST gHost Inst N"). Other apps send MIDI to it;
 * the slot's VST plays through the app's audio output (or into an AAP host if one owns the slot).
 */
abstract class MidiSlotService(private val slot: Int) : MidiDeviceService() {
    private val starting = java.util.concurrent.atomic.AtomicBoolean(false)

    /** Starts the slot's host once (again after a failure or crash). */
    private fun ensureStarted() {
        if (!starting.compareAndSet(false, true)) return
        Thread {
            try { SlotManager.ensureForMidi(slot) }
            catch (e: Exception) { Log.e(TAG, "MIDI: could not start ${SlotManager.label(slot)}", e) }
            finally { starting.set(false) }
        }.start()
    }

    private val receiver = object : MidiReceiver() {
        private val parser = MidiParser { st, d1, d2 -> NativeEngine.sendMidi(slot, st, d1, d2) }
        override fun onSend(msg: ByteArray, offset: Int, count: Int, timestamp: Long) {
            if (NativeEngine.hostState(slot) != NativeEngine.HOST_RUNNING) ensureStarted()
            parser.feed(msg, offset, count)
        }
    }

    // No host start in onCreate: apps (e.g. JUCE hosts) open every MIDI device just to list them.
    // The slot's Windows host starts when the first MIDI message actually arrives (~0.5-1.5 s).
    override fun onCreate() {
        super.onCreate()
        Log.i(TAG, "MIDI device ${SlotManager.label(slot)} opened")
    }

    override fun onGetInputPortReceivers(): Array<MidiReceiver> = arrayOf(receiver)
}

class MidiSlot1Service : MidiSlotService(1)
class MidiSlot2Service : MidiSlotService(2)
class MidiSlot3Service : MidiSlotService(3)
class MidiSlot4Service : MidiSlotService(4)
