// SPDX-License-Identifier: LicenseRef-VstGhost-Proprietary
package com.vstghost

import android.os.Handler
import android.os.Looper
import android.view.MotionEvent
import kotlin.math.min

/**
 * Pinch-zoom / two-finger pan for plugin editors, plus one-finger "mouse" input.
 *
 * The content (contentW x contentH) is drawn at screen = offset + content * scale. scale starts at
 * "fit" and can grow to [maxZoom] x fit; the content stays centred while it is smaller than the
 * view and can't be dragged off-screen while it is larger.
 *
 * One finger is the plugin's mouse. Its press is held back for [pressDelayMs] so that a second
 * finger landing in that window turns the touch into a pinch instead of a click.
 */
class ZoomPan(
    private val onChange: () -> Unit,
    /** Mouse input in content coordinates; button -1 = move, 1 = press, 0 = release. */
    private val onMouse: (x: Float, y: Float, button: Int) -> Unit,
) {
    var maxZoom = 6f
    var pressDelayMs = 60L

    var scale = 1f; private set
    var offsetX = 0f; private set
    var offsetY = 0f; private set
    private var fit = 1f
    private var viewW = 0
    private var viewH = 0
    private var contentW = 0
    private var contentH = 0

    private val handler = Handler(Looper.getMainLooper())
    private var pressed = false
    private var pendingPress: Runnable? = null
    private var gesture = false
    private var lastFx = 0f
    private var lastFy = 0f
    private var lastSpan = 0f
    private var lastCount = 0

    /** Call when the view or the content size changes; resets to "fit" if either changed. */
    fun setSizes(vw: Int, vh: Int, cw: Int, ch: Int) {
        if (vw == viewW && vh == viewH && cw == contentW && ch == contentH) return
        viewW = vw; viewH = vh; contentW = cw; contentH = ch
        reset()
    }

    fun reset() {
        if (contentW <= 0 || contentH <= 0 || viewW <= 0 || viewH <= 0) return
        fit = min(viewW.toFloat() / contentW, viewH.toFloat() / contentH)
        scale = fit
        clamp()
        onChange()
    }

    fun toContentX(sx: Float) = (sx - offsetX) / scale
    fun toContentY(sy: Float) = (sy - offsetY) / scale

    private fun clamp() {
        val w = contentW * scale
        val h = contentH * scale
        offsetX = if (w <= viewW) (viewW - w) / 2f else offsetX.coerceIn(viewW - w, 0f)
        offsetY = if (h <= viewH) (viewH - h) / 2f else offsetY.coerceIn(viewH - h, 0f)
    }

    private fun zoomAround(fx: Float, fy: Float, factor: Float) {
        val s = (scale * factor).coerceIn(fit, fit * maxZoom)
        offsetX = fx - (fx - offsetX) * (s / scale)
        offsetY = fy - (fy - offsetY) * (s / scale)
        scale = s
    }

    private fun focus(e: MotionEvent): Triple<Float, Float, Float> {
        var sx = 0f; var sy = 0f
        val n = e.pointerCount
        val skip = if (e.actionMasked == MotionEvent.ACTION_POINTER_UP) e.actionIndex else -1
        var k = 0
        for (i in 0 until n) if (i != skip) { sx += e.getX(i); sy += e.getY(i); k++ }
        if (k == 0) return Triple(0f, 0f, 0f)
        val fx = sx / k
        val fy = sy / k
        var span = 0f
        for (i in 0 until n) if (i != skip) span += kotlin.math.hypot(e.getX(i) - fx, e.getY(i) - fy)
        return Triple(fx, fy, span / k)
    }

    private fun firePress() {
        pendingPress?.let { handler.removeCallbacks(it) }
        pendingPress?.run()
    }

    fun onTouch(e: MotionEvent): Boolean {
        when (e.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                gesture = false
                val x = toContentX(e.x)
                val y = toContentY(e.y)
                onMouse(x, y, -1)
                val r = Runnable { pendingPress = null; pressed = true; onMouse(x, y, 1) }
                pendingPress = r
                handler.postDelayed(r, pressDelayMs)
            }
            MotionEvent.ACTION_POINTER_DOWN -> {
                pendingPress?.let { handler.removeCallbacks(it) }
                pendingPress = null
                if (pressed) { onMouse(toContentX(e.getX(0)), toContentY(e.getY(0)), 0); pressed = false }
                gesture = true
                lastCount = 0
            }
            MotionEvent.ACTION_MOVE -> {
                if (gesture) {
                    if (e.pointerCount >= 2) {
                        val (fx, fy, span) = focus(e)
                        if (lastCount != e.pointerCount) {
                            lastCount = e.pointerCount; lastFx = fx; lastFy = fy; lastSpan = span
                        } else {
                            offsetX += fx - lastFx
                            offsetY += fy - lastFy
                            if (lastSpan > 10f && span > 10f) zoomAround(fx, fy, span / lastSpan)
                            lastFx = fx; lastFy = fy; lastSpan = span
                            clamp()
                            onChange()
                        }
                    }
                } else {
                    if (pendingPress != null) firePress()
                    onMouse(toContentX(e.x), toContentY(e.y), -1)
                }
            }
            MotionEvent.ACTION_POINTER_UP -> lastCount = 0  // re-anchor on the remaining fingers
            MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> {
                if (!gesture) {
                    if (pendingPress != null) firePress()  // a quick tap: press now, release below
                    if (pressed) onMouse(toContentX(e.x), toContentY(e.y), 0)
                }
                pressed = false
                gesture = false
            }
        }
        return true
    }

    /** Ctrl/mouse-wheel style zoom (e.g. a real mouse), around (fx, fy). */
    fun zoomBy(fx: Float, fy: Float, factor: Float) {
        zoomAround(fx, fy, factor)
        clamp()
        onChange()
    }

}
