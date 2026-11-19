// SPDX-License-Identifier: LicenseRef-VstGhost-Proprietary
package com.vstghost

import android.content.Context
import android.os.SystemClock
import android.util.Log
import org.json.JSONObject
import java.io.File

/**
 * Owns one bridge host process (`vstghost-host.exe serve`) for one slot.
 *
 * Crash isolation: the host is a separate process. If it dies, the audio output keeps running
 * (the slot is silent), [onHostExit] fires, and the slot can be restarted, optionally with the
 * last captured plugin state.
 */
class Bridge private constructor(context: Context, val slot: Int) {
    companion object {
        private val instances = arrayOfNulls<Bridge>(NativeEngine.SLOTS)
        /** One bridge per slot per process (slot 0 = the player; shared with the test harness). */
        fun get(context: Context, slot: Int = 0): Bridge = synchronized(instances) {
            instances[slot] ?: Bridge(context.applicationContext, slot).also { instances[slot] = it }
        }

        /** Slots whose bridge has a plugin loaded and running (this process's view). */
        fun activeSlots(): List<Int> = synchronized(instances) {
            instances.withIndex().filter { (_, b) -> b != null && b.plugin != null && b.isRunning }.map { it.index }
        }
        fun allSlots(): IntRange = 0 until NativeEngine.SLOTS
        fun stopAll() { for (b in synchronized(instances) { instances.filterNotNull() }) if (b.plugin != null) b.stop() }

        /** One persistent Wine session for all hosts (saves ~1 s per load). */
        @Volatile private var sessionStarted = false
        fun ensureSession(payload: Payload) {
            EditorDisplay.ensure(payload)  // before any Wine process: see EditorDisplay
            if (sessionStarted && File(payload.root, "prefix/.wineserver").listFiles()?.any { File(it, "socket").exists() } == true) return
            // `wineserver -p` forks a daemon that inherits stdout: never read its output to EOF
            // (that blocks for as long as the daemon lives). Discard it and wait for the launcher only.
            val ws = payload.box64Command() + listOf("${payload.root}/wine/bin/wineserver", "-p")
            runCatching {
                val p = ProcessBuilder(ws).directory(payload.root).apply {
                    environment().remove("LD_PRELOAD"); environment().putAll(payload.environment())
                }.redirectErrorStream(true).redirectOutput(File("/dev/null")).start()
                p.waitFor(15, java.util.concurrent.TimeUnit.SECONDS)
                sessionStarted = true
            }
        }
    }

    val payload = Payload(context)
    private val appContext: Context = context.applicationContext
    /** Phase 5: the slot runs a native plugin in-process instead of a Wine host. */
    @Volatile var isNativeLoaded = false; private set
    /** Instrument (MIDI in, audio out) or effect (audio in and out); known once loaded. */
    @Volatile var isInstrument = true; private set
    private val shm = File(context.filesDir, "ipc/slot$slot.shm")
    private var proc: Process? = null
    @Volatile private var stopping = false
    private val logLines = ArrayDeque<String>()

    var plugin: File? = null; private set
    var block = 256; private set
    var fillBlocks = 2; private set
    var lastLoadMs = 0L; private set

    /** Called on a background thread when the host exits without being asked to. */
    var onHostExit: ((exitCode: Int) -> Unit)? = null

    fun recentLog(n: Int = 30): List<String> = synchronized(logLines) { logLines.takeLast(n) }

    fun ensureSession() = ensureSession(payload)

    data class LoadResult(val ok: Boolean, val message: String, val ms: Long)

    /**
     * Starts a host for [pluginFile] (null + pattern = ring integrity test). Blocking.
     * [rate] 0 = the app's audio output rate. [useAppAudio] false when an AAP host will consume
     * the slot's audio (the app's own output stream is then not needed).
     */
    fun load(pluginFile: File?, block: Int, fillBlocks: Int, pattern: Boolean = false, timeoutMs: Long = 120_000,
             rate: Int = 0, useAppAudio: Boolean = true): LoadResult {
        if (NativePlugins.isNative(pluginFile)) return loadNative(pluginFile!!, rate, useAppAudio)
        stop()
        if (useAppAudio && !NativeEngine.startAudio()) return LoadResult(false, "could not open the audio output (Oboe)", 0)
        NativeEngine.setAapOwned(slot, !useAppAudio)
        this.plugin = pluginFile
        this.block = block
        this.fillBlocks = fillBlocks
        shm.parentFile?.mkdirs()
        NativeEngine.createBridge(slot, shm.path, rate, block, block * fillBlocks)?.let { return LoadResult(false, "shared memory: $it", 0) }

        val cmd = payload.box64Command() + listOf(
            "${payload.root}/wine/bin/wine", "${payload.root}/host/vstghost-host.exe", "serve",
            pluginFile?.path ?: "-", "--shm", shm.path) + (if (pattern) listOf("--pattern") else emptyList())
        val t0 = SystemClock.elapsedRealtime()
        val p = try {
            ProcessBuilder(cmd).directory(payload.root).apply {
                environment().remove("LD_PRELOAD")
                environment().putAll(payload.environment(mapOf("WINEDEBUG" to "-all,err+module")))
            }.redirectErrorStream(true).start()
        } catch (e: Exception) {
            return LoadResult(false, "could not start host: $e", 0)
        }
        proc = p
        Thread {
            p.inputStream.bufferedReader().forEachLine { line ->
                if (line.startsWith("[BOX64]") || line.startsWith("\t")) return@forEachLine
                Log.i(TAG, "[host$slot] $line")
                synchronized(logLines) { logLines.addLast(line); while (logLines.size > 200) logLines.removeFirst() }
            }
        }.apply { isDaemon = true; start() }
        Thread {
            val code = p.waitFor()
            if (!stopping && proc === p) {
                Log.w(TAG, "slot $slot host exited unexpectedly: $code")
                onHostExit?.invoke(code)
            }
        }.apply { isDaemon = true; start() }

        while (SystemClock.elapsedRealtime() - t0 < timeoutMs) {
            when (NativeEngine.hostState(slot)) {
                NativeEngine.HOST_RUNNING -> {
                    lastLoadMs = SystemClock.elapsedRealtime() - t0
                    // Instrument or effect: decides the slot's place in the engine's routing.
                    isInstrument = request("info")?.let { runCatching { JSONObject(it).optBoolean("instrument", true) }.getOrNull() } ?: true
                    NativeEngine.setSlotKind(slot, if (isInstrument) 0 else 1)
                    PlaybackService.update(appContext)
                    return LoadResult(true, "running", lastLoadMs)
                }
                NativeEngine.HOST_FAILED -> return LoadResult(false, NativeEngine.lastResponse(slot) ?: "host failed", 0)
            }
            if (!p.isAlive) return LoadResult(false, "host exited (code ${p.exitValue()}): ${recentLog(5).joinToString(" | ")}", 0)
            Thread.sleep(20)
        }
        return LoadResult(false, "timed out waiting for the host", 0)
    }

    private fun loadNative(f: File, rate: Int, useAppAudio: Boolean): LoadResult {
        stop()
        if (useAppAudio && !NativeEngine.startAudio()) return LoadResult(false, "could not open the audio output (Oboe)", 0)
        NativeEngine.setAapOwned(slot, !useAppAudio)
        plugin = f
        val t0 = SystemClock.elapsedRealtime()
        val lib = f.nameWithoutExtension
        val info = NativePlugins.ensure(appContext, lib) ?: return LoadResult(false, "could not load native library $lib", 0)
        val err = onMain { NativeEngine.nativeOpen(slot, "lib$lib.so", rate, 1024) }
        if (err != null) return LoadResult(false, err, 0)
        isNativeLoaded = true
        isInstrument = info.optBoolean("instrument", true)
        NativeEngine.setSlotKind(slot, if (isInstrument) 0 else 1)
        lastLoadMs = SystemClock.elapsedRealtime() - t0
        PlaybackService.update(appContext)
        return LoadResult(true, "running (native)", lastLoadMs)
    }

    fun stop() {
        stopInner()
        PlaybackService.update(appContext)
    }

    private fun stopInner() {
        plugin = null  // (load() sets it again after stopping the previous plugin)
        if (isNativeLoaded) {
            isNativeLoaded = false
            onMain { NativeEngine.nativeEditorClose(slot); NativeEngine.nativeClose(slot) }
            NativeEngine.setAapOwned(slot, false)
            return
        }
        val p = proc ?: run { NativeEngine.closeBridge(slot); return }
        stopping = true
        NativeEngine.closeBridge(slot)  // sets stopFlag + wakes the host
        val deadline = SystemClock.elapsedRealtime() + 3000
        while (p.isAlive && SystemClock.elapsedRealtime() < deadline) Thread.sleep(20)
        if (p.isAlive) p.destroyForcibly()
        proc = null
        stopping = false
        NativeEngine.setAapOwned(slot, false)
    }

    val isRunning get() = isNativeLoaded || (proc?.isAlive == true && NativeEngine.hostState(slot) == NativeEngine.HOST_RUNNING)

    fun request(req: String, timeoutMs: Int = 10_000): String? = NativeEngine.request(slot, req, timeoutMs)

    /** CPU time (utime + stime, seconds) of the host process from /proc. */
    fun hostCpuSeconds(): Double? {
        val pid = NativeEngine.statsNamed(slot).hostPid.takeIf { it > 0 } ?: return null
        return runCatching {
            val stat = File("/proc/$pid/stat").readText()
            val f = stat.substringAfterLast(") ").split(" ")
            (f[11].toLong() + f[12].toLong()) / 100.0  // CLK_TCK = 100 on Android
        }.getOrNull()
    }
}
