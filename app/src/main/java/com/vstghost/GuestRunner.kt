// SPDX-License-Identifier: LicenseRef-VstGhost-Proprietary
package com.vstghost

import android.os.Process as AProcess
import android.os.SystemClock
import android.util.Log
import java.io.File
import java.util.concurrent.TimeUnit

const val TAG = "VSTGHOST"

/** One line of guest output, with the time since launch. */
data class OutLine(val tMs: Long, val stream: Char, val text: String)

data class RunResult(
    val command: List<String>,
    val exitCode: Int?,          // null = timed out and killed
    val wallMs: Long,
    val lines: List<OutLine>,
    val mem: ProcSampler.Summary,
    val error: String? = null,
) {
    fun firstLineMatching(prefix: String): OutLine? = lines.firstOrNull { it.text.startsWith(prefix) }
}

/** Runs guest commands (Box64 / Wine) as child processes and captures everything. */
class GuestRunner(private val payload: Payload, private val log: (String) -> Unit) {

    fun run(
        args: List<String>,
        timeoutSec: Long = 180,
        extraEnv: Map<String, String> = emptyMap(),
        useBox64: Boolean = true,
        quiet: Boolean = false,
    ): RunResult {
        val cmd = (if (useBox64) payload.box64Command() else emptyList()) + args
        log("$ ${cmd.joinToString(" ")}")
        val pb = ProcessBuilder(cmd).directory(payload.root)
        val env = pb.environment()
        env.remove("LD_PRELOAD")
        env.putAll(payload.environment(extraEnv))

        val sampler = ProcSampler()
        val lines = java.util.Collections.synchronizedList(mutableListOf<OutLine>())
        val t0 = SystemClock.elapsedRealtime()
        sampler.start()
        val proc = try {
            pb.start()
        } catch (e: Exception) {
            sampler.stop()
            log("  launch failed: $e")
            return RunResult(cmd, -1, 0, emptyList(), sampler.summary(), e.toString())
        }

        fun pump(stream: java.io.InputStream, tag: Char) = Thread {
            stream.bufferedReader().forEachLine { text ->
                val l = OutLine(SystemClock.elapsedRealtime() - t0, tag, text)
                lines.add(l)
                Log.i(TAG, "[$tag] $text")
                if (!quiet) log("  ${l.tMs}ms ${if (tag == 'e') "!" else " "} $text")
            }
        }.apply { start() }

        val tOut = pump(proc.inputStream, 'o')
        val tErr = pump(proc.errorStream, 'e')
        // Process.waitFor(timeout) polls every 100 ms on Android; a blocking waitFor() in a
        // helper thread gives the exact exit time.
        var exitAt = 0L
        val waiter = Thread { proc.waitFor(); exitAt = SystemClock.elapsedRealtime() }.apply { start() }
        waiter.join(TimeUnit.SECONDS.toMillis(timeoutSec))
        val finished = !waiter.isAlive
        val wall = (if (finished) exitAt else SystemClock.elapsedRealtime()) - t0
        if (!finished) {
            log("  TIMEOUT after ${timeoutSec}s, killing")
            proc.destroyForcibly()
        }
        tOut.join(2000); tErr.join(2000)
        sampler.stop()
        val code = if (finished) proc.exitValue() else null
        log("  exit=${code ?: "timeout"} wall=${wall}ms")
        return RunResult(cmd, code, wall, lines.toList(), sampler.summary())
    }
}

/**
 * Polls /proc every 50 ms for processes of our uid (other than the app itself) and records
 * process count and memory. Wine spawns several (wineserver, services.exe, ...), and Android 12+
 * kills "phantom" child processes above a system-wide limit, so the count matters too.
 */
class ProcSampler(private val periodMs: Long = 50) {
    data class Proc(val pid: Int, val name: String, val cmdline: String, val hwmKb: Long)
    data class Summary(val maxProcs: Int, val peakTotalRssKb: Long, val procs: List<Proc>)

    private val myUid = AProcess.myUid()
    private val myPid = AProcess.myPid()
    private val seen = HashMap<Int, Proc>()
    private var maxProcs = 0
    private var peakRss = 0L
    @Volatile private var running = false
    private var thread: Thread? = null

    fun start() {
        running = true
        thread = Thread {
            while (running) { sample(); Thread.sleep(periodMs) }
            sample()
        }.apply { isDaemon = true; start() }
    }

    fun stop() { running = false; thread?.join(1000) }

    @Synchronized fun summary() = Summary(maxProcs, peakRss, seen.values.sortedBy { it.pid })

    /** Processes of our uid other than the app, right now. */
    fun current(): List<Proc> = scan().map { it.first }

    @Synchronized private fun sample() {
        val now = scan()
        maxProcs = maxOf(maxProcs, now.size)
        peakRss = maxOf(peakRss, now.sumOf { it.second })
        for ((p, _) in now) {
            val prev = seen[p.pid]
            seen[p.pid] = if (prev == null || p.hwmKb >= prev.hwmKb) p else prev
        }
    }

    private fun scan(): List<Pair<Proc, Long>> {
        val out = ArrayList<Pair<Proc, Long>>()
        val dirs = File("/proc").list() ?: return out
        for (d in dirs) {
            val pid = d.toIntOrNull() ?: continue
            if (pid == myPid) continue
            try {
                val status = File("/proc/$pid/status").readLines()
                val uid = status.firstOrNull { it.startsWith("Uid:") }?.split(Regex("\\s+"))?.getOrNull(1)?.toIntOrNull()
                if (uid != myUid) continue
                fun kb(key: String) = status.firstOrNull { it.startsWith(key) }
                    ?.split(Regex("\\s+"))?.getOrNull(1)?.toLongOrNull() ?: 0L
                val name = status.firstOrNull { it.startsWith("Name:") }?.substringAfter(':')?.trim() ?: "?"
                val cmd = File("/proc/$pid/cmdline").readText().replace('\u0000', ' ').trim().take(160)
                out += Proc(pid, name, cmd, kb("VmHWM:")) to kb("VmRSS:")
            } catch (_: Exception) {
                // process exited between list and read
            }
        }
        return out
    }
}
