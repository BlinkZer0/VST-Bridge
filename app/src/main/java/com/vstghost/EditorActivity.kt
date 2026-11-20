// SPDX-License-Identifier: LicenseRef-VstGhost-Proprietary
package com.vstghost

import android.app.Activity
import android.content.Context
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.os.Bundle
import android.util.Log
import android.view.Choreographer
import android.view.Gravity
import android.view.InputDevice
import android.view.MotionEvent
import android.view.View
import android.view.ViewGroup.LayoutParams.MATCH_PARENT
import android.view.ViewGroup.LayoutParams.WRAP_CONTENT
import android.view.WindowManager
import android.widget.Button
import android.widget.LinearLayout
import android.widget.TextView
import org.json.JSONObject
import kotlin.concurrent.thread

/**
 * Shows a bridged plugin's own Windows editor (Phase 4). The slot's host opens the editor window
 * (`editor_open`); Wine's Wayland driver draws it into our compositor; [EditorView] shows the
 * composed picture and turns touches into mouse input.
 *
 *   adb shell am start -n com.vstghost.sideload/com.vstghost.EditorActivity --ei slot 0
 * Test hooks (extras): --ei dump N   save the picture to files/editor-dump-<k>.bmp every N seconds (5x)
 *                      --es taps "x,y/x,y"  tap these picture coordinates, one per dump interval
 */
class EditorActivity : Activity() {
    companion object {
        const val EXTRA_SLOT = "slot"
        private const val TAG = "VSTGHOST"
        /** How long the screen may stay in the background before its editor is closed. */
        private const val POWER_CLOSE_MS = 30_000L
    }

    private var slot = 0
    private lateinit var view: EditorView
    private lateinit var title: TextView
    private lateinit var info: TextView
    @Volatile private var opened = false
    @Volatile private var paused = false
    /** The editor was closed to save power while this screen was in the background. */
    @Volatile private var closedForPower = false
    private val ui = android.os.Handler(android.os.Looper.getMainLooper())
    private val powerClose = Runnable {
        if (!paused || !opened || isFinishing) return@Runnable
        opened = false
        closedForPower = true
        thread(name = "editor-close") { Bridge.get(this, slot).request("editor_close", 10_000) }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        slot = intent.getIntExtra(EXTRA_SLOT, 0).coerceIn(0, NativeEngine.SLOTS - 1)
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setBackgroundColor(Color.BLACK)
            fitsSystemWindows = true
        }
        val bar = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
            setPadding(dp(8), dp(2), dp(8), dp(2))
        }
        title = TextView(this).apply { setTextColor(Color.WHITE); textSize = 15f; text = "Opening editor…" }
        info = TextView(this).apply { setTextColor(Color.GRAY); textSize = 12f; setPadding(dp(12), 0, dp(12), 0) }
        bar.addView(title, LinearLayout.LayoutParams(WRAP_CONTENT, WRAP_CONTENT))
        bar.addView(info, LinearLayout.LayoutParams(0, WRAP_CONTENT, 1f))
        bar.addView(Button(this).apply {
            text = "Close"; isAllCaps = false; textSize = 13f; minHeight = 0; minimumHeight = 0
            setPadding(dp(14), dp(2), dp(14), dp(2))
            setOnClickListener { finish() }
        }, LinearLayout.LayoutParams(WRAP_CONTENT, dp(36)))
        root.addView(bar)
        view = EditorView(this)
        root.addView(view, LinearLayout.LayoutParams(MATCH_PARENT, 0, 1f))
        setContentView(root)
        view.onPicture = { w, h, fps -> info.text = "${w}×$h · ${"%.0f".format(fps)} fps" }

        openEditor(first = true)
    }

    /** Opens the slot's editor window; [first] also runs the test hooks from the intent extras. */
    private fun openEditor(first: Boolean) {
        val bridge = Bridge.get(this, slot)
        runOnUiThread { title.text = "Opening editor…" }
        thread(name = "editor-open") {
            val t0 = System.nanoTime()
            val resp = if (bridge.isRunning) bridge.request("editor_open", 30_000) else null
            val ms = (System.nanoTime() - t0) / 1_000_000
            Log.i(TAG, "editor_open slot $slot: $resp ($ms ms) display=${NativeEngine.wlStats()}")
            val j = resp?.let { runCatching { JSONObject(it) }.getOrNull() }
            opened = bridge.isRunning && j?.optBoolean("ok") == true
            closedForPower = false
            runOnUiThread {
                when {
                    !bridge.isRunning -> title.text = "No plugin is running in this slot"
                    j == null -> title.text = "The plugin did not answer"
                    !j.optBoolean("ok") -> title.text = "Editor failed: ${j.optString("error")}"
                    else -> {
                        title.text = bridge.plugin?.nameWithoutExtension ?: "Plugin"
                    }
                }
                EditorDisplay.error?.let { title.text = "Display error: $it" }
                // Sent to the background again while the editor was opening: hide it now.
                if (opened && paused) thread(name = "editor-hide") { bridge.request("editor_visible\n0", 5_000) }
            }
            if (!first) return@thread
            val every = intent.getIntExtra("dump", 0)
            val taps = intent.getStringExtra("taps")?.split("/")?.mapNotNull { t ->
                t.split(",").takeIf { it.size == 2 }?.let { Pair(it[0].toFloat(), it[1].toFloat()) } } ?: emptyList()
            if (every > 0 && opened) for (k in 0 until 5) {
                Thread.sleep(every * 1000L)
                val r = NativeEngine.wlDump(java.io.File(filesDir, "editor-dump-$k.bmp").path)
                Log.i(TAG, "editor dump $k: $r ${NativeEngine.wlStats()}")
                taps.getOrNull(k)?.let { (x, y) ->
                    NativeEngine.wlPointer(x, y, -1); Thread.sleep(50)
                    NativeEngine.wlPointer(x, y, 1); Thread.sleep(80)
                    NativeEngine.wlPointer(x, y, 0)
                    Log.i(TAG, "editor tap $k at $x,$y")
                }
            }
        }
    }

    override fun onResume() {
        super.onResume(); view.start()
        paused = false
        ui.removeCallbacks(powerClose)
        when {
            closedForPower -> openEditor(first = false)
            opened -> thread(name = "editor-show") { Bridge.get(this, slot).request("editor_visible\n1", 5_000) }
        }
    }
    override fun onPause() {
        super.onPause(); view.stop()
        paused = true
        if (isFinishing || !opened) return
        // Screen off or another app in front: hide the Wine window right away, and if we stay in the
        // background, close the editor. Hiding alone saves little (a plugin keeps repainting its own
        // buffer on its timers: Dexed's host sits at ~1.6 cores hidden, ~0.03 closed); closing is what
        // stops it. The editor is reopened on resume.
        thread(name = "editor-hide") { Bridge.get(this, slot).request("editor_visible\n0", 5_000) }
        ui.postDelayed(powerClose, POWER_CLOSE_MS)
    }

    override fun onDestroy() {
        super.onDestroy()
        ui.removeCallbacks(powerClose)
        if (isFinishing && opened) {
            val bridge = Bridge.get(this, slot)
            thread(name = "editor-close") { bridge.request("editor_close", 10_000) }
        }
    }

    private fun dp(v: Int) = (v * resources.displayMetrics.density).toInt()
}

/**
 * Draws the compositor's picture; one finger is the plugin's mouse, two fingers pinch-zoom and pan
 * (see [ZoomPan]).
 */
class EditorView(context: Context) : View(context), Choreographer.FrameCallback {
    private var bmp: Bitmap? = null
    private var buf = IntArray(0)
    private val meta = IntArray(2)
    private var serial = 0L
    private val paint = Paint(Paint.FILTER_BITMAP_FLAG)
    private var running = false
    private var frames = 0
    private var fpsT0 = 0L
    private var fps = 0.0
    var onPicture: ((Int, Int, Double) -> Unit)? = null
    private val zoom = ZoomPan(onChange = { invalidate() }, onMouse = { x, y, b -> NativeEngine.wlPointer(x, y, b) })

    fun start() {
        if (running) return
        running = true
        Choreographer.getInstance().postFrameCallback(this)
    }

    fun stop() {
        running = false
        Choreographer.getInstance().removeFrameCallback(this)
    }

    override fun doFrame(frameTimeNanos: Long) {
        if (!running) return
        var s = NativeEngine.wlSnapshot(serial, buf, meta)
        if (s == -1L) {
            buf = IntArray(meta[0] * meta[1])
            s = NativeEngine.wlSnapshot(0, buf, meta)
        }
        if (s >= 0 && s != serial) {
            serial = s
            val w = meta[0]
            val h = meta[1]
            if (w > 0 && h > 0) {
                var b = bmp
                if (b == null || b.width != w || b.height != h) {
                    b = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888)
                    bmp = b
                }
                b!!.setPixels(buf, 0, w, 0, 0, w, h)
            } else {
                bmp = null
            }
            frames++
            invalidate()
        }
        if (frameTimeNanos - fpsT0 >= 1_000_000_000L) {
            fps = frames * 1e9 / (frameTimeNanos - fpsT0).coerceAtLeast(1)
            frames = 0
            fpsT0 = frameTimeNanos
            bmp?.let { onPicture?.invoke(it.width, it.height, fps) }
        }
        Choreographer.getInstance().postFrameCallback(this)
    }

    override fun onDraw(canvas: Canvas) {
        canvas.drawColor(Color.BLACK)
        val b = bmp ?: return
        zoom.setSizes(width, height, b.width, b.height)
        canvas.save()
        canvas.translate(zoom.offsetX, zoom.offsetY)
        canvas.scale(zoom.scale, zoom.scale)
        canvas.drawBitmap(b, 0f, 0f, paint)
        canvas.restore()
    }

    override fun onTouchEvent(e: MotionEvent): Boolean = if (bmp == null) true else zoom.onTouch(e)

    // A real mouse (USB/Bluetooth): hover and wheel; Ctrl + wheel zooms.
    override fun onGenericMotionEvent(e: MotionEvent): Boolean {
        if (!e.isFromSource(InputDevice.SOURCE_CLASS_POINTER) || bmp == null) return super.onGenericMotionEvent(e)
        val x = zoom.toContentX(e.x)
        val y = zoom.toContentY(e.y)
        when (e.actionMasked) {
            MotionEvent.ACTION_HOVER_MOVE -> NativeEngine.wlPointer(x, y, -1)
            MotionEvent.ACTION_SCROLL -> {
                val v = e.getAxisValue(MotionEvent.AXIS_VSCROLL)
                if ((e.metaState and android.view.KeyEvent.META_CTRL_ON) != 0) zoom.zoomBy(e.x, e.y, if (v > 0) 1.15f else 1 / 1.15f)
                else { NativeEngine.wlPointer(x, y, -1); NativeEngine.wlScroll(-v) }
            }
        }
        return true
    }
}
