package dev.falconpatch.runtime;

import android.app.Service;
import android.content.Intent;
import android.os.IBinder;

public final class FalconPatchService extends Service {
    @Override
    public void onCreate() {
        super.onCreate();
        RuntimeBridge.start(this);
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        return START_NOT_STICKY;
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }
}
