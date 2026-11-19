package com.vstghost

import android.os.Handler
import android.os.Looper

/**
 * Nested message loop for JUCE modal calls (PopupMenu::show(), AlertWindow::showOkCancelBox(), …)
 * in native plugins. JUCE forbids modal loops on Android because the platform has no "run the
 * event loop for a while" call; this provides one: [runFor] runs the main Looper nested for a
 * slice and returns, so JUCE's generic runDispatchLoopUntil() can poll its modal component.
 * Leaving a nested Looper.loop() is done by throwing from a posted Runnable (the Android
 * convention; Looper.loop() propagates it). Called from the native side on the main thread only.
 */
object JuceModalLoop {
    private class Exit : RuntimeException()

    private val main = Handler(Looper.getMainLooper())

    @JvmStatic
    fun runFor(ms: Int): Boolean {
        if (Looper.myLooper() != Looper.getMainLooper()) return false
        val exit = Runnable { throw Exit() }
        main.postDelayed(exit, ms.coerceAtLeast(1).toLong())
        try {
            Looper.loop()
        } catch (e: Exit) {
            // the slice ended
        } finally {
            main.removeCallbacks(exit)
        }
        return true
    }
}
