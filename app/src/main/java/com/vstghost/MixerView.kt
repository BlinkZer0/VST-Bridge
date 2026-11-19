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

/** Meter colours and ballistics shared by the mixer strips and the small bars on the slot cards. */
object Meters {
    const val MIN_DB = -60f
    const val FALL_DB_PER_FRAME = 1.2f   // ~36 dB/s at 30 fps
    const val HOLD_FRAMES = 30
    val COLORS = intArrayOf(Color.parseColor("#2F6FEB"), Color.parseColor("#4FD1C5"), Color.parseColor("#F2B23A"), Color.parseColor("#FF3B3B"))
    val STOPS = floatArrayOf(0f, 0.55f, 0.82f, 1f)

    fun toDb(peak: Float) = if (peak > 0f) (20f * log10(peak)).coerceIn(MIN_DB, 12f) else MIN_DB

    /** One meter's state: level with fall-off, peak hold. */
    class Ballistics {
        var levelDb = MIN_DB; private set
        var holdDb = MIN_DB; private set
        private var holdAge = 0
        fun feed(peak: Float) {
            val db = toDb(peak)
            levelDb = if (db >= levelDb) db else max(MIN_DB, levelDb - FALL_DB_PER_FRAME)
            holdAge++
            if (db >= holdDb || holdAge > HOLD_FRAMES) { holdDb = db; holdAge = 0 }
        }
    }
}

/**
 * The mixer: one strip per slot (Inst 1–4, FX 1–4) and a master strip, each with a live meter, a
 * fader and a mute. The meter shows the output peak after the fader, blue at the bottom through
 * teal and amber to red at the top, with a peak-hold mark. Faders run from −∞ to +6 dB with a
 * mixer taper (0 dB at ¾ of the travel). [onGain] is called when a fader or mute changes;
 * [names] gives each strip's label. The master strip has id [MASTER].
 */
class MixerView(context: Context) : View(context) {
    companion object {
        const val MIN_DB = Meters.MIN_DB
        const val MAX_FADER_DB = 6f
        const val MASTER = -1
    }

    var slots: List<Int> = (1..8).toList()
    var names: (Int) -> String = { if (it == MASTER) "Master" else "Slot $it" }
    var onGain: (slot: Int, gain: Float) -> Unit = { _, _ -> }

    private val faderDb = HashMap<Int, Float>()
    private val muted = HashSet<Int>()
    private val meters = HashMap<Int, Meters.Ballistics>()

    private val dp = resources.displayMetrics.density
    private val text = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = Color.parseColor("#E8EAED"); textSize = 11f * dp; textAlign = Paint.Align.CENTER }
    private val dim = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = Color.parseColor("#9AA3AD"); textSize = 10f * dp; textAlign = Paint.Align.CENTER }
    private val track = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = Color.parseColor("#262B31") }
    private val outline = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = Color.parseColor("#3A414A"); style = Paint.Style.STROKE; strokeWidth = 1f * dp }
    private val knob = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = Color.parseColor("#E8EAED") }
    private val knobMaster = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = Color.parseColor("#F2B23A") }
    private val knobMuted = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = Color.parseColor("#5A431A") }
    private val tick = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = Color.parseColor("#4FD1C5"); strokeWidth = 1.5f * dp }
    private val hold = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = Color.WHITE; strokeWidth = 2f * dp }
    private val divider = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = Color.parseColor("#3A414A"); strokeWidth = 1f * dp }
    private val meterFill = Paint(Paint.ANTI_ALIAS_FLAG)
    private val muteOn = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = Color.parseColor("#FF6B6B") }
    private val muteOff = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = Color.parseColor("#262B31") }
    private val muteText = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = Color.parseColor("#E8EAED"); textSize = 11f * dp; textAlign = Paint.Align.CENTER; isFakeBoldText = true }

    private var dragSlot = Int.MIN_VALUE
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

    /** Feed one frame of peaks (linear); call at the display rate. */
    fun update(peaks: Map<Int, Float>) {
        for (s in slots) meters.getOrPut(s) { Meters.Ballistics() }.feed(peaks[s] ?: 0f)
        invalidate()
    }

    override fun onMeasure(w: Int, h: Int) {
        val want = (slots.size * 64 * dp).toInt()
        setMeasuredDimension(resolveSize(want, w), resolveSize((240 * dp).toInt(), h))
    }

    // Geometry per strip: label on top, meter and fader side by side, mute at the bottom.
    private fun stripW() = width.toFloat() / slots.size
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
        if (meterFill.shader == null)
            meterFill.shader = LinearGradient(0f, b, 0f, t, Meters.COLORS, Meters.STOPS, Shader.TileMode.CLAMP)
        for ((i, s) in slots.withIndex()) {
            val x0 = i * sw
            val cx = x0 + sw / 2
            if (s == MASTER) c.drawLine(x0, t - 8f * dp, x0, height.toFloat(), divider)
            val label = names(s)
            c.drawText(if (label.length > 11) label.take(10) + "…" else label, cx, 13f * dp, text)
            // Meter
            val mx = x0 + sw * 0.22f
            val meter = RectF(mx - meterW / 2, t, mx + meterW / 2, b)
            c.drawRoundRect(meter, 3f * dp, 3f * dp, track)
            val m = meters[s]
            val lv = m?.levelDb ?: MIN_DB
            if (lv > MIN_DB) c.drawRoundRect(RectF(meter.left, faderY(lv.coerceAtMost(MAX_FADER_DB)), meter.right, b), 3f * dp, 3f * dp, meterFill)
            val hd = m?.holdDb ?: MIN_DB
            if (hd > MIN_DB) { val yh = faderY(hd.coerceAtMost(MAX_FADER_DB)); c.drawLine(meter.left, yh, meter.right, yh, hold) }
            c.drawRoundRect(meter, 3f * dp, 3f * dp, outline)
            // Fader
            val fx = x0 + sw * 0.62f
            c.drawRoundRect(RectF(fx - 2f * dp, t, fx + 2f * dp, b), 2f * dp, 2f * dp, track)
            val y0 = faderY(0f)
            c.drawLine(fx - 9f * dp, y0, fx + 9f * dp, y0, tick)
            val db = gainDb(s)
            val ky = faderY(db)
            c.drawRoundRect(RectF(fx - 11f * dp, ky - 7f * dp, fx + 11f * dp, ky + 7f * dp), 3f * dp, 3f * dp,
                if (s in muted) knobMuted else if (s == MASTER) knobMaster else knob)
            c.drawText(if (db <= MIN_DB) "−∞" else "%+.1f".format(db), fx, b + 11f * dp, dim)
            // Mute
            val mr = RectF(cx - 14f * dp, height - 17f * dp, cx + 14f * dp, height - 2f * dp)
            c.drawRoundRect(mr, 4f * dp, 4f * dp, if (s in muted) muteOn else muteOff)
            c.drawText("M", cx, height - 6f * dp, muteText)
        }
    }

    override fun onTouchEvent(e: MotionEvent): Boolean {
        val sw = stripW()
        when (e.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                val s = slots[(e.x / sw).toInt().coerceIn(0, slots.size - 1)]
                if (e.y > height - 20f * dp) {   // mute button
                    if (s in muted) muted.remove(s) else muted.add(s)
                    onGain(s, linearGain(s)); invalidate(); return true
                }
                dragSlot = s; dragStartY = e.y; dragStartDb = gainDb(s)
                if (e.y < top() || e.y > bottom()) return true
                // A tap away from the knob jumps there; a drag from anywhere is relative.
                if (kotlin.math.abs(e.y - faderY(dragStartDb)) > 12f * dp) {
                    faderDb[s] = dbAtY(e.y); dragStartDb = faderDb[s]!!; dragStartY = e.y
                    onGain(s, linearGain(s)); invalidate()
                }
                parent?.requestDisallowInterceptTouchEvent(true)
                return true
            }
            MotionEvent.ACTION_MOVE -> {
                if (dragSlot == Int.MIN_VALUE) return true
                val range = bottom() - top()
                faderDb[dragSlot] = dbOf((fraction(dragStartDb) + (dragStartY - e.y) / range).coerceIn(0f, 1f))
                onGain(dragSlot, linearGain(dragSlot)); invalidate(); return true
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> { dragSlot = Int.MIN_VALUE; return true }
        }
        return super.onTouchEvent(e)
    }
}

/** A thin horizontal meter (blue → red, peak hold) for a slot card. Feed it with [feed] at the display rate. */
class MeterBar(context: Context) : View(context) {
    private val m = Meters.Ballistics()
    private val dp = resources.displayMetrics.density
    private val track = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = Color.parseColor("#1D2126") }
    private val fill = Paint(Paint.ANTI_ALIAS_FLAG)
    private val hold = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = Color.WHITE; strokeWidth = 1.5f * dp }
    private var lastLevel = Meters.MIN_DB
    private var lastHold = Meters.MIN_DB

    fun feed(peak: Float) {
        m.feed(peak)
        if (m.levelDb != lastLevel || m.holdDb != lastHold) { lastLevel = m.levelDb; lastHold = m.holdDb; invalidate() }
    }

    private fun x(db: Float) = ((db - Meters.MIN_DB) / -Meters.MIN_DB).coerceIn(0f, 1f) * width

    override fun onDraw(c: Canvas) {
        if (fill.shader == null) fill.shader = LinearGradient(0f, 0f, width.toFloat(), 0f, Meters.COLORS, Meters.STOPS, Shader.TileMode.CLAMP)
        val r = height / 2f
        c.drawRoundRect(RectF(0f, 0f, width.toFloat(), height.toFloat()), r, r, track)
        if (m.levelDb > Meters.MIN_DB) c.drawRoundRect(RectF(0f, 0f, x(m.levelDb.coerceAtMost(0f)), height.toFloat()), r, r, fill)
        if (m.holdDb > Meters.MIN_DB) { val xh = x(m.holdDb.coerceAtMost(0f)); c.drawLine(xh, 0f, xh, height.toFloat(), hold) }
    }
}
