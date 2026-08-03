package dev.falconpatch.runtime

import android.content.ContentProvider
import android.content.ContentValues
import android.content.Intent
import android.database.Cursor
import android.net.Uri

class FalconPatchKotlinProvider : ContentProvider() {
    override fun onCreate(): Boolean {
        val currentContext = context ?: return false
        val started = RuntimeBridge.start(currentContext)
        try {
            currentContext.startService(Intent(currentContext, FalconPatchKotlinService::class.java))
        } catch (_: RuntimeException) {
            // The provider already initialized the runtime.
        }
        return started
    }

    override fun query(
        uri: Uri,
        projection: Array<out String>?,
        selection: String?,
        selectionArgs: Array<out String>?,
        sortOrder: String?
    ): Cursor? = null

    override fun getType(uri: Uri): String? = null
    override fun insert(uri: Uri, values: ContentValues?): Uri? = null
    override fun delete(uri: Uri, selection: String?, selectionArgs: Array<out String>?): Int = 0
    override fun update(
        uri: Uri,
        values: ContentValues?,
        selection: String?,
        selectionArgs: Array<out String>?
    ): Int = 0
}
