package com.vstghost

import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.LinearGradient
import android.graphics.Paint
import android.graphics.RectF
import android.graphics.Shader
import android.view.MotionEvent
import android.view.View
import kotlin.math.log10
import kotlin.math.max
import kotlin.math.pow

/**
 * The mixer: one strip per slot (Inst 1–4, FX 1–4) with a live meter, a fader and a mute button.
 * The meter shows the slot's output peak after its fader, blue at the bottom through teal and
 * amber to red at the top, with a peak-hold mark. Faders run from −∞ to +6 dB (0 dB at the mark).
 * [onGain] is called when a fader or mute changes; [names] gives each strip's label.
 */
class MixerView(context: Context) : View(context) {
    companion object {
        const val MIN_DB = -60f
        const val MAX_FADER_DB = 6f
        private const val METER_FALL_DB_PER_FRAME = 1.2f   // ~36 dB/s at 30 fps
        private const val HOLD_FRAMES = 30
    }

    var slots: IntRange = 1..8
    var names: (Int) -> String = { "Slot $it" }
    var onGain: (slot: Int, gain: Float) -> Unit = { _, _ -> }

    /** Fader position in dB per slot (−∞ encoded as MIN_DB − 1); mute remembers the fader. */
    private val faderDb = HashMap<Int, Float>()
    private val muted = HashSet<Int>()
    private val levelDb = HashMap<Int, Float>()
    private val holdDb = HashMap<Int, Float>()
    private val holdAge = HashMap<Int, Int>()

    private val dp = resources.displayMetrics.density
    private val text = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = Color.parseColor("#E8EAED"); textSize = 11f * dp; textAlign = Paint.Align.CENTER }
    private val dim = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = Color.parseColor("#9AA3AD"); textSize = 10f * dp; textAlign = Paint.Align.CENTER }
    private val track = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = Color.parseColor("#262B31") }
    private val outline = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = Color.parseColor("#3A414A"); style = Paint.Style.STROKE; strokeWidth = 1f * dp }
    private val knob = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = Color.parseColor("#E8EAED") }
    private val knobMuted = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = Color.parseColor("#5A431A") }
    private val tick = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = Color.parseColor("#4FD1C5"); strokeWidth = 1.5f * dp }
    private val hold = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = Color.WHITE; strokeWidth = 2f * dp }
    private val meterFill = Paint(Paint.ANTI_ALIAS_FLAG)
    private val muteOn = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = Color.parseColor("#FF6B6B") }
    private val muteOff = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = Color.parseColor("#262B31") }
    private val muteText = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = Color.parseColor("#E8EAED"); textSize = 11f * dp; textAlign = Paint.Align.CENTER; isFakeBoldText = true }

    private var dragSlot = -1
    private var dragStartY = 0f
    private var dragStartDb = 0f

    fun setGainDb(slot: Int, db: Float, mute: Boolean) {
        faderDb[slot] = db
        if (mute) muted.add(slot) else muted.remove(slot)
        invalidate()
    }
    fun gainDb(slot: Int) = faderDb[slot] ?: 0f
    fun isMuted(slot: Int) = slot in muted

    /** Linear gain the engine should apply for the strip's current fader and mute. */
    fun linearGain(slot: Int): Float {
        if (slot in muted) return 0f
        val db = gainDb(slot)
        return if (db <= MIN_DB) 0f else 10f.pow(db / 20f)
    }

    /** Feed one frame of peaks (linear, 0..n); call at the display rate. */
    fun update(peaks: Map<Int, Float>) {
        for (s in slots) {
            val pk = peaks[s] ?: 0f
            val db = if (pk > 0f) (20f * log10(pk)).coerceIn(MIN_DB, 12f) else MIN_DB
            val cur = levelDb[s] ?: MIN_DB
            levelDb[s] = if (db >= cur) db else max(MIN_DB, cur - METER_FALL_DB_PER_FRAME)
            val h = holdDb[s] ?: MIN_DB
            val age = (holdAge[s] ?: 0) + 1
            if (db >= h || age > HOLD_FRAMES) { holdDb[s] = db; holdAge[s] = 0 } else holdAge[s] = age
        }
        invalidate()
    }

    override fun onMeasure(w: Int, h: Int) {
        val n = slots.count()
        val want = (n * 64 * dp).toInt()
        setMeasuredDimension(resolveSize(want, w), resolveSize((240 * dp).toInt(), h))
    }

    // Geometry per strip: label on top, meter and fader side by side, mute at the bottom.
    private fun stripW() = width.toFloat() / slots.count()
    private fun top() = 20f * dp
    private fun bottom() = height - 30f * dp
    // Mixer taper: 0 dB sits at 78 % of the travel, the top 22 % is the +6 dB boost, and the
    // range below 0 dB is squared so the useful −20..0 dB part gets most of the room. Meters use
    // the same mapping so a level lines up with the fader that would produce it.
    private fun fraction(db: Float): Float =
        if (db >= 0f) 0.78f + 0.22f * (db / MAX_FADER_DB).coerceIn(0f, 1f)
        else 0.78f * ((db - MIN_DB) / -MIN_DB).coerceIn(0f, 1f).let { it * it }
    private fun dbOf(fraction: Float): Float =
        if (fraction >= 0.78f) (fraction - 0.78f) / 0.22f * MAX_FADER_DB
        else MIN_DB + kotlin.math.sqrt(fraction / 0.78f) * -MIN_DB
    private fun faderY(db: Float): Float = bottom() - fraction(db) * (bottom() - top())
    private fun dbAtY(y: Float): Float = dbOf(((bottom() - y) / (bottom() - top())).coerceIn(0f, 1f))

    override fun onDraw(c: Canvas) {
        val sw = stripW()
        val t = top(); val b = bottom()
        val meterW = 10f * dp
        if (meterFill.shader == null) {
            meterFill.shader = LinearGradient(0f, b, 0f, t,
                intArrayOf(Color.parseColor("#2F6FEB"), Color.parseColor("#4FD1C5"), Color.parseColor("#F2B23A"), Color.parseColor("#FF3B3B")),
                floatArrayOf(0f, 0.55f, 0.82f, 1f), Shader.TileMode.CLAMP)
        }
        for ((i, s) in slots.withIndex()) {
            val x0 = i * sw
            val cx = x0 + sw / 2
            // Label
            val label = names(s)
            val short = if (label.length > 11) label.take(10) + "…" else label
            c.drawText(short, cx, 13f * dp, text)
            // Meter: a column left of the fader
            val mx = x0 + sw * 0.22f
            val meter = RectF(mx - meterW / 2, t, mx + meterW / 2, b)
            c.drawRoundRect(meter, 3f * dp, 3f * dp, track)
            val lv = levelDb[s] ?: MIN_DB
            val yl = faderY(lv.coerceAtMost(MAX_FADER_DB))
            if (lv > MIN_DB) c.drawRoundRect(RectF(meter.left, yl, meter.right, b), 3f * dp, 3f * dp, meterFill)
            val hd = holdDb[s] ?: MIN_DB
            if (hd > MIN_DB) { val yh = faderY(hd.coerceAtMost(MAX_FADER_DB)); c.drawLine(meter.left, yh, meter.right, yh, hold) }
            c.drawRoundRect(meter, 3f * dp, 3f * dp, outline)
            // Fader: track, 0 dB tick, knob
            val fx = x0 + sw * 0.62f
            c.drawRoundRect(RectF(fx - 2f * dp, t, fx + 2f * dp, b), 2f * dp, 2f * dp, track)
            val y0 = faderY(0f)
            c.drawLine(fx - 9f * dp, y0, fx + 9f * dp, y0, tick)
            val db = gainDb(s)
            val ky = faderY(db)
            c.drawRoundRect(RectF(fx - 11f * dp, ky - 7f * dp, fx + 11f * dp, ky + 7f * dp), 3f * dp, 3f * dp, if (s in muted) knobMuted else knob)
            c.drawText(if (db <= MIN_DB) "−∞" else "%+.1f".format(db), fx, b + 11f * dp, dim)
            // Mute
            val m = RectF(cx - 14f * dp, height - 17f * dp, cx + 14f * dp, height - 2f * dp)
            c.drawRoundRect(m, 4f * dp, 4f * dp, if (s in muted) muteOn else muteOff)
            c.drawText("M", cx, height - 6f * dp, muteText)
        }
    }

    override fun onTouchEvent(e: MotionEvent): Boolean {
        val sw = stripW()
        when (e.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                val i = (e.x / sw).toInt().coerceIn(0, slots.count() - 1)
                val s = slots.first + i
                if (e.y > height - 20f * dp) {   // mute button
                    if (s in muted) muted.remove(s) else muted.add(s)
                    onGain(s, linearGain(s)); invalidate(); return true
                }
                dragSlot = s; dragStartY = e.y; dragStartDb = gainDb(s)
                if (e.y < top() || e.y > bottom()) return true
                // A tap away from the knob jumps there; a drag from anywhere is relative.
                val ky = faderY(dragStartDb)
                if (kotlin.math.abs(e.y - ky) > 12f * dp) { faderDb[s] = dbAtY(e.y); dragStartDb = faderDb[s]!!; dragStartY = e.y; onGain(s, linearGain(s)); invalidate() }
                parent?.requestDisallowInterceptTouchEvent(true)
                return true
            }
            MotionEvent.ACTION_MOVE -> {
                if (dragSlot < 0) return true
                val range = bottom() - top()
                val db = dbOf((fraction(dragStartDb) + (dragStartY - e.y) / range).coerceIn(0f, 1f))
                faderDb[dragSlot] = db
                onGain(dragSlot, linearGain(dragSlot)); invalidate(); return true
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> { dragSlot = -1; return true }
        }
        return super.onTouchEvent(e)
    }
}
