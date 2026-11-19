// SPDX-License-Identifier: LicenseRef-VstGhost-Proprietary
package com.vstghost

import android.app.ActivityManager
import android.content.Context
import android.os.Build
import android.os.SystemClock
import java.io.File
import java.io.IOException
import java.nio.file.FileVisitResult
import java.nio.file.Files
import java.nio.file.Path
import java.nio.file.SimpleFileVisitor
import java.nio.file.attribute.BasicFileAttributes
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

/**
 * Phase 0 feasibility tests: does Wine + Box64 start from inside our APK, how long does it take,
 * and how much memory does it use. Writes a Markdown report.
 */
class Phase0(private val context: Context, private val log: (String) -> Unit) {

    private val payload = Payload(context)
    private val runner = GuestRunner(payload, log)
    private val report = StringBuilder()
    private val benchRe = Regex("""BENCH ms=([0-9.]+) checksum=(\S+)""")

    private fun md(s: String = "") { report.appendLine(s) }

    fun reportFiles(): List<File> = listOfNotNull(
        File(context.filesDir, "phase0-report.md"),
        context.getExternalFilesDir(null)?.let { File(it, "phase0-report.md") },
    )

    fun runAll(forceReinstall: Boolean): String {
        val stamp = SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.US).format(Date())
        md("# VST gHost: Phase 0 report")
        md()
        md("Generated $stamp on the device by the app (autorun). Every number below was measured on this device during this run.")
        md()
        deviceSection()

        // 1. Payload
        md("## 1. Payload install")
        if (forceReinstall || payload.installedVersion == null) {
            val t0 = SystemClock.elapsedRealtime()
            var lastPct = -1
            val r = payload.install { done, total ->
                val pct = if (total > 0) (done * 100 / total).toInt() else 0
                if (pct / 10 != lastPct / 10) { lastPct = pct; log("  extracting… $pct%") }
                true
            }
            val wall = SystemClock.elapsedRealtime() - t0
            md("- source: `${r.source ?: "none"}`")
            md("- archive size: ${mb(r.archiveBytes)}")
            md("- result: **${if (r.ok) "OK" else "FAILED"}**: ${r.message}")
            md("- extractor stats: `${r.statsJson ?: "-"}`")
            md("- install wall time (extract + swap): **$wall ms**")
            log(r.message)
            if (!r.ok) return finish()
        } else {
            md("- already installed (`${payload.installedVersion}`); not re-extracted this run")
        }
        md("- on-disk size of rootfs: ${mb(diskUsage(payload.root.toPath()))}")
        if (payload.isPlay) md("- shim: ${payload.buildShim()}")
        md()

        // 2. Box64 alone
        md("## 2. Box64 starts")
        val v = runner.run(listOf("--version"), timeoutSec = 30, extraEnv = mapOf("BOX64_NOBANNER" to "0"))
        section(v, "box64 --version")
        if (v.exitCode != 0) return finish("Box64 did not start; the remaining tests were skipped.")

        // 3. Wine version (no wineserver needed)
        md("## 3. Wine loader")
        val wv = runner.run(listOf("${payload.root}/wine/bin/wine", "--version"), timeoutSec = 60)
        section(wv, "wine --version")

        // 4. Cold start
        md("## 4. Cold start: `wine hello.exe` with no wineserver running")
        stopWineserver()
        val hello = listOf("${payload.root}/wine/bin/wine", "${payload.root}/tests/hello.exe")
        val cold = runner.run(hello, timeoutSec = 300)
        section(cold, "cold")
        helloMetrics(cold)
        val lingerMs = waitForNoGuests(15_000)
        md("- guest processes all exited ${if (lingerMs >= 0) "$lingerMs ms after hello.exe returned" else "**not within 15 s**"}")
        md()

        // 5. Warm start
        md("## 5. Warm start: wineserver already running (`wineserver -p`)")
        val ws = runner.run(listOf("${payload.root}/wine/bin/wineserver", "-p"), timeoutSec = 60)
        val sockMs = waitForServerSocket(60_000)
        md("- `wineserver -p` exit=${ws.exitCode}, server socket appeared after ${if (sockMs >= 0) "$sockMs ms" else "**never (60 s)**"}")
        val warm = (1..3).map { i ->
            val r = runner.run(hello, timeoutSec = 180, quiet = i > 1)
            md("### Warm run $i")
            helloMetrics(r)
            r
        }
        section(warm.first(), "warm run 1 (full output)")
        stopWineserver()

        // 6. Native benchmark, same kernel
        md("## 6. Box64 overhead on DSP code")
        val natRuns = (1..3).map { NativeBridge.benchmark() }
        val nat = natRuns.minBy { it[0] }
        md("- native arm64 (best of 3: ${natRuns.joinToString { "%.1f".format(it[0]) }} ms): **${"%.1f".format(nat[0])} ms**, checksum `${"%.17g".format(nat[1])}`")
        md("- note: neither run is pinned to a core; differences of ±20% can be big-vs-mid core scheduling, not Box64")
        val boxBench = (listOf(cold) + warm).mapNotNull { r -> r.lines.firstNotNullOfOrNull { benchRe.find(it.text) } }
        if (boxBench.isNotEmpty()) {
            val best = boxBench.minBy { it.groupValues[1].toDouble() }
            val ms = best.groupValues[1].toDouble()
            val sum = best.groupValues[2].toDouble()
            md("- x86_64 under Box64 + Wine (best of ${boxBench.size}): **${"%.1f".format(ms)} ms**, checksum `${best.groupValues[2]}`")
            md("- **overhead factor: ${"%.2f".format(ms / nat[0])}×**")
            md("- checksums ${if (sum == nat[1]) "**match** (same floating-point results)" else "**DIFFER** (Box64 float results differ from native arm64)"}")
            md("- the kernel renders 60 s of 32 voices at 48 kHz; real-time factor under Box64 ≈ ${"%.0f".format(60_000.0 / ms)}× (single core)")
        } else {
            md("- no BENCH line from hello.exe, so no comparison is possible")
        }
        md()
        return finish()
    }

    private fun finish(note: String? = null): String {
        if (note != null) { md("**$note**"); md() }
        md("---")
        md("Logcat tag: `VSTGHOST`. Look for SELinux denials with `adb logcat -d | grep avc`.")
        val text = report.toString()
        for (f in reportFiles()) {
            try { f.parentFile?.mkdirs(); f.writeText(text); log("report written: $f") } catch (e: Exception) { log("could not write $f: $e") }
        }
        return text
    }

    private fun deviceSection() {
        val am = context.getSystemService(ActivityManager::class.java)
        val mi = ActivityManager.MemoryInfo().also { am.getMemoryInfo(it) }
        md("## Device")
        md("- device: ${Build.MANUFACTURER} ${Build.MODEL} (${Build.DEVICE})")
        if (Build.VERSION.SDK_INT >= 31) md("- SoC: ${Build.SOC_MANUFACTURER} ${Build.SOC_MODEL}")
        md("- Android ${Build.VERSION.RELEASE} (API ${Build.VERSION.SDK_INT}), patch ${Build.VERSION.SECURITY_PATCH}, build ${Build.DISPLAY}")
        md("- kernel: ${System.getProperty("os.version")}")
        md("- RAM: ${mb(mi.totalMem)} total, ${mb(mi.availMem)} available at start")
        md("- app: ${context.packageName} ${BuildConfig.VERSION_NAME}, edition **${BuildConfig.EDITION}**, targetSdk ${context.applicationInfo.targetSdkVersion}")
        md("- running in emulator: ${if (Build.FINGERPRINT.contains("generic") || Build.HARDWARE.contains("ranchu")) "**YES: results not valid for Box64/latency**" else "no"}")
        md()
    }

    private fun section(r: RunResult, title: String) {
        md("**$title**: exit=${r.exitCode ?: "TIMEOUT"}, wall=${r.wallMs} ms, max guest processes=${r.mem.maxProcs}, peak summed RSS=${mb(r.mem.peakTotalRssKb * 1024)}")
        if (r.error != null) md("- launch error: `${r.error}`")
        md()
        md("```")
        md("$ ${r.command.joinToString(" ")}")
        val shown = if (r.lines.size > 60) r.lines.take(30) + listOf(OutLine(0, ' ', "… ${r.lines.size - 60} lines omitted …")) + r.lines.takeLast(30) else r.lines
        shown.forEach { md("${it.tMs.toString().padStart(6)}ms ${if (it.stream == 'e') "E" else " "} ${it.text}") }
        md("```")
        if (r.mem.procs.isNotEmpty()) {
            md()
            md("| pid | process | peak RSS (VmHWM) |")
            md("|---|---|---|")
            r.mem.procs.forEach { md("| ${it.pid} | `${it.cmdline.ifEmpty { it.name }.replace("|", "\\|")}` | ${mb(it.hwmKb * 1024)} |") }
        }
        md()
    }

    private fun helloMetrics(r: RunResult) {
        val first = r.firstLineMatching("VSTGHOST-HELLO start")
        val done = r.firstLineMatching("VSTGHOST-HELLO done")
        md("- exit=${r.exitCode ?: "TIMEOUT"}; launch → first line of hello.exe output: **${first?.tMs?.let { "$it ms" } ?: "never"}**; " +
           "→ hello.exe done: ${done?.tMs?.let { "$it ms" } ?: "never"}; → wine exited: ${r.wallMs} ms")
        md("- max guest processes: ${r.mem.maxProcs}; peak summed RSS: **${mb(r.mem.peakTotalRssKb * 1024)}**")
        r.lines.firstNotNullOfOrNull { benchRe.find(it.text) }?.let { md("- benchmark: ${it.value}") }
    }

    private fun stopWineserver() {
        runner.run(listOf("${payload.root}/wine/bin/wineserver", "-k"), timeoutSec = 30, quiet = true)
        waitForNoGuests(10_000)
    }

    /** ms until no guest processes remain, or -1 on timeout. */
    private fun waitForNoGuests(timeoutMs: Long): Long {
        val s = ProcSampler()
        val t0 = SystemClock.elapsedRealtime()
        while (SystemClock.elapsedRealtime() - t0 < timeoutMs) {
            if (s.current().isEmpty()) return SystemClock.elapsedRealtime() - t0
            Thread.sleep(100)
        }
        log("guest processes still running: ${s.current().joinToString { it.name }}")
        return -1
    }

    private fun waitForServerSocket(timeoutMs: Long): Long {
        val base = File(payload.root, "prefix/.wineserver")
        val t0 = SystemClock.elapsedRealtime()
        while (SystemClock.elapsedRealtime() - t0 < timeoutMs) {
            if (base.listFiles()?.any { File(it, "socket").exists() } == true) return SystemClock.elapsedRealtime() - t0
            Thread.sleep(50)
        }
        return -1
    }

    private fun diskUsage(p: Path): Long {
        var total = 0L
        if (!Files.exists(p)) return 0
        Files.walkFileTree(p, object : SimpleFileVisitor<Path>() {
            override fun visitFile(file: Path, attrs: BasicFileAttributes): FileVisitResult {
                if (attrs.isRegularFile) total += attrs.size()
                return FileVisitResult.CONTINUE
            }
            override fun visitFileFailed(file: Path, exc: IOException) = FileVisitResult.CONTINUE
        })
        return total
    }

    private fun mb(bytes: Long) = "%.1f MB".format(bytes / 1048576.0)
}
