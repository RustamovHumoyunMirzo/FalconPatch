#include "utils/artifact_bundle.h"
#include "utils/sha256.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

static int failures;

static void expect(int condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        failures++;
    }
}

static int write_entry(gzFile archive, const char *path,
                       const unsigned char *data, size_t size) {
    unsigned char header[512];
    unsigned char padding[512];
    unsigned long checksum = 0;
    size_t padding_size = (512u - (size % 512u)) % 512u;
    size_t i;

    if (strlen(path) >= 100 || size > 077777777777ull) {
        return 0;
    }
    memset(header, 0, sizeof(header));
    memset(padding, 0, sizeof(padding));
    memcpy(header, path, strlen(path));
    snprintf((char *)header + 100, 8, "%07o", 0644);
    snprintf((char *)header + 108, 8, "%07o", 0);
    snprintf((char *)header + 116, 8, "%07o", 0);
    snprintf((char *)header + 124, 12, "%011llo", (unsigned long long)size);
    snprintf((char *)header + 136, 12, "%011o", 0);
    memset(header + 148, ' ', 8);
    header[156] = '0';
    memcpy(header + 257, "ustar", 5);
    memcpy(header + 263, "00", 2);
    for (i = 0; i < sizeof(header); i++) {
        checksum += header[i];
    }
    snprintf((char *)header + 148, 8, "%06lo", checksum);
    header[154] = '\0';
    header[155] = ' ';
    return gzwrite(archive, header, sizeof(header)) == (int)sizeof(header) &&
           gzwrite(archive, data, (unsigned int)size) == (int)size &&
           (!padding_size ||
            gzwrite(archive, padding, (unsigned int)padding_size) == (int)padding_size);
}

static int write_bundle(const char *path, int valid_checksum) {
    static const unsigned char executable[] = "host executable";
    static const unsigned char runtime[] = "android runtime";
    static const unsigned char bootstrap[] = "bootstrap dex";
    unsigned char ending[1024];
    char executable_hash[65];
    char runtime_hash[65];
    char bootstrap_hash[65];
    char metadata[4096];
    int metadata_size;
    gzFile archive;
    int success;

    fpatch_sha256_hex(executable, sizeof(executable) - 1u, executable_hash);
    fpatch_sha256_hex(runtime, sizeof(runtime) - 1u, runtime_hash);
    fpatch_sha256_hex(bootstrap, sizeof(bootstrap) - 1u, bootstrap_hash);
    if (!valid_checksum) {
        memset(runtime_hash, '0', 64);
        runtime_hash[64] = '\0';
    }
    metadata_size = snprintf(
        metadata, sizeof(metadata),
        "{\"schema_version\":1,\"runtime_api\":1,"
        "\"falconpatch_version\":\"1.0.0\","
        "\"package\":\"windows-x86_64\","
        "\"host\":{\"platform\":\"windows\",\"arch\":\"x86_64\"},"
        "\"files\":["
        "{\"kind\":\"executable\",\"name\":\"fpatch\","
        "\"path\":\"host/fpatch.exe\",\"size\":%zu,\"sha256\":\"%s\"},"
        "{\"kind\":\"runtime\",\"name\":\"arm64-v8a\","
        "\"path\":\"android/runtime/arm64-v8a/libfalconpatch.so\","
        "\"size\":%zu,\"sha256\":\"%s\"},"
        "{\"kind\":\"bootstrap-dex\",\"name\":\"java\","
        "\"path\":\"android/bootstrap/java/classes.dex\","
        "\"size\":%zu,\"sha256\":\"%s\"}]}",
        sizeof(executable) - 1u, executable_hash,
        sizeof(runtime) - 1u, runtime_hash,
        sizeof(bootstrap) - 1u, bootstrap_hash);
    if (metadata_size < 0 || (size_t)metadata_size >= sizeof(metadata)) {
        return 0;
    }
    archive = gzopen(path, "wb9");
    if (!archive) {
        return 0;
    }
    memset(ending, 0, sizeof(ending));
    success = write_entry(archive, "falconpatch-artifacts.json",
                          (const unsigned char *)metadata, (size_t)metadata_size) &&
              write_entry(archive, "host/fpatch.exe", executable,
                          sizeof(executable) - 1u) &&
              write_entry(archive, "android/runtime/arm64-v8a/libfalconpatch.so",
                          runtime, sizeof(runtime) - 1u) &&
              write_entry(archive, "android/bootstrap/java/classes.dex",
                          bootstrap, sizeof(bootstrap) - 1u) &&
              gzwrite(archive, ending, sizeof(ending)) == (int)sizeof(ending);
    return gzclose(archive) == Z_OK && success;
}

int main(void) {
    static const char valid_path[] = "artifact-bundle-valid.tar.gz";
    static const char invalid_path[] = "artifact-bundle-invalid.tar.gz";
    static const char abc_hash[] =
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
    FpatchArtifactBundle bundle;
    const FpatchArtifactFile *runtime;
    char hash[65];
    char error[512] = "";

    fpatch_sha256_hex("abc", 3, hash);
    expect(strcmp(hash, abc_hash) == 0, "SHA-256 matches the standard abc vector");
    expect(write_bundle(valid_path, 1), "valid test bundle can be created");
    fpatch_artifact_bundle_init(&bundle);
    expect(fpatch_artifact_bundle_load(valid_path, &bundle, error, sizeof(error)),
           error[0] ? error : "valid artifact bundle loads");
    expect(strcmp(bundle.package, "windows-x86_64") == 0,
           "artifact package metadata is available");
    runtime = fpatch_artifact_bundle_find(&bundle, "runtime", "arm64-v8a");
    expect(runtime && runtime->size == strlen("android runtime"),
           "runtime is selected from metadata without ABI probing");
    fpatch_artifact_bundle_free(&bundle);

    expect(write_bundle(invalid_path, 0), "invalid test bundle can be created");
    error[0] = '\0';
    expect(!fpatch_artifact_bundle_load(invalid_path, &bundle, error, sizeof(error)),
           "checksum mismatch is rejected");
    expect(strstr(error, "checksum") != NULL, "checksum failure is explained");
    fpatch_artifact_bundle_free(&bundle);
    remove(valid_path);
    remove(invalid_path);

    if (failures) {
        fprintf(stderr, "%d artifact bundle test(s) failed.\n", failures);
        return 1;
    }
    puts("Artifact bundle tests passed.");
    return 0;
}
