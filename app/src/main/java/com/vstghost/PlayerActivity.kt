// SPDX-License-Identifier: GPL-3.0-or-later
package com.vstghost

import android.app.Activity
import android.app.AlertDialog
import android.content.Intent
import android.graphics.Color
import android.graphics.Typeface
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.text.Editable
import android.text.InputType
import android.text.TextWatcher
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.view.ViewGroup.LayoutParams.MATCH_PARENT
import android.view.ViewGroup.LayoutParams.WRAP_CONTENT
import android.view.WindowManager
import android.widget.AdapterView
import android.widget.ArrayAdapter
import android.widget.BaseAdapter
import android.widget.Button
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.ListView
import android.widget.SeekBar
import android.widget.Spinner
import android.widget.TextView
import org.json.JSONObject
import java.io.File
import java.util.concurrent.Executors

/**
 * The player: instruments (Inst 1–4) and effects (FX 1–4) in the fixed slots other apps see too
 * (AAP plugins, virtual MIDI devices). The instruments play onto one bus, the effects form a chain
 * on that bus (engine.cpp). One slot is selected at a time: the keyboard, USB MIDI, the parameter
 * list, presets and the Editor button act on it.
 */
class PlayerActivity : Activity() {
    companion object {
        private const val REQ_IMPORT_FILE = 41
        private const val REQ_IMPORT_TREE = 42
        private val INST_SLOTS = SlotManager.FIRST_INST until SlotManager.FIRST_INST + SlotManager.COUNT
        private val FX_SLOTS = SlotManager.FIRST_FX until SlotManager.FIRST_FX + SlotManager.COUNT
    }

    private val io = Executors.newSingleThreadExecutor()
    private val ui = Handler(Looper.getMainLooper())
    private var midi: MidiInput? = null

    /** The selected slot; [bridge] is its bridge. */
    private var current = SlotManager.FIRST_INST
    private val bridge get() = Bridge.get(this, current)
    private val payload by lazy { Payload(this) }
    /** Slots this player started (stopped when it closes; AAP/MIDI-owned ones are left alone). */
    private val mine = HashSet<Int>()

    private lateinit var header: TextView
    private lateinit var status: TextView
    private lateinit var midiStatus: TextView
    private lateinit var pluginSpinner: Spinner
    private lateinit var keyboard: KeyboardView
    private lateinit var paramList: ListView
    private lateinit var statsView: TextView
    private lateinit var statsPanel: LinearLayout
    private lateinit var sustainBtn: Button
    private lateinit var fxInputBtn: Button
    private lateinit var restartBtn: Button
    private lateinit var loadBtn: Button
    private val slotBtns = HashMap<Int, Button>()

    private var plugins: List<File> = emptyList()
    private var params: List<Param> = emptyList()
    private var filter = ""
    private var sustain = false
    private var block = 256
    private var fillBlocks = 2
    private val lastStates = HashMap<Int, JSONObject>()
    private var fxTestSignal = false

    data class Param(val id: Long, val title: String, val units: String, val steps: Int, var value: Double, var text: String)

    private val loadedName get() = bridge.plugin?.let { NativePlugins.displayName(it) }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        for (s in INST_SLOTS + FX_SLOTS) Bridge.get(this, s).onHostExit = { code -> ui.post { onHostCrashed(s, code) } }
        buildUi()
        refreshPlugins()
        selectSlot(SlotManager.FIRST_INST)
        midi = MidiInput(this) { s -> ui.post { midiStatus.text = s } }.also { it.start() }
        io.execute {
            setStatus("Preparing Wine…")
            if (payload.needsInstall()) {
                setStatus("Installing the Windows runtime…")
                val r = payload.install(null)
                if (!r.ok) { setStatus("Runtime install failed: ${r.message}"); return@execute }
            }
            if (payload.isPlay && !payload.shimReady()) payload.buildShim()
            Phase1(this) {}.syncPluginsPublic()
            ui.post { refreshPlugins() }
            Bridge.ensureSession(payload)
            NativeEngine.startAudio()
            setStatus("Ready. Pick a plugin, choose a slot and press Load.")
            // adb: am start -n <pkg>/com.vstghost.PlayerActivity --es assign "1=E-Piano;5=Pedal Faster"
            intent?.getStringExtra("assign")?.split(";")?.forEach { kv ->
                val (k, v) = kv.split("=", limit = 2).let { it[0].trim() to it.getOrNull(1)?.trim() }
                k.toIntOrNull()?.takeIf { it in 1..8 }?.let { SlotManager.assign(it, v?.ifEmpty { null }) }
            }
            // adb: --es importZip <name.zip in Android/data/<pkg>/files>  (same importer as the Add button)
            intent?.getStringExtra("importZip")?.let { z ->
                val f = File(getExternalFilesDir(null), z)
                val r = runCatching { f.inputStream().use { PluginImport(this).importZip(it) } }
                    .getOrElse { PluginImport.Result(emptyList(), "Import failed: ${it.message}") }
                android.util.Log.i("VSTGHOST", "import $z: ${r.message}")
                ui.post { refreshPlugins() }
            }
            // adb: am start -n <pkg>/com.vstghost.PlayerActivity --es load "E-Piano" [--ei slot 5]
            intent?.getStringExtra("load")?.let { name ->
                ui.post {
                    // name: a bridged VST's name, or a native library ("vgn_epianovst")
                    val i = plugins.indexOfFirst { it.nameWithoutExtension == name }
                    intent?.getIntExtra("slot", 0)?.takeIf { it in 1..8 }?.let { selectSlot(it) }
                    if (i >= 0) { pluginSpinner.setSelection(i); loadSelected() }
                }
            }
        }
        ui.postDelayed(statsTick, 500)
    }

    // ------------------------------------------------------------------ add plugins (import)

    private fun addPluginDialog() {
        AlertDialog.Builder(this).setTitle("Add a Windows VST3 plugin")
            .setItems(arrayOf("A .vst3 file or a .zip download", "A .vst3 folder (bundle)")) { _, which ->
                if (which == 0) startActivityForResult(Intent(Intent.ACTION_OPEN_DOCUMENT).apply {
                    addCategory(Intent.CATEGORY_OPENABLE)
                    type = "*/*"
                }, REQ_IMPORT_FILE)
                else startActivityForResult(Intent(Intent.ACTION_OPEN_DOCUMENT_TREE), REQ_IMPORT_TREE)
            }.setNegativeButton("Cancel", null).show()
    }

    @Deprecated("Activity result API is fine for this plain Activity")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        val uri = data?.data ?: return
        if (resultCode != RESULT_OK || (requestCode != REQ_IMPORT_FILE && requestCode != REQ_IMPORT_TREE)) return
        setStatus("Adding plugin…")
        io.execute {
            val r = runCatching {
                if (requestCode == REQ_IMPORT_TREE) PluginImport(this).importTree(uri) else PluginImport(this).importDocument(uri)
            }.getOrElse { PluginImport.Result(emptyList(), "Import failed: ${it.message}") }
            setStatus(r.message + if (r.imported.isNotEmpty()) " · pick it above and press Load" else "")
            ui.post { refreshPlugins() }
        }
    }

    override fun onResume() {
        super.onResume()
        // The Tests screen points the keyboard/MIDI at slot 0 while it runs; take it back.
        if (current in INST_SLOTS) NativeEngine.playerSlot = current
    }

    override fun onDestroy() {
        ui.removeCallbacks(statsTick)
        midi?.stop()
        val stop = mine.toList()
        io.execute { for (s in stop) Bridge.get(this, s).stop(); NativeEngine.stopAudio() }
        io.shutdown()
        super.onDestroy()
    }

    // ------------------------------------------------------------------ UI

    private fun dp(v: Int) = (v * resources.displayMetrics.density).toInt()

    private fun buildUi() {
        // Landscape phone is only ~380 dp tall: compact top bar, the slot strip (Inst 1–4 / FX 1–4),
        // a split middle (parameters | controls + stats) that gets all remaining height, keyboard at
        // the bottom.
        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(8), dp(4), dp(8), dp(4))
            fitsSystemWindows = true
        }
        fun row() = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL; gravity = Gravity.CENTER_VERTICAL }
        fun btn(label: String, parent: ViewGroup, weight: Float = 0f, action: () -> Unit) = Button(this).apply {
            text = label; isAllCaps = false; textSize = 13f; minHeight = 0; minimumHeight = 0
            setPadding(dp(10), dp(4), dp(10), dp(4))
            setOnClickListener { action() }
            parent.addView(this, if (weight > 0) LinearLayout.LayoutParams(0, dp(40), weight) else LinearLayout.LayoutParams(WRAP_CONTENT, dp(40)))
        }

        val top = row()
        pluginSpinner = Spinner(this)
        top.addView(pluginSpinner, LinearLayout.LayoutParams(0, dp(40), 1f))
        loadBtn = btn("Load → Inst 1", top) { loadSelected() }
        btn("Add", top) { addPluginDialog() }
        btn("Editor", top) {
            if (!bridge.isRunning) android.widget.Toast.makeText(this, "Load a plugin into ${slotName(current)} first", android.widget.Toast.LENGTH_SHORT).show()
            else if (bridge.isNativeLoaded)
                startActivity(Intent(this, NativeEditorActivity::class.java).putExtra(EditorActivity.EXTRA_SLOT, current))
            else startActivity(Intent(this, EditorActivity::class.java).putExtra(EditorActivity.EXTRA_SLOT, current))
        }
        btn("Unload", top) { unloadCurrent() }
        btn("Presets", top) { presets() }
        btn("Stats", top) { statsPanel.visibility = if (statsPanel.visibility == View.VISIBLE) View.GONE else View.VISIBLE }
        btn("Tests", top) { startActivity(Intent(this, MainActivity::class.java)) }
        root.addView(top)

        // The slot strip: two rows of four. Tapping a slot selects it; its plugin name is shown.
        fun slotRow(title: String, slots: IntRange): LinearLayout = row().apply {
            addView(TextView(this@PlayerActivity).apply {
                text = title; textSize = 12f; setTypeface(typeface, Typeface.BOLD); setTextColor(Color.GRAY)
                setPadding(0, 0, dp(6), 0)
            }, LinearLayout.LayoutParams(dp(78), WRAP_CONTENT))
            for (s in slots) {
                val b = Button(this@PlayerActivity).apply {
                    isAllCaps = false; textSize = 11f; minHeight = 0; minimumHeight = 0; maxLines = 1
                    setPadding(dp(4), 0, dp(4), 0)
                    setOnClickListener { selectSlot(s) }
                    setOnLongClickListener { slotMenu(s); true }
                }
                slotBtns[s] = b
                addView(b, LinearLayout.LayoutParams(0, dp(34), 1f))
            }
        }
        root.addView(slotRow("Instruments", INST_SLOTS))
        root.addView(slotRow("Effects →", FX_SLOTS))

        header = TextView(this).apply { textSize = 16f; setTypeface(typeface, Typeface.BOLD); text = "No plugin loaded" }
        root.addView(header)
        status = TextView(this).apply { textSize = 12f; maxLines = 2 }
        root.addView(status)
        restartBtn = Button(this).apply {
            text = "Restart plugin (restores its last state)"; isAllCaps = false; visibility = View.GONE
            setOnClickListener { restartAfterCrash() }
        }
        root.addView(restartBtn)

        val middle = row().apply { gravity = Gravity.TOP }
        val left = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
        val right = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setPadding(dp(8), 0, 0, 0) }
        middle.addView(left, LinearLayout.LayoutParams(0, MATCH_PARENT, 3f))
        middle.addView(right, LinearLayout.LayoutParams(0, MATCH_PARENT, 2f))
        root.addView(middle, LinearLayout.LayoutParams(MATCH_PARENT, 0, 1f))

        val ctl = row()
        sustainBtn = btn("Sustain: off", ctl, 1.4f) {
            sustain = !sustain
            NativeEngine.sendMidi(0xB0, 64, if (sustain) 127 else 0)
            sustainBtn.text = "Sustain: ${if (sustain) "ON" else "off"}"
        }
        btn("Oct −", ctl, 1f) { keyboard.allOff(); keyboard.firstOctave -= 1 }
        btn("Oct +", ctl, 1f) { keyboard.allOff(); keyboard.firstOctave += 1 }
        right.addView(ctl)
        val ctl2 = row()
        fxInputBtn = btn("FX input: instruments", ctl2, 1f) {
            fxTestSignal = !fxTestSignal
            NativeEngine.setFxInput(if (fxTestSignal) 1 else 0)
            fxInputBtn.text = "FX input: ${if (fxTestSignal) "test signal" else "instruments"}"
        }
        right.addView(ctl2)
        midiStatus = TextView(this).apply { textSize = 11f; setTextColor(Color.GRAY) }
        right.addView(midiStatus)

        statsPanel = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; visibility = View.GONE }
        val cfg = row()
        val blockSpin = Spinner(this).apply {
            adapter = ArrayAdapter(this@PlayerActivity, android.R.layout.simple_spinner_dropdown_item, listOf("block 128", "block 256", "block 512"))
            setSelection(1)
        }
        val fillSpin = Spinner(this).apply {
            adapter = ArrayAdapter(this@PlayerActivity, android.R.layout.simple_spinner_dropdown_item, (1..8).map { "buffer ×$it" })
            setSelection(1)
        }
        cfg.addView(blockSpin, LinearLayout.LayoutParams(0, WRAP_CONTENT, 1f))
        cfg.addView(fillSpin, LinearLayout.LayoutParams(0, WRAP_CONTENT, 1f))
        btn("Apply", cfg) {
            block = listOf(128, 256, 512)[blockSpin.selectedItemPosition]
            fillBlocks = fillSpin.selectedItemPosition + 1
            if (loadedName != null) restart(keepState = true)
        }
        statsPanel.addView(cfg)
        val tools = row()
        btn("Latency test", tools, 1f) { latencyTest() }
        btn("Reset stats", tools, 1f) { NativeEngine.resetStats() }
        statsPanel.addView(tools)
        statsView = TextView(this).apply { typeface = Typeface.MONOSPACE; textSize = 10f }
        statsPanel.addView(android.widget.ScrollView(this).apply { addView(statsView) })
        right.addView(statsPanel, LinearLayout.LayoutParams(MATCH_PARENT, MATCH_PARENT))

        val search = EditText(this).apply {
            hint = "Search parameters"; inputType = InputType.TYPE_CLASS_TEXT; textSize = 13f; minHeight = 0
            setPadding(dp(6), dp(4), dp(6), dp(4))
            addTextChangedListener(object : TextWatcher {
                override fun afterTextChanged(s: Editable?) { filter = s.toString().trim().lowercase(); adapter.notifyDataSetChanged() }
                override fun beforeTextChanged(s: CharSequence?, a: Int, b: Int, c: Int) {}
                override fun onTextChanged(s: CharSequence?, a: Int, b: Int, c: Int) {}
            })
        }
        left.addView(search, LinearLayout.LayoutParams(MATCH_PARENT, dp(36)))
        paramList = ListView(this).apply { adapter = this@PlayerActivity.adapter }
        left.addView(paramList, LinearLayout.LayoutParams(MATCH_PARENT, 0, 1f))

        keyboard = KeyboardView(this).apply {
            onNote = { note, vel, on -> NativeEngine.sendMidi(if (on) 0x90 else 0x80, note, vel) }
        }
        root.addView(keyboard, LinearLayout.LayoutParams(MATCH_PARENT, dp(120)))
        setContentView(root)
    }

    private fun setStatus(s: String) = ui.post { status.text = s }

    private fun slotName(s: Int) = if (s in FX_SLOTS) "FX ${s - SlotManager.FIRST_FX + 1}" else "Inst $s"

    private fun refreshPlugins() {
        // Native (Phase 5) plugins first, then bridged Windows VSTs.
        // A plugin we build natively hides its Windows build of the same name: the native one is
        // faster and has a working editor (the Windows builds of the user's plugins use WebView2,
        // which stays blank under Wine), and two "E-Piano" entries only invite picking the wrong one.
        val native = NativePlugins.entries(this)
        val nativeNames = native.map { NativePlugins.displayName(it.file).removeSuffix(" · native").lowercase() }.toSet()
        plugins = native.map { it.file } +
            File(filesDir, "plugins").listFiles()
                ?.filter { it.name.endsWith(".vst3") && it.nameWithoutExtension.lowercase() !in nativeNames }
                ?.sortedBy { it.name.lowercase() }.orEmpty()
        val names = plugins.map { NativePlugins.displayName(it) }.ifEmpty { listOf("(no plugins: press Add)") }
        pluginSpinner.adapter = ArrayAdapter(this, android.R.layout.simple_spinner_item, names).apply {
            setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item)
        }
        pluginSpinner.onItemSelectedListener = object : AdapterView.OnItemSelectedListener {
            override fun onItemSelected(p: AdapterView<*>?, v: View?, pos: Int, id: Long) {}
            override fun onNothingSelected(p: AdapterView<*>?) {}
        }
    }

    // ------------------------------------------------------------------ slots

    /** What a slot shows: the running plugin, else the stored assignment (for other apps), else empty. */
    private fun slotText(s: Int): String {
        val b = Bridge.get(this, s)
        val name = b.plugin?.let { NativePlugins.displayName(it).removeSuffix(" · native") }
            ?: SlotManager.assigned(s)?.let { a -> plugins.firstOrNull { it.nameWithoutExtension == a }?.let { NativePlugins.displayName(it).removeSuffix(" · native") } ?: a }
        return "${slotName(s)}: ${name ?: "—"}" + if (b.plugin != null && !b.isRunning) " ✗" else ""
    }

    private fun refreshSlots() {
        for ((s, b) in slotBtns) {
            b.text = slotText(s)
            b.setTypeface(null, if (s == current) Typeface.BOLD else Typeface.NORMAL)
            b.alpha = if (s == current) 1f else 0.7f
        }
        loadBtn.text = "Load → ${slotName(current)}"
    }

    private fun selectSlot(s: Int) {
        if (current != s) keyboard.allOff()
        current = s
        // The keyboard and USB MIDI play the selected instrument (or the last instrument selected).
        if (s in INST_SLOTS) NativeEngine.playerSlot = s
        refreshSlots()
        val b = bridge
        header.text = if (b.plugin != null) "${slotName(s)}  ·  ${loadedName}" else "${slotName(s)}: empty"
        params = emptyList(); adapter.notifyDataSetChanged()
        if (b.isRunning) io.execute { loadParams() }
    }

    private fun slotMenu(s: Int) {
        selectSlot(s)
        val b = Bridge.get(this, s)
        val items = mutableListOf("Load the selected plugin here")
        if (b.isRunning) items += listOf("Editor", "Unload")
        if (SlotManager.assigned(s) != null) items += "Clear the assignment (other apps)"
        AlertDialog.Builder(this).setTitle("${SlotManager.label(s)}").setItems(items.toTypedArray()) { _, which ->
            when (items[which]) {
                "Load the selected plugin here" -> loadSelected()
                "Editor" -> startActivity(Intent(this, if (b.isNativeLoaded) NativeEditorActivity::class.java else EditorActivity::class.java).putExtra(EditorActivity.EXTRA_SLOT, s))
                "Unload" -> unloadCurrent()
                else -> { SlotManager.assign(s, null); refreshSlots() }
            }
        }.show()
    }

    private fun unloadCurrent() {
        val s = current
        keyboard.allOff()
        io.execute {
            Bridge.get(this, s).stop()
            mine.remove(s)
            ui.post { refreshSlots(); selectSlot(s); setStatus("${slotName(s)} unloaded.") }
        }
    }

    // ------------------------------------------------------------------ plugin lifecycle

    private fun loadSelected() {
        val f = plugins.getOrNull(pluginSpinner.selectedItemPosition) ?: return
        keyboard.allOff()
        restartBtn.visibility = View.GONE
        lastStates.remove(current)
        load(current, f, null)
    }

    private fun load(slot: Int, f: File, state: JSONObject?) {
        setStatus("Loading ${NativePlugins.displayName(f)} into ${slotName(slot)}…")
        io.execute {
            val b = Bridge.get(this, slot)
            val r = b.load(f, block, fillBlocks)
            if (!r.ok) { setStatus("Load failed: ${r.message}"); ui.post { refreshSlots() }; return@execute }
            mine += slot
            if (state != null) b.request("set_state\n${state.optString("component")}\n${state.optString("controller")}")
            val info = b.request("info")?.let { JSONObject(it) }
            val instrument = b.isInstrument
            // Other apps (AAP hosts, MIDI apps) see the same slot with the same plugin.
            SlotManager.assign(slot, f.nameWithoutExtension)
            val n = info?.optInt("paramCount") ?: 0
            val wrongSection = (instrument && slot in FX_SLOTS) || (!instrument && slot in INST_SLOTS)
            ui.post {
                refreshSlots()
                if (slot == current) {
                    header.text = "${slotName(slot)}  ·  ${info?.optString("name")}  ·  ${info?.optString("vendor")} ${info?.optString("version")}  ·  " +
                                  (if (instrument) "instrument" else "effect")
                    loadParams()
                }
            }
            setStatus("Loaded in ${r.ms} ms · $n parameters · " +
                      (if (wrongSection) "⚠ an ${if (instrument) "instrument in an FX slot: it plays onto the bus anyway" else "effect in an instrument slot: it joins the FX chain anyway"}, but the sections are the other way round · "
                       else if (instrument) "play the keyboard · " else "it processes the instruments${if (fxTestSignal) " (FX input: test signal)" else ""} · ") +
                      "Editor opens the plugin's own window")
            captureState(slot)
            // adb: --ez selftest true  -> play a chord through the normal MIDI path and log the output peak
            if (intent?.getBooleanExtra("selftest", false) == true) {
                NativeEngine.resetStats(slot)
                if (instrument) for (k in intArrayOf(60, 64, 67)) NativeEngine.sendMidi(slot, 0x90, k, 100)
                Thread.sleep(1500)
                val peak = NativeEngine.stats(slot)[27]
                val st = NativeEngine.stats(slot)
                if (instrument) for (k in intArrayOf(60, 64, 67)) NativeEngine.sendMidi(slot, 0x80, k, 0)
                android.util.Log.i("VSTGHOST", "selftest ${NativePlugins.displayName(f)}: peak %.3f (%.1f dBFS) load %d ms renderAvg %.0f us max %.0f us blocks %.0f underruns %.0f".format(
                    peak, 20 * kotlin.math.log10(peak.coerceAtLeast(1e-9)), r.ms, st[9], st[10], st[8], st[6]))
            }
        }
    }

    private fun restart(keepState: Boolean) {
        val s = current
        val f = bridge.plugin ?: return
        io.execute {
            val st = if (keepState) bridge.request("get_state")?.let { JSONObject(it) } ?: lastStates[s] else null
            ui.post { load(s, f, st) }
        }
    }

    private fun onHostCrashed(slot: Int, code: Int) {
        keyboard.allOff()
        val tail = Bridge.get(this, slot).recentLog(4).joinToString("\n")
        status.text = "⚠ ${slotName(slot)}: the plugin host stopped (exit code $code). Audio output is still running.\n$tail"
        refreshSlots()
        if (slot == current) restartBtn.visibility = View.VISIBLE
    }

    private fun restartAfterCrash() {
        restartBtn.visibility = View.GONE
        bridge.plugin?.let { load(current, it, lastStates[current]) }
    }

    private fun captureState(slot: Int) {
        io.execute { Bridge.get(this, slot).request("get_state")?.let { s -> JSONObject(s).takeIf { it.optBoolean("ok") }?.let { lastStates[slot] = it } } }
    }

    // ------------------------------------------------------------------ parameters

    private fun loadParams() {
        val slot = current
        val arr = Bridge.get(this, slot).request("params")?.let { JSONObject(it).optJSONArray("params") } ?: return
        val list = (0 until arr.length()).map { arr.getJSONObject(it) }.map {
            Param(it.getLong("id"), it.optString("title"), it.optString("units"), it.optInt("steps"), it.optDouble("value"), it.optString("text"))
        }
        ui.post { if (slot == current) { params = list; adapter.notifyDataSetChanged() } }
    }

    private val visible get() = if (filter.isEmpty()) params else params.filter { it.title.lowercase().contains(filter) }
    private val lastSent = HashMap<Long, Long>()

    private fun sendParam(p: Param, v: Double, force: Boolean) {
        val now = SystemClock.elapsedRealtime()
        if (!force && now - (lastSent[p.id] ?: 0) < 30) return
        lastSent[p.id] = now
        val slot = current
        io.execute {
            val r = Bridge.get(this, slot).request("set\n${p.id}\n$v", 2000)?.let { JSONObject(it) } ?: return@execute
            p.value = r.optDouble("value", v); p.text = r.optString("text")
            ui.post { adapter.notifyDataSetChanged() }
        }
        if (force) ui.postDelayed({ captureState(slot) }, 300)
    }

    private val adapter = object : BaseAdapter() {
        override fun getCount() = visible.size
        override fun getItem(i: Int) = visible[i]
        override fun getItemId(i: Int) = visible[i].id
        override fun getView(i: Int, convert: View?, parent: ViewGroup?): View {
            val p = visible[i]
            val row = (convert as? LinearLayout) ?: LinearLayout(this@PlayerActivity).apply {
                orientation = LinearLayout.VERTICAL
                setPadding(0, dp(4), 0, dp(4))
                addView(LinearLayout(context).apply {
                    orientation = LinearLayout.HORIZONTAL
                    addView(TextView(context).apply { textSize = 13f }, LinearLayout.LayoutParams(0, WRAP_CONTENT, 1f))
                    addView(TextView(context).apply { textSize = 13f; setTypeface(typeface, Typeface.BOLD) })
                })
                addView(SeekBar(context))
            }
            val header = row.getChildAt(0) as LinearLayout
            (header.getChildAt(0) as TextView).text = p.title
            (header.getChildAt(1) as TextView).text = "${p.text} ${p.units}".trim()
            val bar = row.getChildAt(1) as SeekBar
            bar.setOnSeekBarChangeListener(null)
            val max = if (p.steps in 1..1000) p.steps else 1000
            bar.max = max
            bar.progress = (p.value * max).toInt()
            bar.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
                override fun onProgressChanged(sb: SeekBar, prog: Int, fromUser: Boolean) {
                    if (fromUser) sendParam(p, prog.toDouble() / max, false)
                }
                override fun onStartTrackingTouch(sb: SeekBar) {}
                override fun onStopTrackingTouch(sb: SeekBar) { sendParam(p, sb.progress.toDouble() / max, true) }
            })
            return row
        }
    }

    // ------------------------------------------------------------------ presets

    private fun presetDir() = File(filesDir, "presets/${loadedName ?: "none"}").apply { mkdirs() }

    private fun presets() {
        val name = loadedName ?: run { status.text = "Load a plugin into ${slotName(current)} first."; return }
        val files = presetDir().listFiles()?.filter { it.name.endsWith(".json") }?.sortedBy { it.name }.orEmpty()
        val items = (listOf("＋ Save current sound as…") + files.map { it.name.removeSuffix(".json") }).toTypedArray()
        AlertDialog.Builder(this).setTitle("Presets: $name").setItems(items) { _, which ->
            if (which == 0) savePreset() else loadPreset(files[which - 1])
        }.show()
    }

    private fun savePreset() {
        val input = EditText(this).apply { hint = "Name" }
        val slot = current
        AlertDialog.Builder(this).setTitle("Save preset").setView(input).setPositiveButton("Save") { _, _ ->
            val name = input.text.toString().trim().replace(Regex("[^A-Za-z0-9 _.-]"), "_").ifEmpty { "Preset" }
            io.execute {
                val st = Bridge.get(this, slot).request("get_state")?.let { JSONObject(it) }
                if (st == null || !st.optBoolean("ok")) { setStatus("Could not read the plugin state."); return@execute }
                st.put("plugin", loadedName); st.put("name", name)
                File(presetDir(), "$name.json").writeText(st.toString())
                setStatus("Saved preset \"$name\".")
            }
        }.setNegativeButton("Cancel", null).show()
    }

    private fun loadPreset(f: File) {
        val slot = current
        io.execute {
            val st = JSONObject(f.readText())
            val r = Bridge.get(this, slot).request("set_state\n${st.optString("component")}\n${st.optString("controller")}")?.let { JSONObject(it) }
            loadParams()
            lastStates[slot] = st
            setStatus(if (r?.optBoolean("ok") == true) "Loaded preset \"${f.name.removeSuffix(".json")}\"." else "Preset load failed: ${r?.optString("error")}")
        }
    }

    // ------------------------------------------------------------------ stats

    private var cpuPrev: Pair<Double, Long>? = null
    private var lastStatsLog = 0L
    private val statsTick = object : Runnable {
        override fun run() {
            if (bridge.isRunning && SystemClock.elapsedRealtime() - lastStatsLog > 5000) {
                lastStatsLog = SystemClock.elapsedRealtime()
                val s = NativeEngine.statsNamed(current)
                android.util.Log.i(TAG, "stats: slot $current callback ${s.callbackFrames} burst ${s.burst} lowLatency ${s.lowLatency} " +
                    "block ${s.block} target ${s.targetFill} fill ${s.fill} underruns ${s.underruns} xruns ${s.oboeXruns} " +
                    "hostAvg ${"%.0f".format(s.hostAvgUs)}us hostMax ${"%.0f".format(s.hostMaxUs)}us restarts ${s.streamRestarts} " +
                    "peak ${"%.1f".format(s.peakDb)} dBFS")
            }
            if (statsPanel.visibility == View.VISIBLE) {
                val s = NativeEngine.statsNamed(current)
                val cpu = bridge.hostCpuSeconds()
                val now = SystemClock.elapsedRealtime()
                var cpuPct = Double.NaN
                if (cpu != null) cpuPrev?.let { (c0, t0) -> cpuPct = (cpu - c0) / ((now - t0) / 1000.0) * 100 }
                cpuPrev = cpu?.let { it to now }
                statsView.text = buildString {
                    appendLine("${slotName(current)}")
                    appendLine("Output   ${s.sampleRate} Hz  burst ${s.burst}  callback ${s.callbackFrames}  buffer ${s.bufferFrames}  ${if (s.lowLatency) "low-latency" else "NOT low-latency (Bluetooth?)"}${if (s.exclusive) " excl" else ""}")
                    appendLine("Bridge   block ${s.block}  target ${s.targetFill} fr (${"%.1f".format(s.targetFill / 48.0)} ms)  fill ${s.fill}  wake ${if (s.futex) "futex" else "poll"}")
                    appendLine("Latency  Oboe out ${"%.1f".format(s.oboeLatencyMs)} ms + bridge ≈ ${"%.1f".format(s.targetFill / 48.0)} ms")
                    appendLine("Glitches underruns ${s.underruns}  Oboe xruns ${s.oboeXruns}")
                    appendLine("Host     render avg ${"%.0f".format(s.hostAvgUs)} µs  max ${"%.0f".format(s.hostMaxUs)} µs  CPU ${if (cpuPct.isNaN()) "-" else "%.0f%%".format(cpuPct)}")
                    append("Callback avg ${"%.0f".format(s.cbAvgUs)} µs  max ${"%.0f".format(s.cbMaxUs)} µs  n=${s.callbacks}")
                }
            }
            ui.postDelayed(this, 500)
        }
    }

    private fun latencyTest() {
        if (!bridge.isRunning) { status.text = "Load a plugin first."; return }
        keyboard.allOff()
        statsView.append("\nLatency test running…")
        io.execute {
            val res = mutableListOf<Double>()
            repeat(8) {
                for (k in 0..127) NativeEngine.sendMidi(0x80, k, 0)
                NativeEngine.sendMidi(0xB0, 64, 0)
                Thread.sleep(700)
                NativeEngine.startProbe(84, 127, -50f)
                val t0 = SystemClock.elapsedRealtime()
                var f = -1L
                while (f < 0 && SystemClock.elapsedRealtime() - t0 < 1000) { Thread.sleep(2); f = NativeEngine.probeResult() }
                if (f >= 0) res += f / 48.0
                NativeEngine.sendMidi(0x80, 84, 0)
            }
            res.sort()
            val s = NativeEngine.statsNamed()
            val med = res.getOrNull(res.size / 2)
            setStatus(if (med == null) "Latency test: no onset detected (patch too quiet or slow attack?)"
                      else "Latency: note-on → output ${"%.1f".format(med)} ms (median of ${res.size}, max ${"%.1f".format(res.last())}) " +
                           "+ Oboe output ${"%.1f".format(s.oboeLatencyMs)} ms ≈ ${"%.0f".format(med + s.oboeLatencyMs)} ms total (touch latency not included)")
        }
    }
}
