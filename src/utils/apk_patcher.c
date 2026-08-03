#include "utils/apk_patcher.h"
#include "utils/apk_archive.h"
#include "utils/apk_inspector.h"
#include "utils/apk_signer.h"
#include "utils/artifact_bundle.h"
#include "utils/embedded_resources.h"
#include "utils/file_utils.h"
#include "utils/payload_archive.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char source[FPATCH_PATH_MAX];
    char target[FPATCH_PATH_MAX];
    char raw[FPATCH_PATH_MAX];
    char ready[FPATCH_PATH_MAX];
} OutputArtifact;

static FpatchEmbeddedResource find_resource(const FpatchArtifactBundle *bundle,
                                            const char *kind, const char *name) {
    FpatchEmbeddedResource resource = {0};
    if (bundle) {
        const FpatchArtifactFile *file = fpatch_artifact_bundle_find(bundle, kind, name);
        if (file) {
            resource.data = file->data;
            resource.size = file->size;
        }
        return resource;
    }
    return fpatch_embedded_find(kind, name);
}

static int same_path(const char *left, const char *right) {
#ifdef _WIN32
    return _stricmp(left, right) == 0;
#else
    return strcmp(left, right) == 0;
#endif
}

static int add_abi(char abis[16][32], size_t *count, const char *abi) {
    size_t i;
    if (!abi || !abi[0]) {
        return 1;
    }
    for (i = 0; i < *count; i++) {
        if (strcmp(abis[i], abi) == 0) {
            return 1;
        }
    }
    if (*count >= 16 || strlen(abi) >= sizeof(abis[0])) {
        return 0;
    }
    snprintf(abis[*count], sizeof(abis[*count]), "%s", abi);
    (*count)++;
    return 1;
}

static int collect_apk_info(const char *path, ManifestInfo *base_manifest,
                            char abis[16][32], size_t *abi_count,
                            char *error, size_t error_size) {
    ApkInfo *info = (ApkInfo *)calloc(1, sizeof(*info));
    size_t i;
    int result = 0;

    if (!info) {
        snprintf(error, error_size, "Out of memory while inspecting %s.", path);
        return 0;
    }
    if (!fpatch_inspect_apk(path, info) || !info->has_manifest || !info->manifest.has_manifest) {
        snprintf(error, error_size, "Input is not a patchable APK: %s", path);
        goto done;
    }
    if (base_manifest) {
        *base_manifest = info->manifest;
    }
    for (i = 0; i < info->abi_count; i++) {
        if (!add_abi(abis, abi_count, info->abis[i])) {
            snprintf(error, error_size, "Too many Android ABIs across the APK set.");
            goto done;
        }
    }
    result = 1;

done:
    free(info);
    return result;
}

static int validate_inputs(FpatchInjectProfile *profile,
                           char abis[16][32], size_t *abi_count,
                           char *error, size_t error_size) {
    size_t i;
    size_t j;

    if (!fpatch_file_exists(profile->source)) {
        snprintf(error, error_size, "Source APK does not exist: %s", profile->source);
        return 0;
    }
    for (i = 0; i < profile->split_count; i++) {
        if (!fpatch_file_exists(profile->splits[i])) {
            snprintf(error, error_size, "Split APK does not exist: %s", profile->splits[i]);
            return 0;
        }
    }
    for (i = 0; i < profile->native_count; i++) {
        char detected[32];
        if (!fpatch_file_exists(profile->native[i].path) ||
            !fpatch_elf_abi(profile->native[i].path, detected, sizeof(detected),
                            error, error_size)) {
            return 0;
        }
        if (profile->native[i].abi[0] && strcmp(profile->native[i].abi, detected) != 0) {
            snprintf(error, error_size,
                     "Native ABI mismatch for %s: profile says %s, ELF is %s.",
                     profile->native[i].path, profile->native[i].abi, detected);
            return 0;
        }
        snprintf(profile->native[i].abi, sizeof(profile->native[i].abi), "%s", detected);
        if (!add_abi(abis, abi_count, detected)) {
            snprintf(error, error_size, "Too many target ABIs.");
            return 0;
        }
        for (j = 0; j < i; j++) {
            char current_name[256];
            char prior_name[256];
            if (strcmp(profile->native[j].abi, detected) == 0 &&
                fpatch_native_filename(&profile->native[i], current_name, sizeof(current_name)) &&
                fpatch_native_filename(&profile->native[j], prior_name, sizeof(prior_name)) &&
                strcmp(current_name, prior_name) == 0) {
                snprintf(error, error_size,
                         "Two native inputs target the same APK library path: %s/%s.",
                         detected, current_name);
                return 0;
            }
        }
    }
    for (i = 0; i < profile->lua_count; i++) {
        if (!fpatch_file_exists(profile->lua[i].path)) {
            snprintf(error, error_size, "Lua script does not exist: %s", profile->lua[i].path);
            return 0;
        }
    }
    for (i = 0; i < profile->asset_count; i++) {
        if (!fpatch_file_exists(profile->assets[i])) {
            snprintf(error, error_size, "Asset does not exist: %s", profile->assets[i]);
            return 0;
        }
        for (j = 0; j < i; j++) {
            if (strcmp(fpatch_path_basename(profile->assets[i]),
                       fpatch_path_basename(profile->assets[j])) == 0) {
                snprintf(error, error_size,
                         "Asset basenames must be unique: %s.",
                         fpatch_path_basename(profile->assets[i]));
                return 0;
            }
        }
    }
    return 1;
}

static int choose_default_abis(const FpatchArtifactBundle *bundle,
                               char abis[16][32], size_t *abi_count,
                               char *error, size_t error_size) {
    static const char *supported[] = {
        "armeabi-v7a", "arm64-v8a", "x86", "x86_64"
    };
    size_t i;

    if (*abi_count) {
        return 1;
    }
    for (i = 0; i < sizeof(supported) / sizeof(supported[0]); i++) {
        FpatchEmbeddedResource runtime = find_resource(bundle, "runtime", supported[i]);
        if (runtime.size && !add_abi(abis, abi_count, supported[i])) {
            break;
        }
    }
    if (!*abi_count) {
        if (bundle) {
            snprintf(error, error_size,
                     "Artifact package %s contains no supported Android runtime.", bundle->package);
        } else {
            snprintf(error, error_size,
                     "No embedded Android runtime is available. Supply --artifacts or rebuild fpatch after scripts/build_android.ps1.");
        }
        return 0;
    }
    return 1;
}

static int validate_runtime_abis(const FpatchArtifactBundle *bundle,
                                 char abis[16][32], size_t abi_count,
                                 char *error, size_t error_size) {
    size_t i;
    for (i = 0; i < abi_count; i++) {
        if (!find_resource(bundle, "runtime", abis[i]).size) {
            if (bundle) {
                snprintf(error, error_size,
                         "Artifact package %s has no libfalconpatch.so for %s.",
                         bundle->package, abis[i]);
            } else {
                snprintf(error, error_size,
                         "fpatch has no embedded libfalconpatch.so for %s. Supply --artifacts or rebuild the Android runtime and host CLI.",
                         abis[i]);
            }
            return 0;
        }
    }
    return 1;
}

static int make_temp_path(const char *target, const char *stage,
                          char *output, size_t output_size) {
    char random[9];
    int written;
    if (!fpatch_random_alpha(random, 8)) {
        return 0;
    }
    written = snprintf(output, output_size, "%s.fpatch-%s.%s.apk",
                       target, random, stage);
    return written >= 0 && (size_t)written < output_size;
}

static int split_target_path(const char *base_output, const char *split_source,
                             char *output, size_t output_size) {
    char directory[FPATCH_PATH_MAX];
    char filename[FPATCH_PATH_MAX];
    const char *basename = fpatch_path_basename(split_source);
    const char *extension = strrchr(basename, '.');
    size_t stem_size = extension && strcmp(extension, ".apk") == 0
        ? (size_t)(extension - basename) : strlen(basename);

    fpatch_path_dirname(base_output, directory, sizeof(directory));
    if (snprintf(filename, sizeof(filename), "%.*s-fpatch.apk",
                 (int)stem_size, basename) < 0) {
        return 0;
    }
    return fpatch_path_join(output, output_size, directory, filename);
}

static void cleanup_artifacts(OutputArtifact *artifacts, size_t count) {
    size_t i;
    for (i = 0; i < count; i++) {
        if (artifacts[i].raw[0]) {
            remove(artifacts[i].raw);
        }
        if (artifacts[i].ready[0]) {
            remove(artifacts[i].ready);
        }
    }
}

static int verify_output(const char *path, const char *package_name,
                         char *error, size_t error_size) {
    ApkInfo *info = (ApkInfo *)calloc(1, sizeof(*info));
    int valid = 0;
    if (!info) {
        snprintf(error, error_size, "Out of memory during output verification.");
        return 0;
    }
    if (fpatch_inspect_apk(path, info) && info->has_manifest && info->dex_count > 0 &&
        info->native_lib_count > 0 && info->has_falcon_bootstrap &&
        strcmp(info->manifest.package_name, package_name) == 0) {
        valid = 1;
    } else {
        snprintf(error, error_size,
                 "Patched APK failed structural verification before finalization.");
    }
    free(info);
    return valid;
}

int fpatch_inject_apk(const FpatchInjectProfile *requested_profile,
                      FpatchInjectResult *result,
                      char *error, size_t error_size) {
    FpatchInjectProfile *profile = NULL;
    FpatchArtifactBundle bundle;
    const FpatchArtifactBundle *external_bundle = NULL;
    ManifestInfo base_manifest;
    FpatchEmbeddedResource dex;
    unsigned char *payload = NULL;
    size_t payload_size = 0;
    FpatchArchivePatch archive_patch;
    OutputArtifact artifacts[FPATCH_MAX_OUTPUT_APKS];
    size_t artifact_count;
    char output_directory[FPATCH_PATH_MAX];
    char keystore[FPATCH_PATH_MAX];
    size_t i;
    int success = 0;

    memset(result, 0, sizeof(*result));
    fpatch_artifact_bundle_init(&bundle);
    memset(&base_manifest, 0, sizeof(base_manifest));
    memset(artifacts, 0, sizeof(artifacts));
    error[0] = '\0';
    profile = (FpatchInjectProfile *)malloc(sizeof(*profile));
    if (!profile) {
        snprintf(error, error_size, "Out of memory while preparing injection.");
        return 0;
    }
    *profile = *requested_profile;
    if (!fpatch_profile_validate(profile, error, error_size)) {
        goto done;
    }
    if (profile->artifacts[0]) {
        if (!fpatch_artifact_bundle_load(profile->artifacts, &bundle, error, error_size)) {
            goto done;
        }
        external_bundle = &bundle;
        snprintf(result->artifact_package, sizeof(result->artifact_package), "%s",
                 bundle.package);
    } else {
        snprintf(result->artifact_package, sizeof(result->artifact_package), "embedded");
    }
    if (!collect_apk_info(profile->source, &base_manifest,
                          result->target_abis, &result->target_abi_count,
                          error, error_size)) {
        goto done;
    }
    if (!base_manifest.package_name[0]) {
        snprintf(error, error_size, "The base APK package name could not be read.");
        goto done;
    }
    for (i = 0; i < profile->split_count; i++) {
        if (!collect_apk_info(profile->splits[i], NULL,
                              result->target_abis, &result->target_abi_count,
                              error, error_size)) {
            goto done;
        }
    }
    if (!validate_inputs(profile, result->target_abis, &result->target_abi_count,
                         error, error_size) ||
        !choose_default_abis(external_bundle, result->target_abis,
                             &result->target_abi_count,
                             error, error_size) ||
        !validate_runtime_abis(external_bundle, result->target_abis,
                               result->target_abi_count,
                               error, error_size)) {
        goto done;
    }

    dex = find_resource(external_bundle, "bootstrap-dex", profile->bootstrap_language);
    if (!dex.size) {
        if (external_bundle) {
            snprintf(error, error_size,
                     "Artifact package %s has no %s bootstrap DEX.",
                     external_bundle->package, profile->bootstrap_language);
        } else {
            snprintf(error, error_size,
                     "No embedded %s bootstrap DEX is available. Supply --artifacts or rebuild fpatch after scripts/build_android.ps1 -BootstrapOnly.",
                     profile->bootstrap_language);
        }
        goto done;
    }
    if (!profile->output[0] &&
        !fpatch_default_output_path(profile->source, profile->output, sizeof(profile->output))) {
        snprintf(error, error_size, "Cannot derive the output APK path.");
        goto done;
    }
    if (same_path(profile->source, profile->output)) {
        snprintf(error, error_size, "Output APK must not overwrite the source APK.");
        goto done;
    }
    fpatch_path_dirname(profile->output, output_directory, sizeof(output_directory));
    if (!fpatch_make_directories(output_directory)) {
        snprintf(error, error_size, "Cannot create output directory: %s", output_directory);
        goto done;
    }

    if (profile->random_libname) {
        if (!fpatch_random_alpha(result->runtime_library, 12)) {
            snprintf(error, error_size, "Cannot generate a runtime library name.");
            goto done;
        }
    } else {
        snprintf(result->runtime_library, sizeof(result->runtime_library), "falconpatch");
    }
    snprintf(result->bootstrap_language, sizeof(result->bootstrap_language), "%s",
             profile->bootstrap_language);
    if (!fpatch_build_payload(profile, &payload, &payload_size, error, error_size)) {
        goto done;
    }

    artifact_count = profile->split_count + 1;
    snprintf(artifacts[0].source, sizeof(artifacts[0].source), "%s", profile->source);
    snprintf(artifacts[0].target, sizeof(artifacts[0].target), "%s", profile->output);
    for (i = 0; i < profile->split_count; i++) {
        snprintf(artifacts[i + 1].source, sizeof(artifacts[i + 1].source), "%s",
                 profile->splits[i]);
        if (!split_target_path(profile->output, profile->splits[i],
                               artifacts[i + 1].target,
                               sizeof(artifacts[i + 1].target))) {
            snprintf(error, error_size, "Cannot derive a split APK output path.");
            goto done;
        }
    }
    for (i = 0; i < artifact_count; i++) {
        if (!make_temp_path(artifacts[i].target, "raw", artifacts[i].raw,
                            sizeof(artifacts[i].raw)) ||
            !make_temp_path(artifacts[i].target, "ready", artifacts[i].ready,
                            sizeof(artifacts[i].ready))) {
            snprintf(error, error_size, "Temporary output path is too long.");
            goto done;
        }
    }

    memset(&archive_patch, 0, sizeof(archive_patch));
    archive_patch.profile = profile;
    archive_patch.package_name = base_manifest.package_name;
    archive_patch.runtime_library = result->runtime_library;
    archive_patch.bootstrap_dex = dex.data;
    archive_patch.bootstrap_dex_size = dex.size;
    archive_patch.payload = payload;
    archive_patch.payload_size = payload_size;
    archive_patch.target_abi_count = result->target_abi_count;
    memcpy(archive_patch.target_abis, result->target_abis,
           sizeof(archive_patch.target_abis));
    for (i = 0; i < result->target_abi_count; i++) {
        FpatchEmbeddedResource runtime =
            find_resource(external_bundle, "runtime", result->target_abis[i]);
        snprintf(archive_patch.runtimes[i].abi,
                 sizeof(archive_patch.runtimes[i].abi), "%s",
                 result->target_abis[i]);
        archive_patch.runtimes[i].data = runtime.data;
        archive_patch.runtimes[i].size = runtime.size;
        archive_patch.runtime_count++;
    }
    if (!fpatch_patch_base_archive(artifacts[0].source, artifacts[0].raw,
                                   &archive_patch, result->strategy_used,
                                   sizeof(result->strategy_used),
                                   error, error_size)) {
        goto done;
    }
    for (i = 1; i < artifact_count; i++) {
        if (!fpatch_repack_split_archive(artifacts[i].source, artifacts[i].raw,
                                         error, error_size)) {
            goto done;
        }
    }
    if (!fpatch_prepare_keystore(profile, output_directory, keystore, sizeof(keystore),
                                 error, error_size)) {
        goto done;
    }
    for (i = 0; i < artifact_count; i++) {
        if (!fpatch_align_and_sign_apk(artifacts[i].raw, artifacts[i].ready,
                                       profile, keystore, error, error_size)) {
            goto done;
        }
    }
    if (!verify_output(artifacts[0].ready, base_manifest.package_name,
                       error, error_size)) {
        goto done;
    }
    for (i = 0; i < artifact_count; i++) {
        if (!fpatch_replace_file(artifacts[i].ready, artifacts[i].target,
                                 error, error_size)) {
            goto done;
        }
        artifacts[i].ready[0] = '\0';
        snprintf(result->output_paths[result->output_count], FPATCH_PATH_MAX,
                 "%s", artifacts[i].target);
        result->output_count++;
    }
    result->resigned = !profile->no_sign;
    success = 1;

done:
    cleanup_artifacts(artifacts, profile ? profile->split_count + 1 : 0);
    free(payload);
    fpatch_artifact_bundle_free(&bundle);
    free(profile);
    return success;
}
