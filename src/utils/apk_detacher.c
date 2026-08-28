#include "utils/apk_detacher.h"
#include "utils/apk_signer.h"
#include "utils/file_utils.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zip.h>

static int ends_with_case(const char *value, const char *suffix) {
    size_t value_size = strlen(value);
    size_t suffix_size = strlen(suffix);
    size_t i;
    if (value_size < suffix_size) {
        return 0;
    }
    value += value_size - suffix_size;
    for (i = 0; i < suffix_size; i++) {
        if (tolower((unsigned char)value[i]) !=
            tolower((unsigned char)suffix[i])) {
            return 0;
        }
    }
    return 1;
}

static int starts_with_case(const char *value, const char *prefix) {
    size_t i;
    for (i = 0; prefix[i]; i++) {
        if (!value[i] || tolower((unsigned char)value[i]) !=
                         tolower((unsigned char)prefix[i])) {
            return 0;
        }
    }
    return 1;
}

static int is_signature_entry(const char *name) {
    if (!starts_with_case(name, "META-INF/")) {
        return 0;
    }
    return ends_with_case(name, ".RSA") || ends_with_case(name, ".DSA") ||
           ends_with_case(name, ".EC") || ends_with_case(name, ".SF") ||
           ends_with_case(name, "/MANIFEST.MF") ||
           strcmp(name + 9, "MANIFEST.MF") == 0;
}

static int copy_entry(zip_t *source, zip_t *target, zip_uint64_t source_index,
                      const char *name, char *error, size_t error_size) {
    zip_source_t *entry_source;
    zip_int64_t target_index;
    zip_uint8_t operating_system;
    zip_uint32_t attributes;

    entry_source = zip_source_zip_file(target, source, source_index,
                                       ZIP_FL_UNCHANGED | ZIP_FL_COMPRESSED,
                                       0, -1, NULL);
    if (!entry_source) {
        snprintf(error, error_size, "Cannot copy compressed APK entry: %s", name);
        return 0;
    }
    target_index = zip_file_add(target, name, entry_source, ZIP_FL_ENC_GUESS);
    if (target_index < 0) {
        zip_source_free(entry_source);
        snprintf(error, error_size, "Cannot add APK entry: %s", name);
        return 0;
    }
    if (zip_file_get_external_attributes(source, source_index, ZIP_FL_UNCHANGED,
                                         &operating_system, &attributes) == 0) {
        zip_file_set_external_attributes(target, (zip_uint64_t)target_index, 0,
                                         operating_system, attributes);
    }
    return 1;
}

static int copy_text(char *destination, size_t size, const char *value) {
    size_t length = value ? strlen(value) : 0;
    if (length >= size) {
        return 0;
    }
    if (value) {
        memcpy(destination, value, length + 1);
    } else {
        destination[0] = '\0';
    }
    return 1;
}

static int normalize_library_name(const char *input, char *output, size_t size) {
    const char *base;
    size_t length;

    if (!input || !input[0]) {
        return 0;
    }
    base = fpatch_path_basename(input);
    if (strncmp(base, "lib", 3) == 0) {
        base += 3;
    }
    length = strlen(base);
    if (length > 3 && ends_with_case(base, ".so")) {
        length -= 3;
    }
    if (length == 0 || length + 7 >= size) {
        return 0;
    }
    snprintf(output, size, "lib%.*s.so", (int)length, base);
    return 1;
}

static int abi_allowed(const FpatchDetachRequest *request, const char *abi) {
    size_t i;
    if (request->abi_count == 0) {
        return 1;
    }
    for (i = 0; i < request->abi_count; i++) {
        if (strcmp(request->abis[i], "-a") == 0 ||
            strcmp(request->abis[i], "all") == 0 ||
            strcmp(request->abis[i], abi) == 0) {
            return 1;
        }
    }
    return 0;
}

static int parse_native_entry(const char *name, char *abi, size_t abi_size,
                              const char **filename) {
    const char *start;
    const char *slash;
    size_t length;

    if (strncmp(name, "lib/", 4) != 0) {
        return 0;
    }
    start = name + 4;
    slash = strchr(start, '/');
    if (!slash || !slash[1]) {
        return 0;
    }
    length = (size_t)(slash - start);
    if (length == 0 || length >= abi_size) {
        return 0;
    }
    memcpy(abi, start, length);
    abi[length] = '\0';
    *filename = slash + 1;
    return 1;
}

static int should_strip_falconpatch_entry(const char *name, const char *detached_library) {
    if (strcmp(detached_library, "libfalconpatch.so") != 0) {
        return 0;
    }
    return strcmp(name, "assets/falconpatch/runtime.bin") == 0;
}

static int make_temp_path(const char *base, const char *suffix,
                          char *output, size_t output_size) {
    char random[9];
    int written;
    if (!fpatch_random_alpha(random, 8)) {
        return 0;
    }
    written = snprintf(output, output_size, "%s.fpatch-%s.%s", base, random, suffix);
    return written >= 0 && (size_t)written < output_size;
}

static void detach_profile(const FpatchDetachRequest *request,
                           FpatchInjectProfile *profile) {
    fpatch_profile_init(profile);
    profile->no_sign = request->sign ? 0 : 1;
    copy_text(profile->keystore, sizeof(profile->keystore), request->keystore);
    if (request->key_alias[0]) {
        copy_text(profile->key_alias, sizeof(profile->key_alias), request->key_alias);
    }
    if (request->store_password[0]) {
        copy_text(profile->store_password, sizeof(profile->store_password),
                  request->store_password);
    }
    if (request->key_password[0]) {
        copy_text(profile->key_password, sizeof(profile->key_password),
                  request->key_password);
    }
}

void fpatch_detach_request_init(FpatchDetachRequest *request) {
    memset(request, 0, sizeof(*request));
}

int fpatch_detach_apk(const FpatchDetachRequest *request,
                      FpatchDetachResult *result,
                      char *error, size_t error_size) {
    zip_t *source = NULL;
    zip_t *target = NULL;
    int zip_error = 0;
    zip_int64_t count;
    zip_int64_t i;
    char library[128];
    char raw[FPATCH_PATH_MAX];
    char output_directory[FPATCH_PATH_MAX];
    char keystore[FPATCH_PATH_MAX];
    FpatchInjectProfile signing_profile;
    int success = 0;

    memset(result, 0, sizeof(*result));
    if (!request->target[0] || !request->output[0] || !request->library[0]) {
        snprintf(error, error_size, "detach requires --target, --so, and --out.");
        return 0;
    }
    if (request->smart_repair) {
        snprintf(error, error_size,
                 "--smart-repair needs DEX call-site rewriting and is not enabled yet.");
        return 0;
    }
    if (!normalize_library_name(request->library, library, sizeof(library))) {
        snprintf(error, error_size, "Invalid --so library name.");
        return 0;
    }
    if (strcmp(request->target, request->output) == 0) {
        snprintf(error, error_size, "Output APK must not overwrite the target APK.");
        return 0;
    }
    if (!make_temp_path(request->output, "raw.apk", raw, sizeof(raw))) {
        snprintf(error, error_size, "Temporary output path is too long.");
        return 0;
    }
    fpatch_path_dirname(request->output, output_directory, sizeof(output_directory));
    if (!fpatch_make_directories(output_directory)) {
        snprintf(error, error_size, "Cannot create output directory: %s", output_directory);
        return 0;
    }

    source = zip_open(request->target, ZIP_RDONLY, &zip_error);
    if (!source) {
        snprintf(error, error_size, "Cannot open target APK (zip error %d): %s",
                 zip_error, request->target);
        goto done;
    }
    target = zip_open(raw, ZIP_CREATE | ZIP_TRUNCATE, &zip_error);
    if (!target) {
        snprintf(error, error_size, "Cannot create detached APK (zip error %d): %s",
                 zip_error, raw);
        goto done;
    }
    count = zip_get_num_entries(source, 0);
    for (i = 0; i < count; i++) {
        const char *name = zip_get_name(source, (zip_uint64_t)i, ZIP_FL_UNCHANGED);
        char abi[32];
        const char *filename = NULL;
        int remove_entry = 0;
        if (!name || is_signature_entry(name)) {
            continue;
        }
        if (parse_native_entry(name, abi, sizeof(abi), &filename) &&
            strcmp(filename, library) == 0 && abi_allowed(request, abi)) {
            if (result->removed_count < FPATCH_MAX_DETACH_ABIS) {
                snprintf(result->removed_entries[result->removed_count],
                         sizeof(result->removed_entries[result->removed_count]),
                         "%s", name);
                result->removed_count++;
            }
            remove_entry = 1;
        } else if (should_strip_falconpatch_entry(name, library)) {
            result->stripped_falconpatch_payload = 1;
            remove_entry = 1;
        }
        if (!remove_entry &&
            !copy_entry(source, target, (zip_uint64_t)i, name, error, error_size)) {
            goto done;
        }
    }
    if (zip_close(target) != 0) {
        snprintf(error, error_size, "Cannot finalize detached APK: %s", raw);
        goto done;
    }
    target = NULL;
    if (result->removed_count == 0) {
        snprintf(error, error_size, "No matching %s entry was found.", library);
        goto done;
    }

    if (request->sign) {
        detach_profile(request, &signing_profile);
        if (!fpatch_prepare_keystore(&signing_profile, output_directory,
                                     keystore, sizeof(keystore),
                                     error, error_size) ||
            !fpatch_align_and_sign_apk(raw, request->output, &signing_profile,
                                       keystore, error, error_size)) {
            goto done;
        }
    } else if (!fpatch_replace_file(raw, request->output, error, error_size)) {
        goto done;
    }
    snprintf(result->output, sizeof(result->output), "%s", request->output);
    snprintf(result->library, sizeof(result->library), "%s", library);
    result->resigned = request->sign;
    success = 1;

done:
    if (target) {
        zip_discard(target);
    }
    if (source) {
        zip_discard(source);
    }
    remove(raw);
    return success;
}
