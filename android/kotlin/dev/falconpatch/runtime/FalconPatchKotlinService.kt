package dev.falconpatch.runtime

import android.app.Service
import android.content.Intent
import android.os.IBinder

class FalconPatchKotlinService : Service() {
    override fun onCreate() {
        super.onCreate()
        RuntimeBridge.start(this)
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int = START_NOT_STICKY
    override fun onBind(intent: Intent?): IBinder? = null
}
