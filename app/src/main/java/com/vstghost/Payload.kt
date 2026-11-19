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
        const val WAYLAND_SOCKET = "wl-0"
        /** Must match X64LIBS (+ the xkbregistry stub) in payload/build.sh. */
        const val EMULATED_LIBS = "libwayland-client.so.0:libffi.so.8:libxkbcommon.so.0:libxkbregistry.so.0:" +
            "libfreetype.so.6:libpng16.so.16:libz.so.1:libbz2.so.1.0:libbz2.so.1:libbrotlidec.so.1:libbrotlicommon.so.1:" +
            // Mesa (software OpenGL for plugin editors): emulated too, so it talks to the emulated Wayland client.
            "libEGL.so.1:libGLESv2.so.2:libGL.so.1:libglapi.so.0:libdrm.so.2:libexpat.so.1:libwayland-server.so.0:libwayland-egl.so.1:libzstd.so.1:libtinfo.so.6"
    }

    val isPlay: Boolean = BuildConfig.EDITION == "play"
    val root = File(context.filesDir, "rootfs")
    private val shimDir = File(context.filesDir, "plibs")
    private val nativeLibDir = File(context.applicationInfo.nativeLibraryDir)

    val installedVersion: String?
        get() = File(root, ".installed").takeIf { it.isFile }?.readText()?.trim()

    /**
     * Identity of the archive we would install now: APK update time for the bundled asset (Play),
     * size + date of the pushed file (sideload). Null if there is no archive.
     */
    private fun sourceStamp(): String? {
        if (isPlay) runCatching { context.assets.openFd(ARCHIVE_NAME).use { return "asset:${it.length}:" +
            context.packageManager.getPackageInfo(context.packageName, 0).lastUpdateTime } }
        for (path in archiveCandidates().filterNot { it.startsWith("asset:") }) {
            val f = File(path)
            if (f.isFile) return "${f.path}:${f.length()}:${f.lastModified()}"
        }
        return null
    }

    /** No payload yet, or a different archive than the installed one is available. */
    fun needsInstall(): Boolean {
        if (installedVersion == null) return true
        if (!File(root, "wine/bin/wine").isFile || !File(root, "VERSION").isFile) return true  // damaged tree
        val now = sourceStamp() ?: return false
        return File(root, ".installed-source").takeIf { it.isFile }?.readText()?.trim() != now
    }

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
            // Markers first: if we are killed while deleting the old tree, the next start reinstalls
            // instead of trusting a half-deleted rootfs.
            File(root, ".installed").delete()
            File(root, ".installed-source").delete()
            deleteTree(root)
            if (!staging.renameTo(root)) return InstallResult(false, "rename ${staging} -> $root failed", res, src.label, src.length)
            File(root, "tmp").mkdirs()
            File(root, "home").mkdirs()
            val version = File(root, "VERSION").takeIf { it.isFile }?.readText()?.trim() ?: "unknown"
            File(root, ".installed").writeText(version)
            sourceStamp()?.let { File(root, ".installed-source").writeText(it) }
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
        if (root.isDirectory) {
            EditorDisplay.ensure(this)  // listening before any Wine process starts
            ensureDrives()
        }
        return linkedMapOf(
            "HOME" to "$r/home",
            "TMPDIR" to "$r/tmp",
            "USER" to "vstghost",
            "TZ" to "UTC",
            "LANG" to "C",
            "PATH" to "$r/wine/bin:/system/bin",
            "LD_LIBRARY_PATH" to if (isPlay) shimDir.path else "$r/glibc/lib",
            "BOX64_LD_LIBRARY_PATH" to "$r/box64/x64lib:$r/x64libs",
            // x86_64 libraries we ship for Wine's Wayland driver and fonts: run them emulated
            // (Box64 would otherwise look for native arm64 builds, which the payload doesn't have).
            "BOX64_EMULATED_LIBS" to EMULATED_LIBS,
            // Plugin editors: Wine's Wayland driver talks to our compositor (EditorDisplay).
            "XDG_RUNTIME_DIR" to "$r/run",
            "WAYLAND_DISPLAY" to "$r/run/$WAYLAND_SOCKET",
            "VSTGHOST_WL_ONE_TOPLEVEL" to "1",
            // Wine patch 0004: read PE images into anonymous memory instead of mapping the file.
            // SELinux denies re-enabling execute on a file mapping that was written to (execmod),
            // which relocation does; the Play edition (targetSdk 36) hits it on every DLL whose
            // code section has relocations (dwrite.dll, d2d1.dll, most plugins).
            "VSTGHOST_IMAGE_COPY" to "1",
            // Wine patch 0004: paths the app may not stat read as "not found" (plugins probing
            // parent directories would otherwise die of an access-denied exception).
            "VSTGHOST_DENIED_IS_MISSING" to "1",
            // Mesa: software rendering only, drivers from the payload.
            "LIBGL_ALWAYS_SOFTWARE" to "1",
            "GALLIUM_DRIVER" to "llvmpipe",
            "LP_NUM_THREADS" to "2",  // leave cores for audio
            // 128-bit (SSE) JIT code: Box64 mistranslated some of llvmpipe's AVX texture sampling
            "LP_NATIVE_VECTOR_WIDTH" to "128",
            // ... and LLVM still emitted AVX elsewhere while cpuid advertised it: every D2D bitmap
            // (knobs, image strips) came out blank. Hiding AVX/AVX2 from x86 code fixes it; SSE
            // paths are what Box64 translates best anyway.
            "BOX64_AVX" to "0",
            "BOX64_AVX2" to "0",
            // Direct2D editors (JUCE 8 before its Wine check) are rasterized in software for every
            // frame they present: cap their "refresh rate" at 20 Hz (Wine patch 0003).
            "VSTGHOST_VBLANK_MS" to "50",
            // Plugins live on D: (dosdevices/d: -> files/plugins, see ensureDrives). Some plugins walk
            // up from their own path (Surge XT looks for a portable data folder); from Z: that walk
            // reaches /data/user/0, which an app may not even stat, and they abort.
            "VSTGHOST_PLUGIN_DIR" to File(context.filesDir, "plugins").path,
            "LIBGL_DRIVERS_PATH" to "$r/x64libs/dri",
            "EGL_PLATFORM" to "wayland",
            "BOX64_PATH" to "$r/wine/bin",
            "BOX64_RCFILE" to "$r/etc/box64.box64rc",
            "BOX64_DYNACACHE" to "0",
            "WINEPREFIX" to "$r/prefix",
            "WINEDEBUG" to "fixme-all",
            "WINEDLLOVERRIDES" to "winemenubuilder.exe=d;mscoree=d;mshtml=d",
        ) + extra + debugEnv()  // the debug file wins, that is its point
    }

    /** D: = files/plugins (the payload's prefix ships with only C: and Z:). */
    private fun ensureDrives() {
        val link = File(root, "prefix/dosdevices/d:")
        val target = File(context.filesDir, "plugins").apply { mkdirs() }.path
        val current = runCatching { Os.readlink(link.path) }.getOrNull()
        if (current == target) return
        runCatching { link.delete(); Os.symlink(target, link.path) }
    }

    /**
     * Debug builds: KEY=VALUE lines in files/env.debug are added to every Wine process's environment
     * (Box64 and Wine tuning over adb without rebuilding: `run-as <pkg> sh -c 'echo X=1 > files/env.debug'`).
     */
    private fun debugEnv(): Map<String, String> {
        if (!BuildConfig.DEBUG) return emptyMap()
        val f = File(context.filesDir, "env.debug")
        if (!f.isFile) return emptyMap()
        return f.readLines().mapNotNull { line ->
            val t = line.trim()
            if (t.isEmpty() || t.startsWith("#") || '=' !in t) null
            else t.substringBefore('=').trim() to t.substringAfter('=').trim()
        }.toMap()
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
