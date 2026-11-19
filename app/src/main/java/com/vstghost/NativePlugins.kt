// SPDX-License-Identifier: LicenseRef-VstGhost-Proprietary
package com.vstghost

import android.content.Context
import android.os.Handler
import android.os.Looper
import android.util.Log
import org.json.JSONObject
import java.io.File
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.CountDownLatch

/**
 * Phase 5: plugins built natively for Android from their own JUCE source (native/CMakeLists.txt;
 * one library lib vgn_<target>.so each, found in the APK by that prefix). In plugin lists and slot
 * assignments a native plugin is represented by a placeholder file files/native/<lib>.vgn, so it can
 * travel through the same code as bridged .vst3 plugins; [Bridge.load] recognises it.
 */
object NativePlugins {
    const val EXT = "vgn"
    private const val TAG = "VSTGHOST"

    data class Entry(val lib: String, val name: String, val vendor: String, val instrument: Boolean, val file: File)

    private val infos = ConcurrentHashMap<String, JSONObject>()

    /** Library base names (without "lib" and ".so") packaged in this APK. */
    fun libs(ctx: Context): List<String> =
        File(ctx.applicationInfo.nativeLibraryDir).list()
            ?.filter { it.startsWith("libvgn_") && it.endsWith(".so") }
            ?.map { it.removePrefix("lib").removeSuffix(".so") }?.sorted().orEmpty()

    fun isNative(f: File?) = f?.extension == EXT
    fun fileFor(ctx: Context, lib: String) = File(ctx.filesDir, "native/$lib.$EXT")

    /** Loads and initialises the library (JUCE needs the main thread for this). Null on failure. */
    fun ensure(ctx: Context, lib: String): JSONObject? {
        infos[lib]?.let { return it }
        val r = onMain {
            runCatching {
                System.loadLibrary(lib)
                NativeEngine.nativeInit("lib$lib.so", ctx.applicationContext)
            }.getOrElse { "ERROR: $it" }
        }
        if (r.startsWith("ERROR")) { Log.e(TAG, "native plugin $lib: $r"); return null }
        return runCatching { JSONObject(r) }.getOrNull()?.also { infos[lib] = it }
    }

    fun entries(ctx: Context): List<Entry> = libs(ctx).mapNotNull { lib ->
        ensure(ctx, lib)?.let { Entry(lib, it.optString("name", lib), it.optString("vendor"), it.optBoolean("instrument", true), fileFor(ctx, lib)) }
    }

    /** What to show for a plugin file (native or bridged). */
    fun displayName(f: File): String =
        if (isNative(f)) (infos[f.nameWithoutExtension]?.optString("name") ?: f.nameWithoutExtension) + " · native"
        else f.name.removeSuffix(".vst3")
}

/** Runs [block] on the main thread and returns its result (directly if already there). */
fun <T> onMain(block: () -> T): T {
    if (Looper.myLooper() == Looper.getMainLooper()) return block()
    var result: Result<T>? = null
    val latch = CountDownLatch(1)
    Handler(Looper.getMainLooper()).post {
        result = runCatching(block)
        latch.countDown()
    }
    latch.await()
    return result!!.getOrThrow()
}
