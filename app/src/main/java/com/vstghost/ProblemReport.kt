// SPDX-License-Identifier: LicenseRef-VstGhost-Proprietary
package com.vstghost

import android.content.Context
import android.content.Intent
import android.os.Build
import androidx.core.content.FileProvider
import java.io.File
import java.util.zip.ZipEntry
import java.util.zip.ZipOutputStream

/**
 * ⋮ → Report a problem: zips what is needed to understand a failure (versions, device, installed
 * plugins, slot states, the hosts' recent output, the app's recent logcat lines, the saved sets)
 * into the cache folder and hands it to the share sheet. Nothing is sent by the app itself.
 */
object ProblemReport {
    fun build(ctx: Context, note: String): File {
        val out = File(ctx.cacheDir, "reports").apply { mkdirs() }
        val f = File(out, "vstghost-report-${System.currentTimeMillis() / 1000}.zip")
        ZipOutputStream(f.outputStream().buffered()).use { z ->
            fun put(name: String, text: String) { z.putNextEntry(ZipEntry(name)); z.write(text.toByteArray()); z.closeEntry() }
            val pi = runCatching { ctx.packageManager.getPackageInfo(ctx.packageName, 0) }.getOrNull()
            val payload = Payload(ctx)
            put("summary.txt", buildString {
                appendLine("VST gHost ${pi?.versionName} (${pi?.longVersionCode})  edition=${BuildConfig.EDITION}")
                appendLine("runtime: ${payload.installedVersion}")
                appendLine("device: ${Build.MANUFACTURER} ${Build.MODEL} (${Build.DEVICE}), Android ${Build.VERSION.RELEASE} (SDK ${Build.VERSION.SDK_INT}), ABI ${Build.SUPPORTED_ABIS.joinToString()}")
                appendLine("note: $note")
                appendLine()
                appendLine("native plugins: ${NativePlugins.libs(ctx).joinToString()}")
                appendLine("imported plugins: ${File(ctx.filesDir, "plugins").list()?.sorted()?.joinToString()}")
                appendLine("slot assignments: ${SlotManager.assignments()}")
                for (s in Bridge.allSlots()) {
                    val b = Bridge.get(ctx, s)
                    if (b.plugin != null) appendLine("slot $s: ${b.plugin?.name} running=${b.isRunning} native=${b.isNativeLoaded} instrument=${b.isInstrument} loadMs=${b.lastLoadMs}")
                }
                appendLine("stats slot 1: ${runCatching { NativeEngine.stats(1).joinToString { "%.1f".format(it) } }.getOrNull()}")
            })
            for (s in Bridge.allSlots()) {
                val b = Bridge.get(ctx, s)
                if (b.plugin != null) put("host-slot$s.log", b.recentLog(200).joinToString("\n"))
            }
            put("logcat.txt", runCatching {
                ProcessBuilder("logcat", "-d", "-t", "600", "--pid=${android.os.Process.myPid()}").redirectErrorStream(true).start()
                    .inputStream.bufferedReader().readText()
            }.getOrElse { "logcat unavailable: $it" })
            File(ctx.filesDir, "sets").listFiles()?.forEach { put("sets/${it.name}", it.readText()) }
            File(ctx.filesDir, "slots.json").takeIf { it.isFile }?.let { put("slots.json", it.readText()) }
            File(ctx.filesDir, "installer.log").takeIf { it.isFile }?.let { put("installer.log", it.readText().takeLast(200_000)) }
            for (p in listOf("player", "mixer", "midi")) put("prefs-$p.txt", ctx.getSharedPreferences(p, Context.MODE_PRIVATE).all.toString())
        }
        return f
    }

    fun share(ctx: Context, f: File) {
        val uri = FileProvider.getUriForFile(ctx, "${ctx.packageName}.files", f)
        val send = Intent(Intent.ACTION_SEND).apply {
            type = "application/zip"
            putExtra(Intent.EXTRA_SUBJECT, "VST gHost problem report")
            putExtra(Intent.EXTRA_STREAM, uri)
            addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
        }
        ctx.startActivity(Intent.createChooser(send, "Send the report with…"))
    }
}
