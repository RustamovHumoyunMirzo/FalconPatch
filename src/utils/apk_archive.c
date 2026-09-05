#include "utils/apk_archive.h"
#include "utils/axml.h"
#include "utils/dex_transformer.h"
#include "utils/file_utils.h"
#include "utils/payload_archive.h"

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
        if (tolower((unsigned char)value[i]) != tolower((unsigned char)suffix[i])) {
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

static int read_entry(zip_t *archive, const char *name,
                      unsigned char **data, size_t *size,
                      char *error, size_t error_size) {
    zip_stat_t stat;
    zip_file_t *file;
    zip_int64_t read_count;

    *data = NULL;
    *size = 0;
    zip_stat_init(&stat);
    if (zip_stat(archive, name, 0, &stat) != 0 || stat.size > SIZE_MAX) {
        snprintf(error, error_size, "Cannot inspect APK entry: %s", name);
        return 0;
    }
    file = zip_fopen(archive, name, 0);
    if (!file) {
        snprintf(error, error_size, "Cannot open APK entry: %s", name);
        return 0;
    }
    *data = (unsigned char *)malloc((size_t)stat.size ? (size_t)stat.size : 1);
    if (!*data) {
        zip_fclose(file);
        snprintf(error, error_size, "Out of memory while reading APK entry: %s", name);
        return 0;
    }
    read_count = zip_fread(file, *data, stat.size);
    zip_fclose(file);
    if (read_count < 0 || (zip_uint64_t)read_count != stat.size) {
        free(*data);
        *data = NULL;
        snprintf(error, error_size, "Cannot read APK entry: %s", name);
        return 0;
    }
    *size = (size_t)stat.size;
    return 1;
}

static unsigned int dex_number(const char *name) {
    const char *cursor;
    unsigned int value = 0;

    if (strcmp(name, "classes.dex") == 0) {
        return 1;
    }
    if (strncmp(name, "classes", 7) != 0) {
        return 0;
    }
    cursor = name + 7;
    if (!isdigit((unsigned char)*cursor)) {
        return 0;
    }
    while (isdigit((unsigned char)*cursor)) {
        if (value > 100000u) {
            return 0;
        }
        value = value * 10u + (unsigned int)(*cursor++ - '0');
    }
    return strcmp(cursor, ".dex") == 0 ? value : 0;
}

static int asset_destination(const char *source, char *destination, size_t size) {
    int written = snprintf(destination, size, "assets/falconpatch/user/%s",
                           fpatch_path_basename(source));
    return written >= 0 && (size_t)written < size;
}

static int native_destination(const char *abi, const char *filename,
                              char *destination, size_t size) {
    int written = snprintf(destination, size, "lib/%s/%s", abi, filename);
    return written >= 0 && (size_t)written < size;
}

static int entry_will_be_replaced(const char *name, const FpatchArchivePatch *patch) {
    size_t i;

    if (strcmp(name, "AndroidManifest.xml") == 0 ||
        strcmp(name, "assets/falconpatch/runtime.bin") == 0) {
        return 1;
    }
    for (i = 0; i < patch->target_abi_count; i++) {
        char destination[512];
        char filename[192];
        snprintf(filename, sizeof(filename), "lib%s.so", patch->runtime_library);
        if (native_destination(patch->target_abis[i], filename,
                               destination, sizeof(destination)) &&
            strcmp(name, destination) == 0) {
            return 1;
        }
    }
    for (i = 0; i < patch->profile->native_count; i++) {
        char destination[512];
        char filename[256];
        if (fpatch_native_filename(&patch->profile->native[i], filename, sizeof(filename)) &&
            native_destination(patch->profile->native[i].abi, filename,
                               destination, sizeof(destination)) &&
            strcmp(name, destination) == 0) {
            return 1;
        }
    }
    for (i = 0; i < patch->profile->asset_count; i++) {
        char destination[FPATCH_PATH_MAX];
        if (asset_destination(patch->profile->assets[i], destination, sizeof(destination)) &&
            strcmp(name, destination) == 0) {
            return 1;
        }
    }
    return 0;
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

static int add_buffer_entry(zip_t *archive, const char *name,
                            const unsigned char *data, size_t size,
                            zip_int32_t compression,
                            char *error, size_t error_size) {
    zip_source_t *source = zip_source_buffer(archive, data, size, 0);
    zip_int64_t index;
    if (!source) {
        snprintf(error, error_size, "Cannot allocate ZIP source for: %s", name);
        return 0;
    }
    index = zip_file_add(archive, name, source, ZIP_FL_ENC_UTF_8 | ZIP_FL_OVERWRITE);
    if (index < 0) {
        zip_source_free(source);
        snprintf(error, error_size, "Cannot add APK entry: %s", name);
        return 0;
    }
    if (zip_set_file_compression(archive, (zip_uint64_t)index, compression, 0) != 0) {
        snprintf(error, error_size, "Cannot set APK compression for: %s", name);
        return 0;
    }
    return 1;
}

static int add_owned_buffer_entry(zip_t *archive, const char *name,
                                  unsigned char *data, size_t size,
                                  zip_int32_t compression,
                                  char *error, size_t error_size) {
    zip_source_t *source = zip_source_buffer(archive, data, size, 1);
    zip_int64_t index;
    if (!source) {
        free(data);
        snprintf(error, error_size, "Cannot allocate ZIP source for: %s", name);
        return 0;
    }
    index = zip_file_add(archive, name, source, ZIP_FL_ENC_UTF_8 | ZIP_FL_OVERWRITE);
    if (index < 0) {
        zip_source_free(source);
        snprintf(error, error_size, "Cannot add transformed APK entry: %s", name);
        return 0;
    }
    if (zip_set_file_compression(archive, (zip_uint64_t)index, compression, 0) != 0) {
        snprintf(error, error_size, "Cannot set APK compression for: %s", name);
        return 0;
    }
    return 1;
}

static int add_disk_entry(zip_t *archive, const char *name, const char *path,
                          zip_int32_t compression,
                          char *error, size_t error_size) {
    zip_source_t *source = zip_source_file(archive, path, 0, -1);
    zip_int64_t index;
    if (!source) {
        snprintf(error, error_size, "Cannot read injection input: %s", path);
        return 0;
    }
    index = zip_file_add(archive, name, source, ZIP_FL_ENC_UTF_8 | ZIP_FL_OVERWRITE);
    if (index < 0) {
        zip_source_free(source);
        snprintf(error, error_size, "Cannot add injection input: %s", path);
        return 0;
    }
    if (zip_set_file_compression(archive, (zip_uint64_t)index, compression, 0) != 0) {
        snprintf(error, error_size, "Cannot set APK compression for: %s", name);
        return 0;
    }
    if (compression == ZIP_CM_STORE) {
        zip_file_set_external_attributes(archive, (zip_uint64_t)index, 0,
                                         ZIP_OPSYS_UNIX, (zip_uint32_t)(0100755u << 16));
    }
    return 1;
}

static int copy_regular_entries(zip_t *source, zip_t *target,
                                const FpatchArchivePatch *patch,
                                unsigned int *highest_dex,
                                char *error, size_t error_size) {
    zip_int64_t count = zip_get_num_entries(source, 0);
    zip_int64_t i;
    for (i = 0; i < count; i++) {
        const char *name = zip_get_name(source, (zip_uint64_t)i, ZIP_FL_UNCHANGED);
        unsigned int number;
        if (!name) {
            continue;
        }
        number = dex_number(name);
        if (number > *highest_dex) {
            *highest_dex = number;
        }
        if (is_signature_entry(name) || entry_will_be_replaced(name, patch)) {
            continue;
        }
        if (number && patch->profile->dex_patch_count > 0) {
            unsigned char *dex = NULL;
            size_t dex_size = 0;
            FpatchDexTransformStats stats = {0};
            if (!read_entry(source, name, &dex, &dex_size, error, error_size) ||
                !fpatch_transform_dex(dex, dex_size,
                                      patch->profile->dex_patches,
                                      patch->profile->dex_patch_count,
                                      patch->dex_patch_applied, &stats,
                                      error, error_size)) {
                free(dex);
                return 0;
            }
            if (patch->dex_methods_patched) {
                *patch->dex_methods_patched += stats.methods_patched;
            }
            if (patch->dex_strings_replaced) {
                *patch->dex_strings_replaced += stats.strings_replaced;
            }
            if (!add_owned_buffer_entry(target, name, dex, dex_size,
                                        ZIP_CM_DEFLATE, error, error_size)) {
                return 0;
            }
            continue;
        }
        if (!copy_entry(source, target, (zip_uint64_t)i, name, error, error_size)) {
            return 0;
        }
    }
    return 1;
}

int fpatch_patch_base_archive(const char *source_path, const char *output_path,
                              const FpatchArchivePatch *patch,
                              char *strategy_used, size_t strategy_used_size,
                              char *error, size_t error_size) {
    zip_t *source = NULL;
    zip_t *target = NULL;
    int zip_error = 0;
    unsigned char *manifest = NULL;
    size_t manifest_size = 0;
    unsigned char *patched_manifest = NULL;
    size_t patched_manifest_size = 0;
    unsigned int highest_dex = 0;
    char dex_name[64];
    size_t i;
    int make_debuggable = strcmp(patch->profile->strategy, "provider-debuggable") == 0;
    int success = 0;

    if (patch->runtime_count != patch->target_abi_count) {
        snprintf(error, error_size, "Runtime payload count does not match target ABI count.");
        return 0;
    }
    for (i = 0; i < patch->target_abi_count; i++) {
        if (!patch->runtimes[i].data || !patch->runtimes[i].size ||
            strcmp(patch->runtimes[i].abi, patch->target_abis[i]) != 0) {
            snprintf(error, error_size, "Runtime payload does not match target ABI %s.",
                     patch->target_abis[i]);
            return 0;
        }
    }

    source = zip_open(source_path, ZIP_RDONLY, &zip_error);
    if (!source) {
        snprintf(error, error_size, "Cannot open source APK (zip error %d): %s",
                 zip_error, source_path);
        goto done;
    }
    if (!read_entry(source, "AndroidManifest.xml", &manifest, &manifest_size,
                    error, error_size)) {
        goto done;
    }
    if (!fpatch_axml_add_bootstrap(manifest, manifest_size, patch->package_name,
                                   patch->runtime_library,
                                   patch->profile->bootstrap_language,
                                   make_debuggable,
                                   &patched_manifest, &patched_manifest_size,
                                   error, error_size)) {
        if (strcmp(patch->profile->strategy, "auto") != 0 ||
            !fpatch_axml_add_bootstrap(manifest, manifest_size, patch->package_name,
                                      patch->runtime_library,
                                      patch->profile->bootstrap_language, 1,
                                      &patched_manifest, &patched_manifest_size,
                                      error, error_size)) {
            goto done;
        }
        make_debuggable = 1;
    }
    snprintf(strategy_used, strategy_used_size, "%s",
             make_debuggable ? "provider-debuggable" : "provider");

    target = zip_open(output_path, ZIP_CREATE | ZIP_TRUNCATE, &zip_error);
    if (!target) {
        snprintf(error, error_size, "Cannot create patched APK (zip error %d): %s",
                 zip_error, output_path);
        goto done;
    }
    if (!copy_regular_entries(source, target, patch, &highest_dex, error, error_size) ||
        !add_buffer_entry(target, "AndroidManifest.xml", patched_manifest,
                          patched_manifest_size, ZIP_CM_DEFLATE, error, error_size) ||
        !add_buffer_entry(target, "assets/falconpatch/runtime.bin",
                          patch->payload, patch->payload_size,
                          ZIP_CM_DEFLATE, error, error_size)) {
        goto done;
    }
    for (i = 0; i < patch->profile->dex_patch_count; i++) {
        if (!patch->dex_patch_applied || patch->dex_patch_applied[i] == 0) {
            const FpatchDexPatch *dex_patch = &patch->profile->dex_patches[i];
            if (dex_patch->action == FPATCH_DEX_PATCH_REPLACE_STRING) {
                snprintf(error, error_size,
                         "DEX patch %zu matched no string in target class DEX: %s",
                         i + 1, dex_patch->target);
            } else {
                snprintf(error, error_size,
                         "DEX patch %zu matched no method: %s.%s",
                         i + 1, dex_patch->target, dex_patch->method);
            }
            goto done;
        }
    }
    snprintf(dex_name, sizeof(dex_name), "classes%u.dex", highest_dex + 1u);
    if (highest_dex == 0) {
        snprintf(dex_name, sizeof(dex_name), "classes.dex");
    }
    if (!add_buffer_entry(target, dex_name, patch->bootstrap_dex,
                          patch->bootstrap_dex_size, ZIP_CM_DEFLATE,
                          error, error_size)) {
        goto done;
    }
    for (i = 0; i < patch->runtime_count; i++) {
        const FpatchRuntimePayload *runtime = &patch->runtimes[i];
        char filename[192];
        char destination[512];
        snprintf(filename, sizeof(filename), "lib%s.so", patch->runtime_library);
        if (!runtime->size ||
            !native_destination(runtime->abi, filename,
                                destination, sizeof(destination)) ||
            !add_buffer_entry(target, destination, runtime->data, runtime->size,
                              ZIP_CM_STORE, error, error_size)) {
            if (!error[0]) {
                snprintf(error, error_size, "Runtime is missing for ABI %s.",
                         runtime->abi);
            }
            goto done;
        }
    }
    for (i = 0; i < patch->profile->native_count; i++) {
        char filename[256];
        char destination[512];
        if (!fpatch_native_filename(&patch->profile->native[i], filename, sizeof(filename)) ||
            !native_destination(patch->profile->native[i].abi, filename,
                                destination, sizeof(destination)) ||
            !add_disk_entry(target, destination, patch->profile->native[i].path,
                            ZIP_CM_STORE, error, error_size)) {
            goto done;
        }
    }
    for (i = 0; i < patch->profile->asset_count; i++) {
        char destination[FPATCH_PATH_MAX];
        if (!asset_destination(patch->profile->assets[i], destination, sizeof(destination)) ||
            !add_disk_entry(target, destination, patch->profile->assets[i],
                            ZIP_CM_DEFLATE, error, error_size)) {
            goto done;
        }
    }
    if (zip_close(target) != 0) {
        snprintf(error, error_size, "Cannot finalize patched APK: %s", output_path);
        goto done;
    }
    target = NULL;
    success = 1;

done:
    if (target) {
        zip_discard(target);
    }
    if (source) {
        zip_discard(source);
    }
    free(manifest);
    free(patched_manifest);
    return success;
}

int fpatch_repack_split_archive(const char *source_path, const char *output_path,
                                char *error, size_t error_size) {
    zip_t *source = NULL;
    zip_t *target = NULL;
    int zip_error = 0;
    zip_int64_t count;
    zip_int64_t i;
    int success = 0;

    source = zip_open(source_path, ZIP_RDONLY, &zip_error);
    if (!source) {
        snprintf(error, error_size, "Cannot open split APK: %s", source_path);
        goto done;
    }
    target = zip_open(output_path, ZIP_CREATE | ZIP_TRUNCATE, &zip_error);
    if (!target) {
        snprintf(error, error_size, "Cannot create split APK output: %s", output_path);
        goto done;
    }
    count = zip_get_num_entries(source, 0);
    for (i = 0; i < count; i++) {
        const char *name = zip_get_name(source, (zip_uint64_t)i, ZIP_FL_UNCHANGED);
        if (!name || is_signature_entry(name)) {
            continue;
        }
        if (!copy_entry(source, target, (zip_uint64_t)i, name, error, error_size)) {
            goto done;
        }
    }
    if (zip_close(target) != 0) {
        snprintf(error, error_size, "Cannot finalize split APK: %s", output_path);
        goto done;
    }
    target = NULL;
    success = 1;

done:
    if (target) {
        zip_discard(target);
    }
    if (source) {
        zip_discard(source);
    }
    return success;
}
