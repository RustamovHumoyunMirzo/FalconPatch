package dev.falconpatch.runtime;

import android.content.Context;
import android.content.pm.ApplicationInfo;
import android.content.pm.PackageManager;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.widget.Toast;

public final class RuntimeBridge {
    private static final String TAG = "FalconPatch";
    private static final String LIBRARY_METADATA = "dev.falconpatch.library";
    private static boolean attempted;
    private static boolean started;

    private RuntimeBridge() {}

    public static synchronized boolean start(Context context) {
        if (attempted) {
            return started;
        }
        attempted = true;
        try {
            Context appContext = context.getApplicationContext();
            if (appContext == null) {
                appContext = context;
            }
            ApplicationInfo info = appContext.getPackageManager().getApplicationInfo(
                    appContext.getPackageName(), PackageManager.GET_META_DATA);
            String library = "falconpatch";
            if (info.metaData != null) {
                String configured = info.metaData.getString(LIBRARY_METADATA);
                if (configured != null && configured.matches("[A-Za-z]+")) {
                    library = configured;
                }
            }
            System.loadLibrary(library);
            started = nativeStart(appContext);
        } catch (Throwable error) {
            Log.e(TAG, "Runtime startup failed; continuing without instrumentation.", error);
            started = false;
        }
        return started;
    }

    public static void showToast(final Context context, final String message) {
        new Handler(Looper.getMainLooper()).post(new Runnable() {
            @Override
            public void run() {
                Toast.makeText(context.getApplicationContext(), message, Toast.LENGTH_SHORT).show();
            }
        });
    }

    private static native boolean nativeStart(Context context);
}
