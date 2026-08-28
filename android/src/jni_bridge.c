#include "fp_internal.h"

JNIEXPORT jboolean JNICALL
Java_dev_falconpatch_runtime_RuntimeBridge_nativeStart(JNIEnv *env, jclass type,
                                                        jobject context) {
    if (!g_fp_runtime.bridge_class && type) {
        g_fp_runtime.bridge_class = (jclass)(*env)->NewGlobalRef(env, type);
    }
    return fp_runtime_start(env, context) ? JNI_TRUE : JNI_FALSE;
}
