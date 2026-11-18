// SPDX-License-Identifier: GPL-3.0-or-later
package com.vstghost

import android.content.Context
import android.util.Log
import org.json.JSONObject
import java.io.File

/**
 * The fixed "bridge slots" other apps see (AAP plugins and virtual MIDI devices):
 *   slots 1..4 = "VST gHost Inst 1..4", slots 5..8 = "VST gHost FX 1..4".
 * The user assigns any bridged VST to a slot in the app; the assignment is stored in
 * files/slots.json. A slot's host is started on demand by an AAP host (prepareForAap) or a MIDI
 * client (ensureForMidi).
 */
object SlotManager {
    const val FIRST_INST = 1
    const val FIRST_FX = 5
    const val COUNT = 4

    private lateinit var app: Context
    private val file get() = File(app.filesDir, "slots.json")
    private val lock = Any()

    fun init(context: Context) { app = context.applicationContext }

    fun label(slot: Int) = if (slot < FIRST_FX) "VST gHost Inst $slot" else "VST gHost FX ${slot - FIRST_FX + 1}"
    fun isEffectSlot(slot: Int) = slot >= FIRST_FX

    fun assignments(): Map<Int, String> = synchronized(lock) {
        val j = runCatching { JSONObject(file.readText()) }.getOrNull() ?: return emptyMap()
        j.keys().asSequence().mapNotNull { k -> k.toIntOrNull()?.let { it to j.getString(k) } }.toMap()
    }

    fun assigned(slot: Int): String? = assignments()[slot]

    fun assign(slot: Int, plugin: String?) = synchronized(lock) {
        val j = runCatching { JSONObject(file.readText()) }.getOrNull() ?: JSONObject()
        if (plugin == null) j.remove(slot.toString()) else j.put(slot.toString(), plugin)
        file.writeText(j.toString())
    }

    private fun pluginFile(name: String) = File(app.filesDir, "plugins/$name.vst3")

    @Volatile private var runtimeReady = false

    /**
     * Once per process, serialized: several slots can start at the same moment (a MIDI app playing
     * two of our devices, or an AAP host instantiating two slots). Setting up the runtime
     * concurrently raced on the loader symlink folder and crashed the app.
     */
    private fun prepareRuntime(): Payload = synchronized(runtimeLock) {
        val payload = Payload(app)
        if (!runtimeReady) {
            if (payload.needsInstall()) payload.install(null)
            if (payload.isPlay && !payload.shimReady()) payload.buildShim()
            Phase1(app) {}.syncPluginsPublic()
            runtimeReady = true
        }
        Bridge.ensureSession(payload)
        payload
    }
    private val runtimeLock = Any()

    /**
     * Called from native code (AAP prepare(), on an AAP binder thread): start the slot's host for
     * the assigned VST, with its audio going to the AAP host. Blocking; returns success.
     */
    @JvmStatic
    fun prepareForAap(slot: Int, sampleRate: Int, frames: Int): Boolean {
        val name = assigned(slot)
        if (name == null) { Log.w(TAG, "AAP: ${label(slot)} has no VST assigned"); return false }
        try { prepareRuntime() } catch (e: Exception) { Log.e(TAG, "AAP: runtime setup failed", e); return false }
        if (!pluginFile(name).exists()) { Log.w(TAG, "AAP: plugin $name not found"); return false }
        val block = when { frames <= 128 -> 128; frames <= 256 -> 256; else -> 256 }
        val r = Bridge.get(app, slot).load(pluginFile(name), block, 2, rate = sampleRate, useAppAudio = false)
        Log.i(TAG, "AAP: ${label(slot)} -> $name at $sampleRate Hz, block $block: ${r.message} (${r.ms} ms)")
        return r.ok
    }

    /** Called from native code (AAP release()). */
    @JvmStatic
    fun releaseFromAap(slot: Int) {
        Bridge.get(app, slot).stop()
        Log.i(TAG, "AAP: ${label(slot)} released")
    }

    /** Virtual MIDI device opened: run the slot with its audio through the app's own output. */
    fun ensureForMidi(slot: Int): Boolean {
        val bridge = Bridge.get(app, slot)
        if (bridge.isRunning) return true  // already running (maybe for an AAP host: MIDI goes there too)
        val name = assigned(slot) ?: run { Log.w(TAG, "MIDI: ${label(slot)} has no VST assigned"); return false }
        try { prepareRuntime() } catch (e: Exception) { Log.e(TAG, "MIDI: runtime setup failed", e); return false }
        val r = bridge.load(pluginFile(name), 128, 2)
        Log.i(TAG, "MIDI: ${label(slot)} -> $name: ${r.message} (${r.ms} ms)")
        return r.ok
    }
}
