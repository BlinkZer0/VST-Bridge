// SPDX-License-Identifier: GPL-3.0-or-later
package com.vstghost

import android.content.Context
import android.net.Uri
import android.provider.DocumentsContract
import android.provider.OpenableColumns
import java.io.File
import java.io.InputStream
import java.util.zip.ZipInputStream

/**
 * "Add plugin": copies Windows VST3 plugins the user picks into files/plugins, where the player and
 * the slots find them. Accepts
 *   - a single-file .vst3 (a DLL),
 *   - a .zip containing .vst3 bundles or files anywhere inside (the usual download format),
 *   - a .vst3 bundle folder (picked with the folder picker).
 */
class PluginImport(private val ctx: Context) {
    data class Result(val imported: List<String>, val message: String)

    private val pluginDir = File(ctx.filesDir, "plugins").apply { mkdirs() }

    fun importDocument(uri: Uri): Result {
        val name = displayName(uri) ?: "plugin"
        val lower = name.lowercase()
        return when {
            lower.endsWith(".zip") -> ctx.contentResolver.openInputStream(uri)!!.use { importZip(it) }
            lower.endsWith(".vst3") -> {
                val dst = File(pluginDir, safeName(name))
                ctx.contentResolver.openInputStream(uri)!!.use { inp -> dst.outputStream().use { inp.copyTo(it) } }
                Result(listOf(dst.name.removeSuffix(".vst3")), "Imported ${dst.name}")
            }
            lower.endsWith(".dll") -> Result(emptyList(), "$name looks like a VST2 plugin; only VST3 is supported so far.")
            lower.endsWith(".exe") || lower.endsWith(".msi") ->
                Result(emptyList(), "$name is an installer. Installers aren't supported yet: use the plugin's zip/portable download, or copy the .vst3 out of an installed copy.")
            else -> Result(emptyList(), "$name is not a .vst3 or .zip")
        }
    }

    /** Every .vst3 inside the zip, bundle folders included. */
    fun importZip(input: InputStream): Result {
        val got = linkedSetOf<String>()
        ZipInputStream(input.buffered()).use { zip ->
            while (true) {
                val e = zip.nextEntry ?: break
                val parts = e.name.replace('\\', '/').split('/').filter { it.isNotEmpty() && it != "." }
                if (parts.any { it == ".." }) continue  // never write outside the plugin folder
                val i = parts.indexOfFirst { it.lowercase().endsWith(".vst3") }
                if (i < 0) continue
                val bundle = safeName(parts[i])
                val rest = parts.drop(i + 1)
                val dst = if (rest.isEmpty()) File(pluginDir, bundle) else File(File(pluginDir, bundle), rest.joinToString("/"))
                if (e.isDirectory) { dst.mkdirs(); continue }
                dst.parentFile?.mkdirs()
                dst.outputStream().use { zip.copyTo(it) }
                got += bundle.removeSuffix(".vst3")
            }
        }
        return if (got.isEmpty()) Result(emptyList(), "No .vst3 plugin found in that zip (installer-only download?)")
        else Result(got.toList(), "Imported ${got.joinToString()}")
    }

    /** A .vst3 bundle folder picked with ACTION_OPEN_DOCUMENT_TREE. */
    fun importTree(treeUri: Uri): Result {
        val rootId = DocumentsContract.getTreeDocumentId(treeUri)
        val rootName = queryName(DocumentsContract.buildDocumentUriUsingTree(treeUri, rootId)) ?: "plugin.vst3"
        if (!rootName.lowercase().endsWith(".vst3")) return Result(emptyList(), "Pick the .vst3 folder itself (got \"$rootName\")")
        val dst = File(pluginDir, safeName(rootName))
        copyTree(treeUri, rootId, dst)
        return Result(listOf(dst.name.removeSuffix(".vst3")), "Imported ${dst.name}")
    }

    private fun copyTree(treeUri: Uri, docId: String, dst: File) {
        dst.mkdirs()
        val children = DocumentsContract.buildChildDocumentsUriUsingTree(treeUri, docId)
        ctx.contentResolver.query(children, arrayOf(
            DocumentsContract.Document.COLUMN_DOCUMENT_ID,
            DocumentsContract.Document.COLUMN_DISPLAY_NAME,
            DocumentsContract.Document.COLUMN_MIME_TYPE), null, null, null)?.use { c ->
            while (c.moveToNext()) {
                val id = c.getString(0)
                val name = safeName(c.getString(1))
                if (c.getString(2) == DocumentsContract.Document.MIME_TYPE_DIR) {
                    copyTree(treeUri, id, File(dst, name))
                } else {
                    val uri = DocumentsContract.buildDocumentUriUsingTree(treeUri, id)
                    ctx.contentResolver.openInputStream(uri)?.use { inp -> File(dst, name).outputStream().use { inp.copyTo(it) } }
                }
            }
        }
    }

    // ---- "Install plugin": files found in the usual download places (needs All files access)

    data class Candidate(val file: File, val where: String, val installable: Boolean, val note: String)

    /** .vst3 files/bundles, zips and installers in Downloads, Documents, the storage root and SD cards. */
    fun scanCommonFolders(): List<Candidate> {
        val roots = linkedMapOf<File, String>()
        val ext = android.os.Environment.getExternalStorageDirectory()
        roots[File(ext, "Download")] = "Download"
        roots[File(ext, "Documents")] = "Documents"
        roots[ext] = "Internal storage"
        for (d in ctx.getExternalFilesDirs(null).filterNotNull()) {
            // /storage/XXXX-XXXX/Android/data/<pkg>/files -> the card's root
            val card = generateSequence(d) { it.parentFile }.firstOrNull { it.name == "Android" }?.parentFile ?: continue
            if (card.path != ext.path) { roots[File(card, "Download")] = "SD card/Download"; roots[card] = "SD card" }
        }
        val out = mutableListOf<Candidate>()
        val seen = HashSet<String>()
        fun add(c: Candidate) { if (seen.add(c.file.path)) out += c }
        for ((root, label) in roots) {
            if (!root.isDirectory) continue
            val depth = if (label.endsWith("Download") || label == "Documents") 2 else 1
            root.walkTopDown().maxDepth(depth)
                // A .vst3 folder is a bundle: list it, don't walk into it. (A directory the filter
                // rejects is not yielded by the walk, so it is added here.) Directories at maxDepth are
                // yielded as plain entries and handled below.
                .onEnter { d ->
                    val n = d.name.lowercase()
                    when {
                        n == "android" -> false
                        n.endsWith(".vst3") -> { add(Candidate(d, label, true, "VST3 bundle")); false }
                        else -> true
                    }
                }
                .forEach { f ->
                    val n = f.name.lowercase()
                    val c = when {
                        n.endsWith(".vst3") -> Candidate(f, label, true, if (f.isDirectory) "VST3 bundle" else "VST3")
                        n.endsWith(".zip") && f.isFile && f.length() < 600L shl 20 -> Candidate(f, label, true, "zip")
                        n.endsWith(".7z") && f.isFile -> Candidate(f, label, false, "7z archive: unpack it first")
                        (n.endsWith(".exe") || n.endsWith(".msi")) && f.isFile -> Candidate(f, label, true, "installer: runs under Wine")
                        else -> null
                    } ?: return@forEach
                    add(c)
                }
        }
        return out.sortedWith(compareBy({ !it.installable }, { it.where }, { it.file.name.lowercase() }))
    }

    /** Imports a file found by [scanCommonFolders]; [status] gets progress text for slow steps. */
    fun importFile(f: File, status: (String) -> Unit = {}): Result {
        val lower = f.name.lowercase()
        return when {
            lower.endsWith(".exe") || lower.endsWith(".msi") -> runInstaller(f, status)
            lower.endsWith(".zip") -> f.inputStream().use { importZip(it) }
            lower.endsWith(".vst3") && f.isDirectory -> {
                val dst = File(pluginDir, safeName(f.name))
                dst.deleteRecursively()
                f.copyRecursively(dst, overwrite = true)
                Result(listOf(dst.name.removeSuffix(".vst3")), "Imported ${dst.name}")
            }
            lower.endsWith(".vst3") -> {
                val dst = File(pluginDir, safeName(f.name))
                f.copyTo(dst, overwrite = true)
                Result(listOf(dst.name.removeSuffix(".vst3")), "Imported ${dst.name}")
            }
            else -> Result(emptyList(), "${f.name}: only .vst3 and .zip can be installed")
        }
    }

    // ---- Installers (.exe / .msi): run silently under Wine, then collect the plugins they installed

    /** "inno", "nsis", "msi", "exe" (unknown kind) or null for a file that is not an installer. */
    private fun installerKind(f: File): String? {
        val n = f.name.lowercase()
        if (n.endsWith(".msi")) return "msi"
        if (!n.endsWith(".exe")) return null
        val head = f.inputStream().use { s ->
            val b = ByteArray(minOf(f.length(), 8_000_000L).toInt()); var o = 0
            while (o < b.size) { val r = s.read(b, o, b.size - o); if (r < 0) break; o += r }
            String(b, 0, o, Charsets.ISO_8859_1)
        }
        return when { "Inno Setup" in head -> "inno"; "Nullsoft" in head -> "nsis"; else -> "exe" }
    }

    /**
     * Runs a plugin installer unattended in the Wine prefix (Inno Setup: /VERYSILENT, NSIS: /S,
     * MSI: msiexec /qn) and moves the .vst3 bundles it left in Common Files\VST3 into files/plugins.
     * Data the installer puts elsewhere in the prefix (ProgramData, the user folder) stays there,
     * where the plugin looks for it; the prefix keeps those folders across app updates.
     * An installer that insists on a window never finishes: it is killed after 10 minutes.
     */
    fun runInstaller(f: File, status: (String) -> Unit): Result {
        val kind = installerKind(f) ?: return Result(emptyList(), "${f.name}: not an installer")
        val payload = Payload(ctx)
        Bridge.ensureSession(payload)
        val tmp = File(payload.root, "tmp").apply { mkdirs() }
        val exe = File(tmp, "installer." + f.extension.lowercase())
        f.copyTo(exe, overwrite = true)
        val winExe = "Z:" + exe.path.replace('/', '\\')
        val vst3Dir = File(payload.root, "prefix/drive_c/Program Files/Common Files/VST3").apply { mkdirs() }
        val before = vst3Dir.listFiles()?.map { it.name }?.toSet().orEmpty()
        val args = when (kind) {
            "msi" -> listOf("msiexec", "/i", winExe, "/qn", "/norestart")
            "inno" -> listOf(winExe, "/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART", "/SP-", "/NOCANCEL")
            "nsis" -> listOf(winExe, "/S")
            else -> listOf(winExe, "/S", "/VERYSILENT", "/quiet")   // the usual flags; a GUI-only installer times out
        }
        status("Running ${f.name} under Wine ($kind, unattended)… this can take a few minutes.")
        val cmd = payload.box64Command() + listOf("${payload.root}/wine/bin/wine") + args
        val pb = ProcessBuilder(cmd).directory(payload.root).redirectErrorStream(true)
        pb.environment().remove("LD_PRELOAD")
        pb.environment().putAll(payload.environment(mapOf(
            "WINEDEBUG" to "-all,err+module", "WINEDLLOVERRIDES" to "mscoree=d;mshtml=d;winemenubuilder.exe=d")))
        val p = pb.start()
        val tail = ArrayDeque<String>()
        Thread { p.inputStream.bufferedReader().forEachLine { synchronized(tail) { tail.addLast(it); while (tail.size > 20) tail.removeFirst() } } }
            .apply { isDaemon = true; start() }
        val finished = p.waitFor(10, java.util.concurrent.TimeUnit.MINUTES)
        exe.delete()
        if (!finished) {
            p.destroyForcibly()
            return Result(emptyList(), "${f.name}: the installer did not finish in 10 minutes (it probably needs a window, which is not supported)")
        }
        val fresh = vst3Dir.listFiles().orEmpty().filter { it.name !in before && it.name.lowercase().endsWith(".vst3") }
        if (fresh.isEmpty()) {
            val log = synchronized(tail) { tail.joinToString(" | ") }.take(300)
            return Result(emptyList(), "${f.name}: the installer finished (exit ${p.exitValue()}) but left no .vst3 in Common Files\\VST3" +
                if (log.isNotEmpty()) " · $log" else "")
        }
        val names = mutableListOf<String>()
        for (src in fresh) {
            val dst = File(pluginDir, safeName(src.name))
            dst.deleteRecursively()
            if (!src.renameTo(dst)) {
                if (src.isDirectory) src.copyRecursively(dst, overwrite = true) else src.copyTo(dst, overwrite = true)
                src.deleteRecursively()
            }
            names += dst.name.removeSuffix(".vst3")
        }
        return Result(names, "Installed ${names.joinToString()} from ${f.name}")
    }

    private fun displayName(uri: Uri): String? = queryName(uri) ?: uri.lastPathSegment?.substringAfterLast('/')

    private fun queryName(uri: Uri): String? =
        ctx.contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)?.use { c ->
            if (c.moveToFirst()) c.getString(0) else null
        }

    private fun safeName(n: String) = n.replace('/', '_').replace('\\', '_').trim().ifEmpty { "plugin" }
}
