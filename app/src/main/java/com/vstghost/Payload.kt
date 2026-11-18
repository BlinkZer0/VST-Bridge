// SPDX-License-Identifier: GPL-3.0-or-later
package com.vstghost

import android.content.Context
import android.os.ParcelFileDescriptor
import android.system.Os
import java.io.File
import java.io.IOException
import java.nio.file.FileVisitResult
import java.nio.file.Files
import java.nio.file.Path
import java.nio.file.SimpleFileVisitor
import java.nio.file.attribute.BasicFileAttributes

/**
 * Where the Wine/Box64 payload lives on the device and how Box64 is launched.
 *
 * Sideload edition (targetSdk 28): everything is extracted to filesDir/rootfs and Box64 is
 * exec'd from there; its PT_INTERP was rewritten from [PLACEHOLDER] during extraction.
 *
 * Play edition (targetSdk 36): files in filesDir can't be exec'd or mmap'd PROT_EXEC. The arm64
 * loader, Box64 and glibc ship as lib*.so in nativeLibraryDir. A shim dir of soname symlinks
 * (libc.so.6 -> nativeLibraryDir/libc_so_6.so) lets the glibc loader find them, and Box64 is
 * started through the loader explicitly. The x86_64 part of the payload is plain data.
 */
class Payload(private val context: Context) {

    companion object {
        /** Must match PLACEHOLDER in payload/build.sh: "/vstghost_root" + 50 underscores = 64 chars. */
        val PLACEHOLDER = "/vstghost_root" + "_".repeat(50)
        const val ARCHIVE_NAME = "vstghost-payload.tar.zst"
        const val JNILIBS_MAP = "jnilibs.map"
    }

    val isPlay: Boolean = BuildConfig.EDITION == "play"
    val root = File(context.filesDir, "rootfs")
    private val shimDir = File(context.filesDir, "plibs")
    private val nativeLibDir = File(context.applicationInfo.nativeLibraryDir)

    val installedVersion: String?
        get() = File(root, ".installed").takeIf { it.isFile }?.readText()?.trim()

    /** Candidate archive locations, in the order we try them. */
    fun archiveCandidates(): List<String> = buildList {
        if (isPlay) add("asset:$ARCHIVE_NAME")
        context.getExternalFilesDir(null)?.let { add(File(it, ARCHIVE_NAME).path) }
        add(File(context.filesDir, "incoming/$ARCHIVE_NAME").path)
    }

    private class Source(val fd: Int, val offset: Long, val length: Long, val label: String, val close: () -> Unit)

    private fun openSource(): Source? {
        if (isPlay) {
            try {
                val afd = context.assets.openFd(ARCHIVE_NAME)  // needs noCompress "zst"
                return Source(afd.parcelFileDescriptor.fd, afd.startOffset, afd.length, "APK asset") { afd.close() }
            } catch (_: IOException) { /* not bundled in this build */ }
        }
        for (path in archiveCandidates().filterNot { it.startsWith("asset:") }) {
            val f = File(path)
            if (!f.isFile) continue
            val pfd = ParcelFileDescriptor.open(f, ParcelFileDescriptor.MODE_READ_ONLY)
            return Source(pfd.fd, 0, f.length(), f.path) { pfd.close() }
        }
        return null
    }

    data class InstallResult(val ok: Boolean, val message: String, val statsJson: String?, val source: String?, val archiveBytes: Long)

    /** Extracts into rootfs.new, then swaps it in. Blocking; call off the UI thread. */
    fun install(progress: ProgressListener?): InstallResult {
        val src = openSource()
            ?: return InstallResult(false, "No payload found. Looked in: ${archiveCandidates()}", null, null, 0)
        val staging = File(context.filesDir, "rootfs.new")
        try {
            deleteTree(staging)
            val res = NativeBridge.extract(src.fd, src.offset, src.length, staging.path, PLACEHOLDER, root.path, progress)
            if (res.startsWith("ERROR: ")) return InstallResult(false, res, null, src.label, src.length)
            deleteTree(root)
            if (!staging.renameTo(root)) return InstallResult(false, "rename ${staging} -> $root failed", res, src.label, src.length)
            File(root, "tmp").mkdirs()
            File(root, "home").mkdirs()
            val version = File(root, "VERSION").takeIf { it.isFile }?.readText()?.trim() ?: "unknown"
            File(root, ".installed").writeText(version)
            if (isPlay) buildShim()
            return InstallResult(true, "Installed payload $version", res, src.label, src.length)
        } finally {
            src.close()
        }
    }

    /** True if every soname link exists and points at the current nativeLibraryDir. */
    fun shimReady(): Boolean = runCatching {
        val map = context.assets.open(JNILIBS_MAP).bufferedReader().readLines().filter { it.isNotBlank() && !it.startsWith("#") }
        map.all { line ->
            val (soname, file) = line.trim().split(Regex("\\s+"))
            Os.readlink(File(shimDir, soname).path) == File(nativeLibDir, file).path
        }
    }.getOrDefault(false)

    /** Play edition: soname symlinks into nativeLibraryDir, from payload's jnilibs.map. */
    fun buildShim(): String {
        deleteTree(shimDir)
        shimDir.mkdirs()
        val map = try {
            context.assets.open(JNILIBS_MAP).bufferedReader().readLines()
        } catch (_: IOException) {
            return "no $JNILIBS_MAP asset in this build"
        }
        var n = 0
        for (line in map) {
            val parts = line.trim().split(Regex("\\s+"))
            if (parts.size != 2 || line.startsWith("#")) continue
            val (soname, file) = parts
            Os.symlink(File(nativeLibDir, file).path, File(shimDir, soname).path)
            n++
        }
        return "linked $n libraries"
    }

    fun uninstall() {
        deleteTree(root)
        deleteTree(File(context.filesDir, "rootfs.new"))
        deleteTree(shimDir)
    }

    /**
     * Command prefix that runs Box64 in this edition. In the Play build, libbox64.so's PT_INTERP is
     * /data/data/<pkg>/files/plibs/ld-linux-aarch64.so.1 (set by payload/build.sh), a symlink to
     * nativeLibraryDir/libldlinux.so, so Box64 can be exec'd, and can re-exec itself, directly.
     */
    fun box64Command(): List<String> =
        if (isPlay) listOf(File(nativeLibDir, "libbox64.so").path)
        else listOf(File(root, "box64/bin/box64").path)

    fun environment(extra: Map<String, String> = emptyMap()): Map<String, String> {
        val r = root.path
        return linkedMapOf(
            "HOME" to "$r/home",
            "TMPDIR" to "$r/tmp",
            "USER" to "vstghost",
            "TZ" to "UTC",
            "LANG" to "C",
            "PATH" to "$r/wine/bin:/system/bin",
            "LD_LIBRARY_PATH" to if (isPlay) shimDir.path else "$r/glibc/lib",
            "BOX64_LD_LIBRARY_PATH" to "$r/box64/x64lib",
            "BOX64_PATH" to "$r/wine/bin",
            "BOX64_RCFILE" to "$r/etc/box64.box64rc",
            "BOX64_DYNACACHE" to "0",
            "WINEPREFIX" to "$r/prefix",
            "WINEDEBUG" to "fixme-all",
            "WINEDLLOVERRIDES" to "winemenubuilder.exe=d;mscoree=d;mshtml=d",
        ) + extra
    }

    fun describe(): String = buildString {
        appendLine("edition: ${BuildConfig.EDITION} (targetSdk ${context.applicationInfo.targetSdkVersion})")
        appendLine("root: $root")
        appendLine("nativeLibraryDir: $nativeLibDir")
        appendLine("installed payload: ${installedVersion ?: "none"}")
    }
}

/** Recursive delete that never follows symlinks (the Wine prefix has z: -> /). */
fun deleteTree(dir: File) {
    val p: Path = dir.toPath()
    if (!Files.exists(p, java.nio.file.LinkOption.NOFOLLOW_LINKS)) return
    Files.walkFileTree(p, object : SimpleFileVisitor<Path>() {
        override fun visitFile(file: Path, attrs: BasicFileAttributes): FileVisitResult {
            file.toFile().setWritable(true)
            Files.deleteIfExists(file); return FileVisitResult.CONTINUE
        }
        override fun visitFileFailed(file: Path, exc: IOException): FileVisitResult {
            Files.deleteIfExists(file); return FileVisitResult.CONTINUE
        }
        override fun preVisitDirectory(d: Path, attrs: BasicFileAttributes): FileVisitResult {
            d.toFile().setWritable(true); d.toFile().setExecutable(true); return FileVisitResult.CONTINUE
        }
        override fun postVisitDirectory(d: Path, exc: IOException?): FileVisitResult {
            Files.deleteIfExists(d); return FileVisitResult.CONTINUE
        }
    })
}
