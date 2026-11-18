// SPDX-License-Identifier: GPL-3.0-or-later
package com.vstghost

import android.content.Context
import android.os.Build
import android.os.SystemClock
import org.json.JSONObject
import java.io.File
import java.nio.file.Files
import java.nio.file.StandardCopyOption
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

/**
 * Phase 1: load real Windows VST3 plugins in the bridge host (vstghost-host.exe under Wine +
 * Box64), print their info, and render the test MIDI file offline to WAV.
 *
 * Plugins come from <external files>/plugins (adb push) and are copied to files/plugins.
 * Every host invocation is its own process inside one long-lived Wine session, so a crashing
 * plugin only fails its own row.
 */
class Phase1(private val context: Context, private val log: (String) -> Unit) {

    private val payload = Payload(context)
    private val runner = GuestRunner(payload, log)
    private val report = StringBuilder()
    private fun md(s: String = "") { report.appendLine(s) }

    private val extDir = context.getExternalFilesDir(null)
    private val pluginSrc = extDir?.let { File(it, "plugins") }
    private val pluginDir = File(context.filesDir, "plugins")
    private val outDir = File(context.filesDir, "phase1-out")
    private val host get() = "${payload.root}/host/vstghost-host.exe"
    private val wine get() = "${payload.root}/wine/bin/wine"

    data class Row(
        val name: String, var kind: String = "?", var loads: Boolean = false, var renders: Boolean = false,
        var params: Int = 0, var loadMs: Double = Double.NaN, var rtFactor: Double = Double.NaN,
        var p99: Double = Double.NaN, var maxBlock: Double = Double.NaN, var budget: Double = Double.NaN,
        var silent: Boolean? = null, var nonFinite: Long = 0, var peak: String = "", var hostHwmMb: Double = Double.NaN,
        var problem: String = "",
    )

    fun reportFile() = File(extDir ?: context.filesDir, "phase1-report.md")

    fun runAll(blockSize: Int = 256, reinstall: Boolean = false): String {
        md("# VST gHost: Phase 1 report (offline render)")
        md()
        md("Generated ${SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.US).format(Date())} on " +
           "${Build.MANUFACTURER} ${Build.MODEL}, Android ${Build.VERSION.RELEASE}, edition **${BuildConfig.EDITION}**. " +
           "All numbers measured on this device in this run.")
        md()
        if (reinstall || payload.installedVersion == null) {
            log("installing payload")
            val r = payload.install(null)
            if (!r.ok) return finish("Payload install failed: ${r.message}")
        }
        if (payload.isPlay) payload.buildShim()
        md("- payload: `${payload.installedVersion}`")
        if (!File(host).isFile) return finish("Bridge host missing from payload (rebuild payload with the `host` step).")

        // 1. Plugins: copy from external storage into internal storage.
        val copied = syncPlugins()
        val plugins = pluginDir.listFiles()?.filter { it.name.endsWith(".vst3") }?.sortedBy { it.name.lowercase() }.orEmpty()
        md("- plugins: ${plugins.joinToString { "`${it.name}`" }} ($copied)")
        if (plugins.isEmpty()) return finish("No plugins. Push them to `${pluginSrc}` first.")
        outDir.mkdirs()

        // 2. One Wine session for everything.
        runner.run(listOf("${payload.root}/wine/bin/wineserver", "-k"), timeoutSec = 20, quiet = true)
        runner.run(listOf("${payload.root}/wine/bin/wineserver", "-p"), timeoutSec = 30, quiet = true)
        val warm = runner.run(listOf(wine, host, "scan", pluginDir.path), timeoutSec = 120)
        md("- Wine session start + `scan`: ${warm.wallMs} ms (exit ${warm.exitCode})")
        md("- render: test MIDI `tests/phase1.mid` (20 s + 3 s tail), 48 kHz, block $blockSize, VST3 offline mode; " +
           "effects get the host's built-in 8 s test signal (+3 s tail)")
        md()

        val rows = plugins.map { testPlugin(it, blockSize) }
        runner.run(listOf("${payload.root}/wine/bin/wineserver", "-k"), timeoutSec = 20, quiet = true)

        // 3. Compatibility table goes first in the final report.
        val table = StringBuilder()
        table.appendLine("## Compatibility")
        table.appendLine()
        table.appendLine("| Plugin | Type | Loads | Params | Load (ms) | Renders | Render speed (× real time) | Block p99 / max (ms, budget ${"%.2f".format(rows.firstOrNull()?.budget ?: 0.0)}) | Output peak (dBFS) | Host peak RSS | Notes |")
        table.appendLine("|---|---|---|---|---|---|---|---|---|---|---|")
        for (r in rows) {
            table.appendLine("| ${r.name} | ${r.kind} | ${yn(r.loads)} | ${if (r.loads) r.params else "-"} | ${f1(r.loadMs)} | " +
                "${yn(r.renders)}${if (r.silent == true) " (silent!)" else ""} | ${f1(r.rtFactor)} | ${f2(r.p99)} / ${f2(r.maxBlock)} | " +
                "${r.peak.ifEmpty { "-" }} | ${if (r.hostHwmMb.isNaN()) "-" else "%.0f MB".format(r.hostHwmMb)} | ${r.problem.ifEmpty { "-" }} |")
        }
        table.appendLine()
        table.appendLine("WAVs: `${File(extDir, "phase1-out")}` (pull with adb, compare to the PC reference with `tools/compare_wav.py`).")
        table.appendLine()
        val header = report.toString()
        report.clear()
        report.append(header.substringBefore("\n\n") + "\n\n")  // title
        report.append(table)
        report.append(header.substringAfter("\n\n"))
        return finish()
    }

    private fun testPlugin(dir: File, blockSize: Int): Row {
        val name = dir.name.removeSuffix(".vst3")
        val row = Row(name)
        md("## $name")
        log("=== $name: info")
        val info = runner.run(listOf(wine, host, "info", dir.path), timeoutSec = 180, quiet = true)
        val ij = lastJson(info)
        if (ij == null || !ij.optBoolean("ok")) {
            row.problem = describeFailure(info, ij)
            md("- **info failed**: ${row.problem}")
            stderrTail(info)
            return row
        }
        row.loads = true
        row.kind = if (ij.optBoolean("instrument")) "instrument" else "effect"
        row.params = ij.optInt("paramCount")
        row.loadMs = ij.optDouble("loadTotalMs")
        md("- ${ij.optString("vendor")} ${ij.optString("version")}, ${ij.optString("sdk")}, `${ij.optString("subCategories")}`")
        md("- buses: in ${busText(ij, "audioIn")}, out ${busText(ij, "audioOut")}, events ${ij.optJSONArray("eventIn")?.length() ?: 0}; " +
           "separate controller: ${ij.optBoolean("separateController")}, IMidiMapping: ${ij.optBoolean("midiMapping")}")
        md("- load ${f1(row.loadMs)} ms (LoadLibrary ${f1(ij.optJSONObject("loadMs")?.optDouble("loadLibrary") ?: Double.NaN)} ms), " +
           "unload ${f1(ij.optDouble("unloadMs"))} ms; `info` process wall ${info.wallMs} ms")
        val params = ij.optJSONArray("params")
        if (params != null) {
            val firstReal = (0 until params.length()).map { params.getJSONObject(it) }
                .filter { (it.optInt("flags") and 0x1) != 0 || !it.optString("title").startsWith("MIDI CC") }.take(8)
            md("- ${row.params} parameters, e.g. " + firstReal.joinToString { "${it.optString("title")} = ${it.optString("defaultText")}" })
        }

        log("=== $name: render")
        val wav = File(outDir, "$name.wav")
        val render = runner.run(
            listOf(wine, host, "render", dir.path, "--midi", "${payload.root}/tests/phase1.mid",
                   "--out", wav.path, "--block", blockSize.toString()),
            timeoutSec = 900, quiet = true)
        val rj = lastJson(render)
        row.hostHwmMb = render.mem.procs.filter { it.cmdline.contains("vstghost-host") }.maxOfOrNull { it.hwmKb / 1024.0 } ?: Double.NaN
        if (rj == null || !rj.optBoolean("ok")) {
            row.problem = describeFailure(render, rj)
            md("- **render failed**: ${row.problem}")
            stderrTail(render)
            return row
        }
        row.renders = true
        row.rtFactor = rj.optDouble("realtimeFactor")
        rj.optJSONObject("blockMs")?.let { row.p99 = it.optDouble("p99"); row.maxBlock = it.optDouble("max"); row.budget = it.optDouble("budget") }
        row.silent = rj.optBoolean("silent")
        row.nonFinite = rj.optLong("nonFinite")
        row.peak = rj.optJSONArray("peakDb")?.let { a -> (0 until a.length()).joinToString(" / ") { "%.1f".format(a.getDouble(it)) } } ?: ""
        if (row.nonFinite > 0) row.problem = "${row.nonFinite} NaN/Inf samples"
        if (row.silent == true) row.problem = (row.problem + " output silent").trim()
        md("- render: ${f1(rj.optDouble("audioSec"))} s of audio in ${f1(rj.optDouble("renderMs"))} ms → **${f1(row.rtFactor)}× real time**; " +
           "block p50/p99/max ${f3(rj.optJSONObject("blockMs")?.optDouble("p50") ?: Double.NaN)} / ${f2(row.p99)} / ${f2(row.maxBlock)} ms " +
           "(budget ${f2(row.budget)} ms)")
        md("- notes ${rj.optInt("notes")}, CC/bend/aftertouch mapped ${rj.optInt("ccMapped")}, unmapped ${rj.optInt("ccUnmapped")}; " +
           "peak ${row.peak} dBFS; non-finite samples ${row.nonFinite}")
        md("- host process peak RSS ${f1(row.hostHwmMb)} MB; `render` process wall ${render.wallMs} ms")
        // Copy the WAV out for adb pull.
        extDir?.let { e ->
            val dst = File(e, "phase1-out/${wav.name}")
            dst.parentFile?.mkdirs()
            Files.copy(wav.toPath(), dst.toPath(), StandardCopyOption.REPLACE_EXISTING)
        }
        md()
        return row
    }

    private fun syncPlugins(): String {
        val src = pluginSrc ?: return "no external storage"
        if (!src.isDirectory) return "nothing new in $src"
        var files = 0; var bytes = 0L
        val t0 = SystemClock.elapsedRealtime()
        src.walkTopDown().filter { it.isFile }.forEach { f ->
            val rel = f.relativeTo(src).path
            val dst = File(pluginDir, rel)
            if (dst.isFile && dst.length() == f.length() && dst.lastModified() >= f.lastModified()) return@forEach
            dst.parentFile?.mkdirs()
            Files.copy(f.toPath(), dst.toPath(), StandardCopyOption.REPLACE_EXISTING)
            files++; bytes += f.length()
        }
        return "copied $files files, ${"%.1f".format(bytes / 1048576.0)} MB in ${SystemClock.elapsedRealtime() - t0} ms"
    }

    private fun lastJson(r: RunResult): JSONObject? =
        r.lines.filter { it.stream == 'o' && it.text.trimStart().startsWith("{") }.lastOrNull()
            ?.let { runCatching { JSONObject(it.text) }.getOrNull() }

    private fun describeFailure(r: RunResult, j: JSONObject?): String = when {
        r.exitCode == null -> "timed out"
        j == null -> "no result (exit ${r.exitCode}); see stderr"
        j.optString("error") == "crashed" ->
            "crashed ${j.optString("exceptionHex")} in `${j.optString("module").substringAfterLast('\\')}` during ${j.optString("stage")}"
        else -> "${j.optString("error")} (stage ${j.optString("stage")}, exit ${r.exitCode})"
    }

    private fun stderrTail(r: RunResult) {
        val tail = r.lines.filter { it.stream == 'e' && !it.text.startsWith("[BOX64]") && !it.text.startsWith("\t") }.takeLast(15)
        if (tail.isEmpty()) return
        md("```")
        tail.forEach { md(it.text) }
        md("```")
    }

    private fun busText(j: JSONObject, key: String): String {
        val a = j.optJSONArray(key) ?: return "none"
        if (a.length() == 0) return "none"
        return (0 until a.length()).joinToString { a.getJSONObject(it).let { b -> "${b.optString("name")} (${b.optInt("channels")}ch)" } }
    }

    private fun finish(note: String? = null): String {
        if (note != null) md("**$note**")
        val text = report.toString()
        listOf(File(context.filesDir, "phase1-report.md"), reportFile()).forEach {
            runCatching { it.parentFile?.mkdirs(); it.writeText(text); log("report written: $it") }
        }
        return text
    }

    private fun yn(b: Boolean) = if (b) "✅" else "❌"
    private fun f1(d: Double) = if (d.isNaN()) "-" else "%.1f".format(d)
    private fun f2(d: Double) = if (d.isNaN()) "-" else "%.2f".format(d)
    private fun f3(d: Double) = if (d.isNaN()) "-" else "%.3f".format(d)
}
