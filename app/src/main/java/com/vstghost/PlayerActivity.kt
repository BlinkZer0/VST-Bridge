// SPDX-License-Identifier: GPL-3.0-or-later
package com.vstghost

import androidx.appcompat.app.AppCompatActivity
import com.google.android.material.button.MaterialButton
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import com.google.android.material.textfield.MaterialAutoCompleteTextView
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
class PlayerActivity : AppCompatActivity() {
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
    private lateinit var pluginPicker: MaterialAutoCompleteTextView
    private var selectedPlugin = 0
    private lateinit var keyboard: KeyboardView
    private lateinit var paramList: ListView
    private lateinit var statsView: TextView
    private lateinit var statsPanel: LinearLayout
    private lateinit var sustainBtn: Button
    private lateinit var fxInputBtn: Button
    private lateinit var restartBtn: Button
    private lateinit var loadBtn: Button
    private val slotBtns = HashMap<Int, MaterialButton>()

    private var plugins: List<File> = emptyList()
    private val slotBars = HashMap<Int, MeterBar>()
    /** The one reader of the engine's peak meters (they reset on read): feeds the slot cards and the mixer. */
    private var mixerSink: ((Map<Int, Float>) -> Unit)? = null
    private val meterTick = object : Runnable {
        override fun run() {
            val peaks = HashMap<Int, Float>()
            for (s in INST_SLOTS + FX_SLOTS) {
                val p = NativeEngine.slotMeter(s)
                peaks[s] = p
                slotBars[s]?.feed(p)
            }
            peaks[MixerView.MASTER] = NativeEngine.masterMeter()
            mixerSink?.invoke(peaks)
            ui.postDelayed(this, 33)
        }
    }
    private var params: List<Param> = emptyList()
    private var filter = ""
    private var sustain = false
    private var block = 256
    private var fillBlocks = 2
    private val lastStates = HashMap<Int, JSONObject>()
    private var fxTestSignal = false
    private var selftestOnce = false

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
            applyMixerPrefs()
            ui.post {
                if (android.os.Build.VERSION.SDK_INT >= 33 &&
                    checkSelfPermission(android.Manifest.permission.POST_NOTIFICATIONS) != android.content.pm.PackageManager.PERMISSION_GRANTED)
                    requestPermissions(arrayOf(android.Manifest.permission.POST_NOTIFICATIONS), 7)
            }
            setStatus("Ready. Pick a plugin, choose a slot and press Load.")
            // adb extras (below) run once. The activity is singleTask, and Android replays a dead
            // task's launch intent when the user opens the app again: without this, a test's
            // `--es load X --ez selftest true` kept loading X and playing its chord at every start.
            val adb = intent?.extras?.let { b -> b.keySet().sorted().joinToString("|") { "$it=${b.get(it)}" } }.orEmpty()
            val prefs = getSharedPreferences("player", MODE_PRIVATE)
            val replay = adb.isNotEmpty() && prefs.getString("lastAdbIntent", "") == adb
            if (adb.isNotEmpty() && !replay) prefs.edit().putString("lastAdbIntent", adb).apply()
            val intent = if (replay) null else intent
            // adb: am start -n <pkg>/com.vstghost.PlayerActivity --es assign "1=E-Piano;5=Pedal Faster"
            // (to repeat an identical command, add a changing extra such as --el t <seconds>)
            intent?.getStringExtra("assign")?.split(";")?.forEach { kv ->
                val (k, v) = kv.split("=", limit = 2).let { it[0].trim() to it.getOrNull(1)?.trim() }
                k.toIntOrNull()?.takeIf { it in 1..8 }?.let { SlotManager.assign(it, v?.ifEmpty { null }) }
            }
            selftestOnce = intent?.getBooleanExtra("selftest", false) == true
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
                    intent?.getIntExtra("slot", 0)?.takeIf { it in 1..8 }?.let { selectSlot(it) }
                    loadByName(name)
                }
            }
        }
        ui.postDelayed(statsTick, 500)
    }

    // ------------------------------------------------------------------ add plugins (import)

    private fun addPluginDialog() {
        MaterialAlertDialogBuilder(this).setTitle("Add a Windows VST3 plugin")
            .setItems(arrayOf("A .vst3 file or a .zip download", "A .vst3 folder (bundle)")) { _, which ->
                if (which == 0) startActivityForResult(Intent(Intent.ACTION_OPEN_DOCUMENT).apply {
                    addCategory(Intent.CATEGORY_OPENABLE)
                    type = "*/*"
                }, REQ_IMPORT_FILE)
                else startActivityForResult(Intent(Intent.ACTION_OPEN_DOCUMENT_TREE), REQ_IMPORT_TREE)
            }.setNegativeButton("Cancel", null).show()
    }

    /**
     * ⋮ → Install plugin: lists .vst3 files/bundles and zips in Download, Documents, the storage
     * root and SD cards, and installs the tapped one. Needs "All files access" (scoped storage
     * hides other apps' downloads otherwise); asks for it once.
     */
    private fun installFromFolders() {
        if (android.os.Build.VERSION.SDK_INT >= 30 && !android.os.Environment.isExternalStorageManager()) {
            MaterialAlertDialogBuilder(this).setTitle("Allow access to your files")
                .setMessage("To find plugins in Download, Documents and on SD cards, VST gHost needs \"All files access\". Android opens a settings page: switch it on and come back.")
                .setPositiveButton("Open settings") { _, _ ->
                    startActivity(Intent(android.provider.Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION, android.net.Uri.parse("package:$packageName")))
                }.setNegativeButton("Cancel", null).show()
            return
        }
        setStatus("Looking for plugins in your folders…")
        io.execute {
            val found = PluginImport(this).scanCommonFolders()
            ui.post {
                if (found.isEmpty()) {
                    setStatus("No .vst3 or zip files found in Download, Documents or on SD cards.")
                    return@post
                }
                val labels = found.map { c ->
                    val size = if (c.file.isFile) " · %.1f MB".format(c.file.length() / 1048576.0) else ""
                    (if (c.installable) "" else "✗ ") + "${c.file.name}\n${c.where}$size · ${c.note}"
                }.toTypedArray()
                MaterialAlertDialogBuilder(this).setTitle("Install a plugin").setItems(labels) { _, which ->
                    val c = found[which]
                    if (!c.installable) { setStatus("${c.file.name}: ${c.note}"); return@setItems }
                    setStatus("Installing ${c.file.name}…")
                    io.execute {
                        val r = runCatching { PluginImport(this).importFile(c.file) { msg -> ui.post { setStatus(msg) } } }
                            .getOrElse { PluginImport.Result(emptyList(), "Install failed: ${it.message}") }
                        setStatus(r.message + if (r.imported.isNotEmpty()) " · pick it in the plugin list and press Load" else "")
                        ui.post { refreshPlugins() }
                    }
                }.setNegativeButton("Cancel", null).show()
                setStatus("${found.count { it.installable }} installable file(s) found.")
            }
        }
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

    /**
     * adb while the player is running (singleTop): `am start -n <pkg>/.PlayerActivity`
     *   --ei slot N              select a slot
     *   --es load <name>         load that plugin into the selected slot
     *   --es req "cmd\narg..."   send a request to the selected slot's plugin, answer in logcat
     */
    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        intent.getIntExtra("slot", 0).takeIf { it in 1..8 }?.let { selectSlot(it) }
        intent.getStringExtra("load")?.let { loadByName(it) }
        intent.getStringExtra("req")?.let { r ->
            val slot = current
            io.execute { android.util.Log.i(TAG, "req slot $slot ${r.lineSequence().first()}: ${Bridge.get(this, slot).request(r.replace("\\n", "\n"))}") }
        }
    }

    override fun onResume() {
        super.onResume()
        // The Tests screen points the keyboard/MIDI at slot 0 while it runs; take it back.
        if (current in INST_SLOTS) NativeEngine.playerSlot = current
        refreshSlots()   // the notification's Stop may have unloaded everything meanwhile
        ui.removeCallbacks(meterTick); ui.post(meterTick)
    }

    override fun onPause() {
        ui.removeCallbacks(meterTick)
        super.onPause()
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
        setContentView(R.layout.activity_player)
        pluginPicker = findViewById(R.id.pluginPicker)
        pluginPicker.setOnItemClickListener { _, _, pos, _ -> selectedPlugin = pos }
        header = findViewById(R.id.header)
        status = findViewById(R.id.status)
        midiStatus = findViewById(R.id.midiStatus)
        keyboard = findViewById(R.id.keyboard)
        paramList = findViewById(R.id.paramList)
        statsView = findViewById(R.id.statsView)
        statsPanel = findViewById(R.id.statsPanel)
        sustainBtn = findViewById(R.id.sustainBtn)
        fxInputBtn = findViewById(R.id.fxInputBtn)
        restartBtn = findViewById(R.id.restartBtn)
        loadBtn = findViewById(R.id.loadBtn)

        loadBtn.setOnClickListener { loadSelected() }
        findViewById<Button>(R.id.addBtn).setOnClickListener { addPluginDialog() }
        findViewById<Button>(R.id.editorBtn).setOnClickListener {
            if (!bridge.isRunning) android.widget.Toast.makeText(this, "Load a plugin into ${slotName(current)} first", android.widget.Toast.LENGTH_SHORT).show()
            else if (bridge.isNativeLoaded)
                startActivity(Intent(this, NativeEditorActivity::class.java).putExtra(EditorActivity.EXTRA_SLOT, current))
            else startActivity(Intent(this, EditorActivity::class.java).putExtra(EditorActivity.EXTRA_SLOT, current))
        }
        findViewById<Button>(R.id.unloadBtn).setOnClickListener { unloadCurrent() }
        findViewById<Button>(R.id.presetsBtn).setOnClickListener { presets() }
        findViewById<Button>(R.id.moreBtn).setOnClickListener { v ->
            android.widget.PopupMenu(this, v).apply {
                menu.add("Mixer").setOnMenuItemClickListener { showMixer(); true }
                menu.add("Install plugin…").setOnMenuItemClickListener { installFromFolders(); true }
                menu.add("Uninstall plugin…").setOnMenuItemClickListener { uninstallPlugin(); true }
                menu.add("Stats").setOnMenuItemClickListener { statsPanel.visibility = if (statsPanel.visibility == View.VISIBLE) View.GONE else View.VISIBLE; true }
                menu.add("Tests").setOnMenuItemClickListener { startActivity(Intent(this@PlayerActivity, MainActivity::class.java)); true }
            }.show()
        }
        restartBtn.setOnClickListener { restartAfterCrash() }

        // The slot strip: tapping a slot selects it, long-press for its menu.
        fun slotRow(rowId: Int, slots: IntRange) {
            val row = findViewById<LinearLayout>(rowId)
            for (s in slots) {
                val b = MaterialButton(this, null, com.google.android.material.R.attr.materialButtonOutlinedStyle).apply {
                    setTextAppearance(com.google.android.material.R.style.TextAppearance_Material3_LabelMedium)
                    isAllCaps = false; textSize = 12f; maxLines = 1; ellipsize = android.text.TextUtils.TruncateAt.END
                    minHeight = 0; minimumHeight = 0; insetTop = dp(1); insetBottom = dp(1)
                    setPadding(dp(8), 0, dp(8), 0)
                    isCheckable = true
                    setOnClickListener { isChecked = true; selectSlot(s) }
                    setOnLongClickListener { slotMenu(s); true }
                }
                slotBtns[s] = b
                val bar = MeterBar(this)
                slotBars[s] = bar
                val cell = LinearLayout(this).apply {
                    orientation = LinearLayout.VERTICAL
                    addView(b, LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, dp(34)))
                    addView(bar, LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, dp(4)).apply {
                        leftMargin = dp(10); rightMargin = dp(10); topMargin = dp(-2); bottomMargin = dp(1) })
                }
                row.addView(cell, LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f).apply { leftMargin = dp(3); rightMargin = dp(3) })
            }
        }
        slotRow(R.id.instRow, INST_SLOTS)
        slotRow(R.id.fxRow, FX_SLOTS)

        sustainBtn.setOnClickListener {
            sustain = !sustain
            NativeEngine.sendMidi(0xB0, 64, if (sustain) 127 else 0)
            (sustainBtn as MaterialButton).isChecked = sustain
        }
        findViewById<Button>(R.id.octDownBtn).setOnClickListener { keyboard.allOff(); keyboard.firstOctave -= 1 }
        findViewById<Button>(R.id.octUpBtn).setOnClickListener { keyboard.allOff(); keyboard.firstOctave += 1 }
        fxInputBtn.setOnClickListener {
            fxTestSignal = !fxTestSignal
            NativeEngine.setFxInput(if (fxTestSignal) 1 else 0)
            fxInputBtn.text = "FX: ${if (fxTestSignal) "test" else "inst"}"
        }

        val blockSpin = findViewById<Spinner>(R.id.blockSpin).apply {
            adapter = ArrayAdapter(this@PlayerActivity, android.R.layout.simple_spinner_dropdown_item, listOf("block 128", "block 256", "block 512"))
            setSelection(1)
        }
        val fillSpin = findViewById<Spinner>(R.id.fillSpin).apply {
            adapter = ArrayAdapter(this@PlayerActivity, android.R.layout.simple_spinner_dropdown_item, (1..8).map { "buffer ×$it" })
            setSelection(1)
        }
        findViewById<Button>(R.id.applyBtn).setOnClickListener {
            block = listOf(128, 256, 512)[blockSpin.selectedItemPosition]
            fillBlocks = fillSpin.selectedItemPosition + 1
            if (loadedName != null) restart(keepState = true)
        }
        findViewById<Button>(R.id.latencyBtn).setOnClickListener { latencyTest() }
        findViewById<Button>(R.id.resetStatsBtn).setOnClickListener { NativeEngine.resetStats() }

        findViewById<EditText>(R.id.search).addTextChangedListener(object : TextWatcher {
            override fun afterTextChanged(s: Editable?) { filter = s.toString().trim().lowercase(); adapter.notifyDataSetChanged() }
            override fun beforeTextChanged(s: CharSequence?, a: Int, b: Int, c: Int) {}
            override fun onTextChanged(s: CharSequence?, a: Int, b: Int, c: Int) {}
        })
        paramList.adapter = adapter
        keyboard.onNote = { note, vel, on -> NativeEngine.sendMidi(if (on) 0x90 else 0x80, note, vel) }
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
        pluginPicker.setAdapter(ArrayAdapter(this, R.layout.item_plugin, names))
        selectedPlugin = selectedPlugin.coerceIn(0, (plugins.size - 1).coerceAtLeast(0))
        pluginPicker.setText(names[selectedPlugin.coerceAtMost(names.size - 1)], false)
    }

    // ------------------------------------------------------------------ slots

    /** What a slot shows: its loaded plugin, or empty. (Loading also assigns the slot for other apps.) */
    private fun slotText(s: Int): String {
        val b = Bridge.get(this, s)
        val name = b.plugin?.let { NativePlugins.displayName(it).removeSuffix(" · native") }
        return "${slotName(s)}: ${name ?: "—"}" + if (b.plugin != null && !b.isRunning) " ✗" else ""
    }

    private fun refreshSlots() {
        for ((s, b) in slotBtns) {
            b.text = slotText(s)
            b.isChecked = s == current
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
        SlotManager.assigned(s)?.takeIf { b.plugin == null }?.let { a ->
            val shown = plugins.firstOrNull { it.nameWithoutExtension == a }?.let { NativePlugins.displayName(it) } ?: a
            items += "Other apps still see \"$shown\" here: clear"
        }
        MaterialAlertDialogBuilder(this).setTitle("${SlotManager.label(s)}").setItems(items.toTypedArray()) { _, which ->
            when (items[which]) {
                "Load the selected plugin here" -> loadSelected()
                "Editor" -> startActivity(Intent(this, if (b.isNativeLoaded) NativeEditorActivity::class.java else EditorActivity::class.java).putExtra(EditorActivity.EXTRA_SLOT, s))
                "Unload" -> unloadCurrent()
                else -> { SlotManager.assign(s, null); refreshSlots() }
            }
        }.show()
    }

    // ------------------------------------------------------------------ mixer

    private val mixerPrefs get() = getSharedPreferences("mixer", MODE_PRIVATE)

    /** Faders and mutes persist across runs; the engine gets them at start and whenever they move. */
    private fun applyMixerPrefs() {
        val p = mixerPrefs
        for (s in INST_SLOTS + FX_SLOTS + listOf(MixerView.MASTER)) {
            val db = p.getFloat("db$s", 0f)
            val mute = p.getBoolean("mute$s", false)
            val g = if (mute || db <= MixerView.MIN_DB) 0f else Math.pow(10.0, db / 20.0).toFloat()
            if (s == MixerView.MASTER) NativeEngine.setGain(g) else NativeEngine.setSlotGain(s, g)
        }
    }

    /** ⋮ → Mixer: eight strips with live meters (blue → red), faders and mutes. */
    private fun showMixer() {
        val mixer = MixerView(this).apply {
            slots = (INST_SLOTS + FX_SLOTS).toList() + MixerView.MASTER
            names = { s -> if (s == MixerView.MASTER) "Master" else slotText(s).removeSuffix(" ✗") }
            val p = mixerPrefs
            for (s in slots) setGainDb(s, p.getFloat("db$s", 0f), p.getBoolean("mute$s", false))
            onGain = { s, g ->
                if (s == MixerView.MASTER) NativeEngine.setGain(g) else NativeEngine.setSlotGain(s, g)
                mixerPrefs.edit().putFloat("db$s", gainDb(s)).putBoolean("mute$s", isMuted(s)).apply()
            }
        }
        val dialog = MaterialAlertDialogBuilder(this).setTitle("Mixer")
            .setView(mixer).setPositiveButton("Close", null).create()
        dialog.setOnShowListener {
            dialog.window?.setLayout((resources.displayMetrics.widthPixels * 0.96f).toInt(), android.view.WindowManager.LayoutParams.WRAP_CONTENT)
            mixerSink = { peaks -> mixer.update(peaks) }
        }
        dialog.setOnDismissListener { mixerSink = null }
        dialog.show()
    }

    // ------------------------------------------------------------------ uninstall

    /** ⋮ → Uninstall plugin: removes an imported Windows plugin (native builds are part of the app). */
    private fun uninstallPlugin() {
        val removable = plugins.filter { !NativePlugins.isNative(it) }
        if (removable.isEmpty()) { setStatus("No imported plugins to uninstall."); return }
        val labels = removable.map { f ->
            val where = (INST_SLOTS + FX_SLOTS).filter { Bridge.get(this, it).plugin?.path == f.path }
            f.name.removeSuffix(".vst3") + if (where.isEmpty()) "" else "  (loaded in ${where.joinToString { slotName(it) }})"
        }.toTypedArray()
        MaterialAlertDialogBuilder(this).setTitle("Uninstall a plugin").setItems(labels) { _, which ->
            val f = removable[which]
            MaterialAlertDialogBuilder(this).setTitle("Uninstall ${f.name.removeSuffix(".vst3")}?")
                .setMessage("The plugin files are deleted from VST gHost. Saved presets in files/presets are kept.")
                .setPositiveButton("Uninstall") { _, _ ->
                    setStatus("Uninstalling ${f.name}…")
                    io.execute {
                        for (s in INST_SLOTS + FX_SLOTS) {
                            val b = Bridge.get(this, s)
                            if (b.plugin?.path == f.path) { b.stop(); mine.remove(s); SlotManager.assign(s, null) }
                        }
                        val ok = f.deleteRecursively()
                        ui.post {
                            refreshPlugins(); refreshSlots(); selectSlot(current)
                            setStatus(if (ok) "${f.name.removeSuffix(".vst3")} uninstalled." else "Could not delete ${f.name}.")
                        }
                    }
                }.setNegativeButton("Cancel", null).show()
        }.setNegativeButton("Cancel", null).show()
    }

    private fun unloadCurrent() {
        val s = current
        keyboard.allOff()
        io.execute {
            Bridge.get(this, s).stop()
            mine.remove(s)
            SlotManager.assign(s, null)  // empty for other apps too
            ui.post { refreshSlots(); selectSlot(s); setStatus("${slotName(s)} unloaded.") }
        }
    }

    // ------------------------------------------------------------------ plugin lifecycle

    private fun loadSelected() {
        val f = plugins.getOrNull(selectedPlugin) ?: return
        keyboard.allOff()
        restartBtn.visibility = View.GONE
        lastStates.remove(current)
        load(current, f, null)
    }

    /** adb `--es load <name>`: by file name, not through the spinner (its selection updates asynchronously). */
    private fun loadByName(name: String) {
        val i = plugins.indexOfFirst { it.nameWithoutExtension == name }
        if (i < 0) { setStatus("No plugin named \"$name\"."); return }
        selectedPlugin = i
        pluginPicker.setText(NativePlugins.displayName(plugins[i]), false)
        keyboard.allOff()
        restartBtn.visibility = View.GONE
        lastStates.remove(current)
        load(current, plugins[i], null)
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
            if (selftestOnce) {
                selftestOnce = false
                // An effect is fed by the instrument bus, which is silent here: feed it the test signal
                // for the measurement (the engine's "FX: test" switch), then put the switch back.
                if (!instrument && !fxTestSignal) NativeEngine.setFxInput(1)
                NativeEngine.resetStats(slot)
                if (instrument) for (k in intArrayOf(60, 64, 67)) NativeEngine.sendMidi(slot, 0x90, k, 100)
                Thread.sleep(1500)
                val peak = NativeEngine.stats(slot)[27]
                val st = NativeEngine.stats(slot)
                if (instrument) for (k in intArrayOf(60, 64, 67)) NativeEngine.sendMidi(slot, 0x80, k, 0)
                if (!instrument && !fxTestSignal) NativeEngine.setFxInput(0)
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
            (header.getChildAt(1) as TextView).text = if (p.units.isNotEmpty() && p.text.trim().endsWith(p.units)) p.text.trim() else "${p.text} ${p.units}".trim()
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

    /**
     * `.vstpreset` files for the loaded (bridged) plugin: shipped in its bundle
     * (Contents/Resources/Presets) or installed in the prefix's "VST3 Presets" folders
     * (Documents, ProgramData, Public Documents), where they sit under <vendor>/<plugin>.
     */
    private fun vstPresetFiles(): List<File> {
        val b = bridge
        val plug = b.plugin ?: return emptyList()
        if (b.isNativeLoaded) return emptyList()
        val key = plug.nameWithoutExtension.lowercase()
        val bundle = File(plug, "Contents/Resources/Presets")
        val shared = listOf("users/vstghost/Documents", "ProgramData", "users/Public/Documents")
            .map { File(payload.root, "prefix/drive_c/$it/VST3 Presets") }
        val found = mutableListOf<File>()
        found += presetDir().listFiles()?.filter { it.isFile && it.extension.equals("vstpreset", true) }.orEmpty()
        if (bundle.isDirectory) found += bundle.walkTopDown().filter { it.isFile && it.extension.equals("vstpreset", true) }
        for (root in shared) if (root.isDirectory)
            found += root.walkTopDown().filter { it.isFile && it.extension.equals("vstpreset", true) && it.relativeTo(root).path.lowercase().contains(key) }
        return found.sortedBy { it.path.lowercase() }
    }

    private fun presets() {
        val name = loadedName ?: run { status.text = "Load a plugin into ${slotName(current)} first."; return }
        val slot = current
        setStatus("Reading presets…")
        io.execute {
            // Factory programs (VST3 program lists / JUCE programs), .vstpreset files, user presets.
            data class Item(val label: String, val action: () -> Unit)
            val items = mutableListOf<Item>()
            val b = Bridge.get(this, slot)
            val lists = b.request("programs")?.let { runCatching { JSONObject(it).optJSONArray("lists") }.getOrNull() }
            if (lists != null) for (li in 0 until lists.length()) {
                val l = lists.getJSONObject(li)
                if (!l.optBoolean("selectable", false)) continue
                val progs = l.optJSONArray("programs") ?: continue
                val cur = l.optInt("current", -1)
                val listName = l.optString("name").ifEmpty { "Programs" }
                for (pi in 0 until progs.length()) {
                    val mark = if (pi == cur) "● " else "   "
                    items += Item("$mark$listName: ${progs.getString(pi)}") { setProgram(slot, l.optInt("id"), pi, progs.getString(pi)) }
                }
            }
            for (f in vstPresetFiles()) items += Item("   ▣ ${f.nameWithoutExtension}") { loadVstPreset(slot, f) }
            val user = presetDir().listFiles()?.filter { it.name.endsWith(".json") }?.sortedBy { it.name }.orEmpty()
            for (f in user) items += Item("   ★ ${f.name.removeSuffix(".json")}") { loadPreset(f) }
            setStatus(if (items.isEmpty()) "No factory presets found for $name." else "${items.size} presets.")
            ui.post {
                val labels = (listOf("＋ Save current sound as…") + items.map { it.label }).toTypedArray()
                MaterialAlertDialogBuilder(this).setTitle("Presets: $name").setItems(labels) { _, which ->
                    if (which == 0) savePreset() else items[which - 1].action()
                }.show()
            }
        }
    }

    /** A file's path for the Wine host: inside the prefix as C:\..., elsewhere the unix path (the host maps it). */
    private fun hostPath(f: File): String {
        val driveC = File(payload.root, "prefix/drive_c")
        return if (f.path.startsWith(driveC.path + "/")) "C:\\" + f.relativeTo(driveC).path.replace('/', '\\') else f.path
    }

    private fun setProgram(slot: Int, listId: Int, index: Int, label: String) {
        io.execute {
            val r = Bridge.get(this, slot).request("set_program\n$listId\n$index")?.let { JSONObject(it) }
            loadParams()
            captureState(slot)
            setStatus(if (r?.optBoolean("ok") == true) "Program: $label" else "Program change failed: ${r?.optString("error")}")
        }
    }

    private fun loadVstPreset(slot: Int, f: File) {
        io.execute {
            val r = Bridge.get(this, slot).request("load_preset\n${hostPath(f)}")?.let { JSONObject(it) }
            loadParams()
            captureState(slot)
            setStatus(if (r?.optBoolean("ok") == true) "Loaded preset \"${f.nameWithoutExtension}\"." else "Preset load failed: ${r?.optString("error")}")
        }
    }

    private fun savePreset() {
        val input = EditText(this).apply { hint = "Name" }
        val slot = current
        MaterialAlertDialogBuilder(this).setTitle("Save preset").setView(input).setPositiveButton("Save") { _, _ ->
            val name = input.text.toString().trim().replace(Regex("[^A-Za-z0-9 _.-]"), "_").ifEmpty { "Preset" }
            io.execute {
                val b = Bridge.get(this, slot)
                if (!b.isNativeLoaded) {
                    // Bridged plugins: a standard .vstpreset (readable by any VST3 host) in the app's
                    // own preset folder. Not inside the Wine prefix: the prefix is part of the payload
                    // and is replaced on every app update.
                    val r = b.request("save_preset\n${hostPath(File(presetDir(), "$name.vstpreset"))}")?.let { JSONObject(it) }
                    setStatus(if (r?.optBoolean("ok") == true) "Saved preset \"$name\" (.vstpreset)." else "Could not save: ${r?.optString("error")}")
                    return@execute
                }
                val st = b.request("get_state")?.let { JSONObject(it) }
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
