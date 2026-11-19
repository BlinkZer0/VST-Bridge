// SPDX-License-Identifier: GPL-3.0-or-later
package com.vstghost

import android.app.Activity
import android.content.Intent
import android.graphics.Typeface
import android.os.Bundle
import android.util.Log
import android.view.ViewGroup.LayoutParams.MATCH_PARENT
import android.view.ViewGroup.LayoutParams.WRAP_CONTENT
import android.view.WindowManager
import android.widget.Button
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import java.util.concurrent.Executors

/**
 * Test harness UI. Can also be driven from adb:
 *   adb shell am start -n <pkg>/com.vstghost.MainActivity --ez autorun true [--ez reinstall true]
 *   adb shell am start -n <pkg>/com.vstghost.MainActivity --ez autorun true --ei phase 1 [--ei block 256]
 */
class MainActivity : Activity() {

    private lateinit var logView: TextView
    private lateinit var scroll: ScrollView
    private lateinit var status: TextView
    private val worker = Executors.newSingleThreadExecutor()
    @Volatile private var busy = false

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        NativeEngine.playerSlot = 0  // the tests drive slot 0 (the player moves it to its instrument slot)
        val pad = (12 * resources.displayMetrics.density).toInt()

        val col = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(pad, pad, pad, pad)
            fitsSystemWindows = true  // targetSdk 35+ is edge-to-edge
        }
        status = TextView(this).apply { textSize = 14f }
        col.addView(TextView(this).apply { text = getString(R.string.app_name) + ": Phase 0 harness"; textSize = 20f })
        col.addView(status)

        val buttons = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        fun button(label: String, action: () -> Unit) = Button(this).apply {
            text = label; isAllCaps = false; setOnClickListener { action() }
            buttons.addView(this, LinearLayout.LayoutParams(0, WRAP_CONTENT, 1f))
        }
        button("Phase 0") { start(reinstall = false) }
        button("Phase 1") { startPhase1() }
        button("Share report") { share() }
        col.addView(buttons)

        logView = TextView(this).apply { typeface = Typeface.MONOSPACE; textSize = 11f; setTextIsSelectable(true) }
        scroll = ScrollView(this).apply { addView(logView) }
        col.addView(scroll, LinearLayout.LayoutParams(MATCH_PARENT, 0, 1f))
        setContentView(col)

        refreshStatus()
        handleIntent(intent)
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        handleIntent(intent)
    }

    private fun handleIntent(i: Intent?) {
        if (i?.getBooleanExtra("autorun", false) == true) {
            i.removeExtra("autorun")
            when (i.getIntExtra("phase", 0)) {
                1 -> startPhase1(i.getIntExtra("block", 256), i.getBooleanExtra("reinstall", false))
                2 -> startPhase2(i.getIntExtra("seconds", 15), i.getBooleanExtra("reinstall", false))
                3 -> runTask("PHASE3") { Phase3(this, ::log).runAll() }
                else -> start(reinstall = i.getBooleanExtra("reinstall", false))
            }
        }
    }

    private fun runTask(tag: String, body: () -> Unit) {
        if (busy) { log("already running"); return }
        busy = true
        logView.text = ""
        worker.execute {
            try { body(); log("$tag DONE") }
            catch (t: Throwable) { log("$tag CRASHED: ${Log.getStackTraceString(t)}") }
            finally { busy = false; runOnUiThread { refreshStatus() } }
        }
    }

    private fun startPhase2(seconds: Int, reinstall: Boolean) {
        if (busy) { log("already running"); return }
        busy = true
        logView.text = ""
        worker.execute {
            try {
                Phase2(this, ::log).runAll(playSec = seconds, reinstall = reinstall)
                log("PHASE2 DONE")
            } catch (t: Throwable) {
                log("PHASE2 CRASHED: ${Log.getStackTraceString(t)}")
            } finally {
                busy = false
                runOnUiThread { refreshStatus() }
            }
        }
    }

    private fun startPhase1(block: Int = 256, reinstall: Boolean = false) {
        if (busy) { log("already running"); return }
        busy = true
        logView.text = ""
        worker.execute {
            try {
                Phase1(this, ::log).runAll(block, reinstall)
                log("PHASE1 DONE")
            } catch (t: Throwable) {
                log("PHASE1 CRASHED: ${Log.getStackTraceString(t)}")
            } finally {
                busy = false
                runOnUiThread { refreshStatus() }
            }
        }
    }

    private fun refreshStatus() {
        val p = Payload(this)
        status.text = p.describe() + "payload search order: " + p.archiveCandidates().joinToString(", ")
    }

    private fun log(s: String) {
        Log.i(TAG, s)
        runOnUiThread {
            logView.append(s + "\n")
            scroll.post { scroll.fullScroll(ScrollView.FOCUS_DOWN) }
        }
    }

    private fun start(reinstall: Boolean) {
        if (busy) { log("already running"); return }
        busy = true
        logView.text = ""
        worker.execute {
            try {
                Phase0(this, ::log).runAll(reinstall)
                log("PHASE0 DONE")
            } catch (t: Throwable) {
                log("PHASE0 CRASHED: ${Log.getStackTraceString(t)}")
            } finally {
                busy = false
                runOnUiThread { refreshStatus() }
            }
        }
    }

    private fun share() {
        val f = (listOf(Phase1(this) {}.reportFile()) + Phase0(this) {}.reportFiles())
            .filter { it.isFile }.maxByOrNull { it.lastModified() } ?: run { log("no report yet"); return }
        startActivity(Intent.createChooser(Intent(Intent.ACTION_SEND).apply {
            type = "text/plain"
            putExtra(Intent.EXTRA_SUBJECT, "VST gHost Phase 0 report")
            putExtra(Intent.EXTRA_TEXT, f.readText())
        }, "Share report"))
    }
}
