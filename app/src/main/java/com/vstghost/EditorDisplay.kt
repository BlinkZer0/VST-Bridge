// SPDX-License-Identifier: GPL-3.0-or-later
package com.vstghost

import android.util.Log
import java.io.File

/**
 * The app process's Wayland compositor (shared/wl), where Wine's Wayland driver draws plugin editors.
 * It must be listening before the Wine session starts: Wine's desktop process picks the display
 * driver once ("wayland,null" in the prefix) and every Wine process then uses that choice.
 */
object EditorDisplay {
    /** Size of the one virtual monitor Wine sees; plugins keep their menus inside it. */
    const val SCREEN_W = 1920
    const val SCREEN_H = 1200

    @Volatile var error: String? = null; private set
    private var started = false

    fun ensure(payload: Payload) {
        synchronized(this) {
            if (started && File(payload.root, "run/${Payload.WAYLAND_SOCKET}").exists()) return
            val run = File(payload.root, "run")
            run.mkdirs()
            run.setReadable(true, true); run.setWritable(true, true); run.setExecutable(true, true)
            error = NativeEngine.wlStart(File(run, Payload.WAYLAND_SOCKET).path, SCREEN_W, SCREEN_H)
            if (error == null) started = true else Log.e("VSTGHOST", "editor display: $error")
        }
    }
}
