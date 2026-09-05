#include "fp_internal.h"

JNIEXPORT jboolean JNICALL
Java_dev_falconpatch_runtime_RuntimeBridge_nativeStart(JNIEnv *env, jclass type,
                                                        jobject context) {
    if (!g_fp_runtime.bridge_class && type) {
        g_fp_runtime.bridge_class = (jclass)(*env)->NewGlobalRef(env, type);
    }
    return fp_runtime_start(env, context) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jstring JNICALL
Java_dev_falconpatch_runtime_RuntimeBridge_nativeDispatchHook(
        JNIEnv *env, jclass type, jstring class_name, jint object_id,
        jstring method_name, jstring signature, jstring encoded_result) {
    (void)type;
    return fp_method_hook_dispatch(env, class_name, object_id, method_name,
                                   signature, encoded_result);
}
