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

        fun update(context: Context) {
            val ctx = context.applicationContext
            val names = Bridge.activeSlots().map { s ->
                SlotManager.label(s).removePrefix("VST gHost ") + ": " +
                    (Bridge.get(ctx, s).plugin?.let { NativePlugins.displayName(it).removeSuffix(" · native") } ?: "?")
            }
            val intent = Intent(ctx, PlaybackService::class.java)
            if (names.isEmpty()) {
                ctx.stopService(intent)
            } else {
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
