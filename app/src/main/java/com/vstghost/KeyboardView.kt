// SPDX-License-Identifier: GPL-3.0-or-later
package com.vstghost

import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.RectF
import android.view.MotionEvent
import android.view.View

/**
 * Multi-touch piano keyboard. Velocity comes from the touch position along the key
 * (near the top = soft, near the front edge = loud). Sliding across keys plays a glissando.
 */
class KeyboardView(context: Context) : View(context) {
    var firstOctave = 3          // C3 = MIDI 48
        set(v) { field = v.coerceIn(0, 8); invalidate() }
    var octaves = 2
        set(v) { field = v.coerceIn(1, 4); invalidate() }
    var onNote: ((note: Int, velocity: Int, on: Boolean) -> Unit)? = null

    private val down = HashMap<Int, Int>()          // pointerId -> note
    private val lit = HashSet<Int>()
    private val white = Paint().apply { color = Color.WHITE }
    private val black = Paint().apply { color = Color.rgb(25, 25, 30) }
    private val litPaint = Paint().apply { color = Color.rgb(90, 160, 255) }
    private val border = Paint().apply { color = Color.rgb(60, 60, 60); style = Paint.Style.STROKE; strokeWidth = 2f }
    private val label = Paint().apply { color = Color.rgb(90, 90, 90); textSize = 28f; isAntiAlias = true }
    private val whiteSemis = intArrayOf(0, 2, 4, 5, 7, 9, 11)
    private val blackAfterWhite = booleanArrayOf(true, true, false, true, true, true, false)

    private val base get() = 12 * (firstOctave + 1)
    private val whiteCount get() = 7 * octaves + 1

    private fun whiteRect(i: Int): RectF {
        val w = width.toFloat() / whiteCount
        return RectF(i * w, 0f, (i + 1) * w, height.toFloat())
    }

    private fun blackRect(i: Int): RectF {
        val w = width.toFloat() / whiteCount
        val cx = (i + 1) * w
        return RectF(cx - w * 0.3f, 0f, cx + w * 0.3f, height * 0.6f)
    }

    private fun whiteNote(i: Int) = base + 12 * (i / 7) + whiteSemis[i % 7]

    override fun onDraw(c: Canvas) {
        for (i in 0 until whiteCount) {
            val r = whiteRect(i)
            val n = whiteNote(i)
            c.drawRect(r, if (n in lit) litPaint else white)
            c.drawRect(r, border)
            if (n % 12 == 0) c.drawText("C${n / 12 - 1}", r.left + 8, r.bottom - 12, label)
        }
        for (i in 0 until whiteCount - 1) {
            if (!blackAfterWhite[i % 7]) continue
            val n = whiteNote(i) + 1
            c.drawRect(blackRect(i), if (n in lit) litPaint else black)
        }
    }

    private fun hit(x: Float, y: Float): Pair<Int, Int>? {
        if (x < 0 || x >= width || y < 0 || y >= height) return null
        for (i in 0 until whiteCount - 1) {
            if (!blackAfterWhite[i % 7]) continue
            val r = blackRect(i)
            if (r.contains(x, y)) return whiteNote(i) + 1 to velocity(y, r.height())
        }
        val i = (x / (width.toFloat() / whiteCount)).toInt().coerceIn(0, whiteCount - 1)
        return whiteNote(i) to velocity(y, height.toFloat())
    }

    private fun velocity(y: Float, keyHeight: Float) = (35 + 92 * (y / keyHeight).coerceIn(0f, 1f)).toInt().coerceIn(1, 127)

    private fun press(pid: Int, x: Float, y: Float) {
        val (note, vel) = hit(x, y) ?: return
        down[pid] = note
        lit += note
        onNote?.invoke(note, vel, true)
        invalidate()
    }

    private fun release(pid: Int) {
        val note = down.remove(pid) ?: return
        if (note !in down.values) { lit -= note; onNote?.invoke(note, 0, false) }
        invalidate()
    }

    override fun onTouchEvent(e: MotionEvent): Boolean {
        when (e.actionMasked) {
            MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                val i = e.actionIndex
                press(e.getPointerId(i), e.getX(i), e.getY(i))
            }
            MotionEvent.ACTION_MOVE -> for (i in 0 until e.pointerCount) {
                val pid = e.getPointerId(i)
                val h = hit(e.getX(i), e.getY(i))
                if (h?.first != down[pid]) { release(pid); if (h != null) press(pid, e.getX(i), e.getY(i)) }
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP -> release(e.getPointerId(e.actionIndex))
            MotionEvent.ACTION_CANCEL -> down.keys.toList().forEach { release(it) }
        }
        return true
    }

    fun allOff() { down.keys.toList().forEach { release(it) } }
}
