package com.vstghost

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.os.Build
import android.os.IBinder
import android.os.PowerManager
import android.util.Log

/**
 * Keeps the audio engine alive while any slot has a plugin: a media-playback foreground service
 * with a "VST gHost is playing" notification and a partial wake lock. Without it Android freezes
 * or kills the process once the player leaves the screen, which ends the audio for virtual-MIDI
 * and AAP users too. [update] is called by [Bridge] after every load and stop: the service runs
 * while at least one slot is running and stops itself when the last one is unloaded.
 */
class PlaybackService : Service() {
    companion object {
        private const val TAG = "VSTGHOST"
        private const val CHANNEL = "playback"
        private const val NOTIFICATION_ID = 1
        const val ACTION_STOP = "com.vstghost.action.STOP"

        // Audio focus: a phone call or another player taking the output mutes the master until focus
        // returns; the plugins stay loaded. Requested while any slot runs, abandoned after the last unload.
        private var focusRequest: android.media.AudioFocusRequest? = null
        private var ducked = false
        private fun masterGain(ctx: Context): Float {
            val p = ctx.getSharedPreferences("mixer", Context.MODE_PRIVATE)
            val db = p.getFloat("db-1", 0f)
            return if (p.getBoolean("mute-1", false) || db <= -60f) 0f else Math.pow(10.0, db / 20.0).toFloat()
        }
        private fun ensureFocus(ctx: Context) {
            if (focusRequest != null) return
            val am = ctx.getSystemService(android.media.AudioManager::class.java) ?: return
            val attrs = android.media.AudioAttributes.Builder()
                .setUsage(android.media.AudioAttributes.USAGE_MEDIA)
                .setContentType(android.media.AudioAttributes.CONTENT_TYPE_MUSIC).build()
            val req = android.media.AudioFocusRequest.Builder(android.media.AudioManager.AUDIOFOCUS_GAIN)
                .setAudioAttributes(attrs).setWillPauseWhenDucked(false)
                .setOnAudioFocusChangeListener { change ->
                    when (change) {
                        android.media.AudioManager.AUDIOFOCUS_GAIN -> { ducked = false; NativeEngine.setGain(masterGain(ctx)) }
                        android.media.AudioManager.AUDIOFOCUS_LOSS_TRANSIENT_CAN_DUCK -> { ducked = true; NativeEngine.setGain(masterGain(ctx) * 0.25f) }
                        else -> {   // LOSS, LOSS_TRANSIENT: silence and release held notes
                            ducked = true
                            NativeEngine.setGain(0f)
                            for (s in Bridge.activeSlots()) { NativeEngine.sendMidi(s, 0xB0, 64, 0); NativeEngine.sendMidi(s, 0xB0, 123, 0) }
                        }
                    }
                }.build()
            if (am.requestAudioFocus(req) == android.media.AudioManager.AUDIOFOCUS_REQUEST_GRANTED) focusRequest = req
        }
        private fun abandonFocus(ctx: Context) {
            val req = focusRequest ?: return
            ctx.getSystemService(android.media.AudioManager::class.java)?.abandonAudioFocusRequest(req)
            focusRequest = null
            if (ducked) { ducked = false; NativeEngine.setGain(masterGain(ctx)) }
        }

        fun update(context: Context) {
            val ctx = context.applicationContext
            val names = Bridge.activeSlots().map { s ->
                SlotManager.label(s).removePrefix("VST gHost ") + ": " +
                    (Bridge.get(ctx, s).plugin?.let { NativePlugins.displayName(it).removeSuffix(" · native") } ?: "?")
            }
            val intent = Intent(ctx, PlaybackService::class.java)
            if (names.isEmpty()) {
                abandonFocus(ctx)
                ctx.stopService(intent)
            } else {
                ensureFocus(ctx)
                intent.putExtra("text", names.joinToString(" · "))
                runCatching { ctx.startForegroundService(intent) }
                    .onFailure { Log.w(TAG, "playback service: $it") }
            }
        }
    }

    private var wakeLock: PowerManager.WakeLock? = null

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        if (intent?.action == ACTION_STOP) {
            Thread {
                Bridge.stopAll()
                for (s in Bridge.allSlots()) SlotManager.assign(s, null)
                stopSelf()
            }.start()
            return START_NOT_STICKY
        }
        val nm = getSystemService(NotificationManager::class.java)
        nm.createNotificationChannel(NotificationChannel(CHANNEL, "Playing", NotificationManager.IMPORTANCE_LOW).apply {
            description = "Shown while a plugin is loaded, so the audio keeps running in the background"
            setShowBadge(false)
        })
        val open = PendingIntent.getActivity(this, 0, Intent(this, PlayerActivity::class.java),
            PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT)
        val stop = PendingIntent.getService(this, 1, Intent(this, PlaybackService::class.java).setAction(ACTION_STOP),
            PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT)
        val n = Notification.Builder(this, CHANNEL)
            .setSmallIcon(R.drawable.ic_stat_ghost)
            .setContentTitle("VST gHost is playing")
            .setContentText(intent?.getStringExtra("text") ?: "")
            .setContentIntent(open)
            .setOngoing(true)
            .setOnlyAlertOnce(true)
            .addAction(Notification.Action.Builder(null, "Stop", stop).build())
            .build()
        // Android refuses a foreground start from an app that is neither visible nor bound by a
        // visible one (e.g. a load triggered while the screen is off). Then we stay a plain service:
        // the audio keeps running as long as the process does, and the next load retries.
        try {
            if (Build.VERSION.SDK_INT >= 29)
                startForeground(NOTIFICATION_ID, n, ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PLAYBACK)
            else
                startForeground(NOTIFICATION_ID, n)
        } catch (e: Exception) {
            Log.w(TAG, "playback service: foreground start refused: $e")
        }
        if (wakeLock == null) {
            wakeLock = getSystemService(PowerManager::class.java).newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "vstghost:playback")
                .apply { setReferenceCounted(false); acquire() }
        }
        return START_STICKY
    }

    override fun onDestroy() {
        wakeLock?.let { if (it.isHeld) it.release() }
        wakeLock = null
        super.onDestroy()
    }
}
