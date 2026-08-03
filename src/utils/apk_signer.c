#include "utils/apk_signer.h"
#include "utils/file_utils.h"
#include "utils/process.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int temp_path(const char *base, const char *suffix,
                     char *output, size_t output_size) {
    char random[9];
    int written;
    if (!fpatch_random_alpha(random, 8)) {
        return 0;
    }
    written = snprintf(output, output_size, "%s.fpatch-%s.%s", base, random, suffix);
    return written >= 0 && (size_t)written < output_size;
}

int fpatch_prepare_keystore(const FpatchInjectProfile *profile,
                            const char *output_directory,
                            char *keystore, size_t keystore_size,
                            char *error, size_t error_size) {
    char keytool[FPATCH_PATH_MAX];
    const char *arguments[24];
    char validity[] = "10000";
    size_t index = 0;

    if (profile->no_sign) {
        keystore[0] = '\0';
        return 1;
    }
    if (profile->keystore[0]) {
        if (!fpatch_file_exists(profile->keystore)) {
            snprintf(error, error_size, "Keystore does not exist: %s", profile->keystore);
            return 0;
        }
        if (strlen(profile->keystore) >= keystore_size) {
            snprintf(error, error_size, "Keystore path is too long.");
            return 0;
        }
        snprintf(keystore, keystore_size, "%s", profile->keystore);
        return 1;
    }
    if (!fpatch_path_join(keystore, keystore_size, output_directory,
                          "falconpatch-debug.keystore")) {
        snprintf(error, error_size, "Default keystore path is too long.");
        return 0;
    }
    if (fpatch_file_exists(keystore)) {
        return 1;
    }
    if (!fpatch_find_executable("keytool", keytool, sizeof(keytool))) {
        snprintf(error, error_size,
                 "keytool was not found. Supply --keystore or install a JDK.");
        return 0;
    }
    arguments[index++] = keytool;
    arguments[index++] = "-genkeypair";
    arguments[index++] = "-keystore";
    arguments[index++] = keystore;
    arguments[index++] = "-storepass";
    arguments[index++] = profile->store_password;
    arguments[index++] = "-keypass";
    arguments[index++] = profile->key_password;
    arguments[index++] = "-alias";
    arguments[index++] = profile->key_alias;
    arguments[index++] = "-keyalg";
    arguments[index++] = "RSA";
    arguments[index++] = "-keysize";
    arguments[index++] = "2048";
    arguments[index++] = "-validity";
    arguments[index++] = validity;
    arguments[index++] = "-dname";
    arguments[index++] = "CN=FalconPatch Debug,O=FalconPatch,C=UZ";
    arguments[index++] = "-noprompt";
    arguments[index] = NULL;
    return fpatch_run_process(keytool, arguments, error, error_size);
}

int fpatch_align_and_sign_apk(const char *input, const char *output,
                              const FpatchInjectProfile *profile,
                              const char *keystore,
                              char *error, size_t error_size) {
    char zipalign[FPATCH_PATH_MAX];
    char aligned[FPATCH_PATH_MAX];
    const char *align_arguments[8];

    if (!fpatch_find_android_build_tool("zipalign", zipalign, sizeof(zipalign))) {
        snprintf(error, error_size,
                 "zipalign was not found in Android SDK build-tools.");
        return 0;
    }
    if (!temp_path(output, "aligned.apk", aligned, sizeof(aligned))) {
        snprintf(error, error_size, "Temporary APK path is too long.");
        return 0;
    }
    align_arguments[0] = zipalign;
    align_arguments[1] = "-f";
    align_arguments[2] = "-p";
    align_arguments[3] = "4";
    align_arguments[4] = input;
    align_arguments[5] = aligned;
    align_arguments[6] = NULL;
    if (!fpatch_run_process(zipalign, align_arguments, error, error_size)) {
        remove(aligned);
        return 0;
    }
    if (profile->no_sign) {
        if (!fpatch_replace_file(aligned, output, error, error_size)) {
            remove(aligned);
            return 0;
        }
        return 1;
    }

    {
        char java[FPATCH_PATH_MAX];
        char signer[FPATCH_PATH_MAX];
        char store_password[300];
        char key_password[300];
        char signed_apk[FPATCH_PATH_MAX];
        const char *sign_arguments[24];
        const char *verify_arguments[12];
        size_t index = 0;

        if (!fpatch_find_executable("java", java, sizeof(java))) {
            snprintf(error, error_size, "Java was not found; apksigner cannot run.");
            remove(aligned);
            return 0;
        }
        if (!fpatch_find_apksigner_jar(signer, sizeof(signer))) {
            snprintf(error, error_size,
                     "apksigner.jar was not found in Android SDK build-tools.");
            remove(aligned);
            return 0;
        }
        if (!temp_path(output, "signed.apk", signed_apk, sizeof(signed_apk)) ||
            snprintf(store_password, sizeof(store_password), "pass:%s", profile->store_password) < 0 ||
            snprintf(key_password, sizeof(key_password), "pass:%s", profile->key_password) < 0) {
            snprintf(error, error_size, "Signing parameters are too long.");
            remove(aligned);
            return 0;
        }
        sign_arguments[index++] = java;
        sign_arguments[index++] = "-jar";
        sign_arguments[index++] = signer;
        sign_arguments[index++] = "sign";
        sign_arguments[index++] = "--ks";
        sign_arguments[index++] = keystore;
        sign_arguments[index++] = "--ks-key-alias";
        sign_arguments[index++] = profile->key_alias;
        sign_arguments[index++] = "--ks-pass";
        sign_arguments[index++] = store_password;
        sign_arguments[index++] = "--key-pass";
        sign_arguments[index++] = key_password;
        sign_arguments[index++] = "--out";
        sign_arguments[index++] = signed_apk;
        sign_arguments[index++] = aligned;
        sign_arguments[index] = NULL;
        if (!fpatch_run_process(java, sign_arguments, error, error_size)) {
            remove(aligned);
            remove(signed_apk);
            return 0;
        }
        verify_arguments[0] = java;
        verify_arguments[1] = "-jar";
        verify_arguments[2] = signer;
        verify_arguments[3] = "verify";
        verify_arguments[4] = "--verbose";
        verify_arguments[5] = signed_apk;
        verify_arguments[6] = NULL;
        if (!fpatch_run_process(java, verify_arguments, error, error_size) ||
            !fpatch_replace_file(signed_apk, output, error, error_size)) {
            remove(aligned);
            remove(signed_apk);
            return 0;
        }
        remove(aligned);
    }
    return 1;
}
