// SPDX-License-Identifier: GPL-3.0-or-later
package com.vstghost

import android.app.Activity
import android.content.Context
import android.graphics.Color
import android.os.Bundle
import android.util.Log
import android.view.Gravity
import android.view.MotionEvent
import android.view.ViewGroup.LayoutParams.MATCH_PARENT
import android.view.ViewGroup.LayoutParams.WRAP_CONTENT
import android.view.WindowManager
import android.widget.Button
import android.widget.FrameLayout
import android.widget.LinearLayout
import android.widget.TextView
import kotlin.math.hypot
import kotlin.math.min

/**
 * Phase 5: a native plugin's own editor (its JUCE AudioProcessorEditor, e.g. a WebView UI) placed
 * directly in this activity. One finger goes to the plugin; two fingers pinch-zoom and pan.
 *
 *   adb shell am start -n com.vstghost/com.vstghost.NativeEditorActivity --ei slot 0
 */
class NativeEditorActivity : Activity() {
    private var slot = 0
    private var opened = false
    private lateinit var zoom: ZoomLayout
    private lateinit var title: TextView

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        slot = intent.getIntExtra(EditorActivity.EXTRA_SLOT, 0).coerceIn(0, NativeEngine.SLOTS - 1)
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
        val bridge = Bridge.get(this, slot)
        title = TextView(this).apply {
            setTextColor(Color.WHITE); textSize = 15f
            text = bridge.plugin?.let { NativePlugins.displayName(it) } ?: "Plugin"
        }
        bar.addView(title, LinearLayout.LayoutParams(0, WRAP_CONTENT, 1f))
        bar.addView(Button(this).apply {
            text = "Fit"; isAllCaps = false; textSize = 13f; minHeight = 0; minimumHeight = 0
            setOnClickListener { zoom.reset() }
        }, LinearLayout.LayoutParams(WRAP_CONTENT, dp(36)))
        bar.addView(Button(this).apply {
            text = "Close"; isAllCaps = false; textSize = 13f; minHeight = 0; minimumHeight = 0
            setOnClickListener { finish() }
        }, LinearLayout.LayoutParams(WRAP_CONTENT, dp(36)))
        root.addView(bar)
        zoom = ZoomLayout(this)
        root.addView(zoom, LinearLayout.LayoutParams(MATCH_PARENT, 0, 1f))
        setContentView(root)

        if (!NativeEngine.isNative(slot)) { title.text = "No native plugin in this slot"; return }
        zoom.post {
            val t0 = System.nanoTime()
            val size = NativeEngine.nativeEditorOpen(slot, zoom.content, zoom.width, zoom.height)
            val ms = (System.nanoTime() - t0) / 1_000_000
            Log.i("VSTGHOST", "native editor slot $slot: ${size?.joinToString("x")} in $ms ms (area ${zoom.width}x${zoom.height})")
            if (size == null) { title.text = "This plugin has no editor"; return@post }
            opened = true
            zoom.setContentSize(size[0], size[1])
        }
    }

    override fun onDestroy() {
        super.onDestroy()
        if (opened && isFinishing) NativeEngine.nativeEditorClose(slot)
    }

    private fun dp(v: Int) = (v * resources.displayMetrics.density).toInt()
}

/**
 * Hosts real child views (the plugin's editor) in [content]; two fingers zoom and pan it with a view
 * transform, so touches still reach the plugin at the right place (Android maps them through the
 * transform). One-finger touches are not intercepted.
 */
class ZoomLayout(context: Context) : FrameLayout(context) {
    val content = FrameLayout(context)
    private var cw = 0
    private var ch = 0
    private var fit = 1f
    private var scale = 1f
    private var ox = 0f
    private var oy = 0f
    private var lastFx = 0f
    private var lastFy = 0f
    private var lastSpan = 0f
    private var lastCount = 0
    var maxZoom = 6f

    init {
        clipChildren = true
        addView(content, LayoutParams(WRAP_CONTENT, WRAP_CONTENT))
        content.pivotX = 0f
        content.pivotY = 0f
    }

    /** The editor's size in px at zoom 1 (it was already fitted to this layout). */
    fun setContentSize(w: Int, h: Int) {
        cw = w; ch = h
        content.layoutParams = LayoutParams(w, h)
        reset()
    }

    fun reset() {
        if (cw <= 0 || ch <= 0 || width <= 0) return
        fit = min(1f, min(width.toFloat() / cw, height.toFloat() / ch))
        scale = fit
        apply()
    }

    private fun apply() {
        val w = cw * scale
        val h = ch * scale
        ox = if (w <= width) (width - w) / 2f else ox.coerceIn(width - w, 0f)
        oy = if (h <= height) (height - h) / 2f else oy.coerceIn(height - h, 0f)
        content.scaleX = scale; content.scaleY = scale
        content.translationX = ox; content.translationY = oy
    }

    override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) {
        super.onSizeChanged(w, h, oldw, oldh)
        reset()
    }

    override fun onInterceptTouchEvent(e: MotionEvent): Boolean = e.pointerCount >= 2

    override fun onTouchEvent(e: MotionEvent): Boolean {
        if (e.pointerCount < 2) { lastCount = 0; return true }
        val skip = if (e.actionMasked == MotionEvent.ACTION_POINTER_UP) e.actionIndex else -1
        var sx = 0f; var sy = 0f; var k = 0
        for (i in 0 until e.pointerCount) if (i != skip) { sx += e.getX(i); sy += e.getY(i); k++ }
        if (k < 2) { lastCount = 0; return true }
        val fx = sx / k
        val fy = sy / k
        var span = 0f
        for (i in 0 until e.pointerCount) if (i != skip) span += hypot(e.getX(i) - fx, e.getY(i) - fy)
        span /= k
        if (lastCount != k || e.actionMasked != MotionEvent.ACTION_MOVE) {
            lastCount = k; lastFx = fx; lastFy = fy; lastSpan = span
            return true
        }
        ox += fx - lastFx
        oy += fy - lastFy
        if (lastSpan > 10f && span > 10f) {
            val s = (scale * span / lastSpan).coerceIn(fit, fit * maxZoom)
            ox = fx - (fx - ox) * (s / scale)
            oy = fy - (fy - oy) * (s / scale)
            scale = s
        }
        lastFx = fx; lastFy = fy; lastSpan = span
        apply()
        return true
    }
}
