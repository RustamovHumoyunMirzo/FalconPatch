package dev.falconpatch.runtime;

import android.content.ContentProvider;
import android.content.ContentValues;
import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;

public final class FalconPatchProvider extends ContentProvider {
    @Override
    public boolean onCreate() {
        if (getContext() == null) {
            return false;
        }
        boolean started = RuntimeBridge.start(getContext());
        try {
            getContext().startService(new Intent(getContext(), FalconPatchService.class));
        } catch (RuntimeException ignored) {
            // The provider already initialized the runtime; background-service policy may reject this.
        }
        return started;
    }

    @Override
    public Cursor query(Uri uri, String[] projection, String selection,
                        String[] selectionArgs, String sortOrder) {
        return null;
    }

    @Override
    public String getType(Uri uri) {
        return null;
    }

    @Override
    public Uri insert(Uri uri, ContentValues values) {
        return null;
    }

    @Override
    public int delete(Uri uri, String selection, String[] selectionArgs) {
        return 0;
    }

    @Override
    public int update(Uri uri, ContentValues values, String selection,
                      String[] selectionArgs) {
        return 0;
    }
}
