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

    private fun displayName(uri: Uri): String? = queryName(uri) ?: uri.lastPathSegment?.substringAfterLast('/')

    private fun queryName(uri: Uri): String? =
        ctx.contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)?.use { c ->
            if (c.moveToFirst()) c.getString(0) else null
        }

    private fun safeName(n: String) = n.replace('/', '_').replace('\\', '_').trim().ifEmpty { "plugin" }
}
