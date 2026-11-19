// SPDX-License-Identifier: LicenseRef-VstGhost-Proprietary
package com.vstghost

import android.os.Bundle
import android.view.ViewGroup.LayoutParams.MATCH_PARENT
import android.view.ViewGroup.LayoutParams.WRAP_CONTENT
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.google.android.material.button.MaterialButton
import com.google.android.material.dialog.MaterialAlertDialogBuilder

/**
 * ⋮ → About: version, the terms, the privacy policy and the third-party notices with their license
 * texts (all from assets, so the app shows exactly what it ships with).
 */
class AboutActivity : AppCompatActivity() {
    private lateinit var body: TextView

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        title = "About VST gHost"
        val dp = resources.displayMetrics.density
        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            fitsSystemWindows = true
            setPadding((16 * dp).toInt(), (10 * dp).toInt(), (16 * dp).toInt(), (10 * dp).toInt())
        }
        val version = runCatching { packageManager.getPackageInfo(packageName, 0).versionName }.getOrNull() ?: "?"
        val payload = Payload(this).installedVersion ?: "not installed"
        root.addView(TextView(this).apply {
            text = "VST gHost $version  ·  runtime $payload\n© 2026 Blink Zero. All rights reserved."
            textSize = 13f
        })
        val bar = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        val docs = listOf("Terms" to "legal/terms-of-service.md", "Privacy" to "legal/privacy-policy.md",
                          "Notices" to "licenses/NOTICES.md", "Licenses" to "")
        for ((label, path) in docs) {
            bar.addView(MaterialButton(this, null, com.google.android.material.R.attr.materialButtonOutlinedStyle).apply {
                text = label; isAllCaps = false
                setOnClickListener { if (path.isEmpty()) pickLicense() else show(path) }
            }, LinearLayout.LayoutParams(0, WRAP_CONTENT, 1f).apply { leftMargin = (3 * dp).toInt(); rightMargin = (3 * dp).toInt() })
        }
        bar.addView(MaterialButton(this).apply { text = "Close"; isAllCaps = false; setOnClickListener { finish() } },
            LinearLayout.LayoutParams(WRAP_CONTENT, WRAP_CONTENT).apply { leftMargin = (6 * dp).toInt() })
        root.addView(bar)
        body = TextView(this).apply { textSize = 12f; setTextIsSelectable(true); setPadding(0, (8 * dp).toInt(), 0, 0) }
        root.addView(ScrollView(this).apply { addView(body) }, LinearLayout.LayoutParams(MATCH_PARENT, 0, 1f))
        setContentView(root)
        show("licenses/NOTICES.md")
    }

    private fun show(path: String) {
        body.text = runCatching { assets.open(path).bufferedReader().readText() }.getOrElse { "($path missing)" }
            .let(::plainText)
    }

    private fun pickLicense() {
        val files = assets.list("licenses")?.filter { it != "NOTICES.md" }?.sorted().orEmpty()
        MaterialAlertDialogBuilder(this).setTitle("License texts").setItems(files.toTypedArray()) { _, i -> show("licenses/${files[i]}") }
            .setNegativeButton("Cancel", null).show()
    }

    /** Markdown → readable plain text: headings kept as lines, tables as "a — b — c", links as text. */
    private fun plainText(md: String): String = md.lines().joinToString("\n") { line ->
        val l = line.trimEnd()
        when {
            l.startsWith("|---") -> ""
            l.startsWith("|") -> l.trim('|').split("|").joinToString("  —  ") { it.trim() }
            l.startsWith("#") -> l.trimStart('#').trim().uppercase()
            else -> l.replace(Regex("\\[([^]]+)]\\([^)]+\\)"), "$1").replace("**", "").replace("`", "")
        }
    }
}
