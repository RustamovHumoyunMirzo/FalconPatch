#include "utils/artifact_bundle.h"
#include "utils/sha256.h"

#include <ctype.h>
#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yaml.h>
#include <zlib.h>

#define TAR_BLOCK_SIZE 512u
#define MAX_EXPANDED_ARCHIVE_SIZE (256u * 1024u * 1024u)
#define MAX_METADATA_SIZE (1024u * 1024u)
#define METADATA_PATH "falconpatch-artifacts.json"

typedef struct {
    char path[FPATCH_ARTIFACT_PATH_MAX];
    const unsigned char *data;
    size_t size;
} TarEntry;

static void set_error(char *error, size_t error_size, const char *format, ...) {
    va_list arguments;
    if (!error || !error_size) {
        return;
    }
    va_start(arguments, format);
    vsnprintf(error, error_size, format, arguments);
    va_end(arguments);
}

static int copy_text(char *output, size_t output_size, const char *value,
                     const char *field, char *error, size_t error_size) {
    size_t length;
    if (!value) {
        set_error(error, error_size, "Artifact metadata field '%s' must be a string.", field);
        return 0;
    }
    length = strlen(value);
    if (!length || length >= output_size) {
        set_error(error, error_size, "Artifact metadata field '%s' is empty or too long.", field);
        return 0;
    }
    memcpy(output, value, length + 1);
    return 1;
}

static int inflate_tar(const char *path, unsigned char **data, size_t *size,
                       char *error, size_t error_size) {
    FILE *raw_file;
    unsigned char magic[2];
    gzFile file;
    unsigned char *buffer;
    size_t capacity = 64u * 1024u;
    size_t used = 0;
    int read_size;
    int zlib_error = Z_OK;

    *data = NULL;
    *size = 0;
    raw_file = fopen(path, "rb");
    if (!raw_file) {
        set_error(error, error_size, "Cannot open artifact bundle: %s", path);
        return 0;
    }
    if (fread(magic, 1, sizeof(magic), raw_file) != sizeof(magic) ||
        magic[0] != 0x1fu || magic[1] != 0x8bu) {
        fclose(raw_file);
        set_error(error, error_size, "Artifact bundle must be gzip-compressed tar data.");
        return 0;
    }
    fclose(raw_file);
    file = gzopen(path, "rb");
    if (!file) {
        set_error(error, error_size, "Cannot open artifact bundle: %s", path);
        return 0;
    }
    buffer = (unsigned char *)malloc(capacity);
    if (!buffer) {
        gzclose(file);
        set_error(error, error_size, "Out of memory while opening the artifact bundle.");
        return 0;
    }
    for (;;) {
        if (used == capacity) {
            unsigned char *grown;
            size_t next_capacity;
            if (capacity >= MAX_EXPANDED_ARCHIVE_SIZE) {
                free(buffer);
                gzclose(file);
                set_error(error, error_size,
                          "Artifact bundle expands beyond the %u MiB safety limit.",
                          MAX_EXPANDED_ARCHIVE_SIZE / (1024u * 1024u));
                return 0;
            }
            next_capacity = capacity * 2u;
            if (next_capacity > MAX_EXPANDED_ARCHIVE_SIZE) {
                next_capacity = MAX_EXPANDED_ARCHIVE_SIZE;
            }
            grown = (unsigned char *)realloc(buffer, next_capacity);
            if (!grown) {
                free(buffer);
                gzclose(file);
                set_error(error, error_size, "Out of memory while expanding the artifact bundle.");
                return 0;
            }
            buffer = grown;
            capacity = next_capacity;
        }
        read_size = gzread(file, buffer + used,
                           (unsigned int)((capacity - used) > INT_MAX
                               ? INT_MAX : (capacity - used)));
        if (read_size < 0) {
            const char *message = gzerror(file, &zlib_error);
            set_error(error, error_size, "Cannot decompress artifact bundle: %s",
                      message ? message : "invalid gzip data");
            free(buffer);
            gzclose(file);
            return 0;
        }
        if (read_size == 0) {
            break;
        }
        used += (size_t)read_size;
    }
    if (gzclose(file) != Z_OK) {
        free(buffer);
        set_error(error, error_size, "Cannot finish reading the artifact bundle.");
        return 0;
    }
    if (used < TAR_BLOCK_SIZE * 2u || used % TAR_BLOCK_SIZE != 0) {
        free(buffer);
        set_error(error, error_size, "Artifact bundle does not contain a complete tar archive.");
        return 0;
    }
    *data = buffer;
    *size = used;
    return 1;
}

static int block_is_zero(const unsigned char *block) {
    size_t i;
    for (i = 0; i < TAR_BLOCK_SIZE; i++) {
        if (block[i] != 0) {
            return 0;
        }
    }
    return 1;
}

static int parse_octal(const unsigned char *field, size_t field_size,
                       uint64_t *value) {
    size_t i = 0;
    uint64_t result = 0;
    int found = 0;

    while (i < field_size && (field[i] == ' ' || field[i] == '\0')) {
        i++;
    }
    for (; i < field_size && field[i] >= '0' && field[i] <= '7'; i++) {
        if (result > (UINT64_MAX - 7u) / 8u) {
            return 0;
        }
        result = result * 8u + (uint64_t)(field[i] - '0');
        found = 1;
    }
    while (i < field_size && (field[i] == ' ' || field[i] == '\0')) {
        i++;
    }
    if (!found || i != field_size) {
        return 0;
    }
    *value = result;
    return 1;
}

static size_t field_length(const unsigned char *field, size_t field_size) {
    size_t length = 0;
    while (length < field_size && field[length]) {
        length++;
    }
    return length;
}

static int tar_path(const unsigned char *header, char *path, size_t path_size) {
    size_t name_size = field_length(header, 100);
    size_t prefix_size = field_length(header + 345, 155);
    int written;

    if (!name_size) {
        return 0;
    }
    if (prefix_size) {
        written = snprintf(path, path_size, "%.*s/%.*s",
                           (int)prefix_size, (const char *)(header + 345),
                           (int)name_size, (const char *)header);
    } else {
        written = snprintf(path, path_size, "%.*s",
                           (int)name_size, (const char *)header);
    }
    return written >= 0 && (size_t)written < path_size;
}

static int safe_relative_path(const char *path, int directory) {
    const char *segment;
    const char *cursor;

    if (!path[0] || path[0] == '/' || path[0] == '\\' || strchr(path, '\\') ||
        strchr(path, ':')) {
        return 0;
    }
    segment = path;
    for (cursor = path;; cursor++) {
        if (*cursor == '/' || *cursor == '\0') {
            size_t length = (size_t)(cursor - segment);
            if (!length) {
                return directory && *cursor == '\0' && cursor > path && cursor[-1] == '/';
            }
            if ((length == 1 && segment[0] == '.') ||
                (length == 2 && segment[0] == '.' && segment[1] == '.')) {
                return 0;
            }
            if (*cursor == '\0') {
                return 1;
            }
            segment = cursor + 1;
        }
    }
}

static int validate_header_checksum(const unsigned char *header) {
    uint64_t expected;
    uint64_t actual = 0;
    size_t i;
    if (!parse_octal(header + 148, 8, &expected)) {
        return 0;
    }
    for (i = 0; i < TAR_BLOCK_SIZE; i++) {
        actual += (i >= 148 && i < 156) ? (unsigned char)' ' : header[i];
    }
    return actual == expected;
}

static int find_tar_entry(const TarEntry *entries, size_t count,
                          const char *path, const TarEntry **entry) {
    size_t i;
    for (i = 0; i < count; i++) {
        if (strcmp(entries[i].path, path) == 0) {
            *entry = &entries[i];
            return 1;
        }
    }
    return 0;
}

static int parse_tar(const unsigned char *data, size_t size,
                     TarEntry entries[FPATCH_MAX_ARTIFACT_FILES + 1],
                     size_t *entry_count, char *error, size_t error_size) {
    size_t offset = 0;
    int found_end = 0;

    *entry_count = 0;
    while (offset + TAR_BLOCK_SIZE <= size) {
        const unsigned char *header = data + offset;
        uint64_t file_size_64;
        size_t file_size;
        size_t padded_size;
        char path[FPATCH_ARTIFACT_PATH_MAX];
        char type;

        if (block_is_zero(header)) {
            size_t trailing;
            if (size - offset < TAR_BLOCK_SIZE * 2u) {
                set_error(error, error_size,
                          "Artifact bundle tar archive has an incomplete end marker.");
                return 0;
            }
            for (trailing = offset; trailing < size; trailing++) {
                if (data[trailing] != 0) {
                    set_error(error, error_size,
                              "Artifact bundle contains data after the tar end marker.");
                    return 0;
                }
            }
            found_end = 1;
            break;
        }
        if (!validate_header_checksum(header) ||
            !parse_octal(header + 124, 12, &file_size_64) ||
            file_size_64 > SIZE_MAX || !tar_path(header, path, sizeof(path))) {
            set_error(error, error_size, "Artifact bundle contains an invalid tar header.");
            return 0;
        }
        file_size = (size_t)file_size_64;
        if (file_size > SIZE_MAX - (TAR_BLOCK_SIZE - 1u)) {
            set_error(error, error_size, "Artifact bundle contains an oversized tar entry.");
            return 0;
        }
        padded_size = (file_size + TAR_BLOCK_SIZE - 1u) & ~(TAR_BLOCK_SIZE - 1u);
        if (padded_size > size - offset - TAR_BLOCK_SIZE) {
            set_error(error, error_size, "Artifact bundle contains a truncated tar entry.");
            return 0;
        }
        type = (char)header[156];
        if (type == '\0' || type == '0') {
            const TarEntry *duplicate = NULL;
            if (!safe_relative_path(path, 0)) {
                set_error(error, error_size, "Artifact bundle contains an unsafe path: %s", path);
                return 0;
            }
            if (*entry_count >= FPATCH_MAX_ARTIFACT_FILES + 1u) {
                set_error(error, error_size, "Artifact bundle contains too many files.");
                return 0;
            }
            if (find_tar_entry(entries, *entry_count, path, &duplicate)) {
                set_error(error, error_size, "Artifact bundle contains duplicate path: %s", path);
                return 0;
            }
            snprintf(entries[*entry_count].path, sizeof(entries[*entry_count].path), "%s", path);
            entries[*entry_count].data = data + offset + TAR_BLOCK_SIZE;
            entries[*entry_count].size = file_size;
            (*entry_count)++;
        } else if (type == '5') {
            if (!safe_relative_path(path, 1)) {
                set_error(error, error_size,
                          "Artifact bundle contains an unsafe directory path: %s", path);
                return 0;
            }
        } else if (type != 'x' && type != 'g') {
            set_error(error, error_size,
                      "Artifact bundle contains unsupported tar entry type '%c'.",
                      isprint((unsigned char)type) ? type : '?');
            return 0;
        }
        offset += TAR_BLOCK_SIZE + padded_size;
    }
    if (!found_end) {
        set_error(error, error_size, "Artifact bundle tar archive has no end marker.");
        return 0;
    }
    return 1;
}

static const char *scalar_value(yaml_node_t *node) {
    if (!node || node->type != YAML_SCALAR_NODE) {
        return NULL;
    }
    return (const char *)node->data.scalar.value;
}

static yaml_node_t *mapping_value(yaml_document_t *document, yaml_node_t *mapping,
                                  const char *key) {
    yaml_node_pair_t *pair;
    if (!mapping || mapping->type != YAML_MAPPING_NODE) {
        return NULL;
    }
    for (pair = mapping->data.mapping.pairs.start;
         pair < mapping->data.mapping.pairs.top; pair++) {
        const char *candidate = scalar_value(yaml_document_get_node(document, pair->key));
        if (candidate && strcmp(candidate, key) == 0) {
            return yaml_document_get_node(document, pair->value);
        }
    }
    return NULL;
}

static int mapping_keys_are_unique(yaml_document_t *document, yaml_node_t *mapping,
                                   char *error, size_t error_size) {
    yaml_node_pair_t *current;
    if (!mapping || mapping->type != YAML_MAPPING_NODE) {
        set_error(error, error_size, "Artifact metadata contains a non-object entry.");
        return 0;
    }
    for (current = mapping->data.mapping.pairs.start;
         current < mapping->data.mapping.pairs.top; current++) {
        yaml_node_pair_t *prior;
        const char *key = scalar_value(yaml_document_get_node(document, current->key));
        if (!key) {
            set_error(error, error_size, "Artifact metadata object keys must be strings.");
            return 0;
        }
        for (prior = mapping->data.mapping.pairs.start; prior < current; prior++) {
            const char *prior_key = scalar_value(yaml_document_get_node(document, prior->key));
            if (prior_key && strcmp(prior_key, key) == 0) {
                set_error(error, error_size, "Artifact metadata contains duplicate key: %s", key);
                return 0;
            }
        }
    }
    return 1;
}

static int parse_unsigned(const char *value, uint64_t *result) {
    uint64_t parsed = 0;
    size_t i;
    if (!value || !value[0]) {
        return 0;
    }
    for (i = 0; value[i]; i++) {
        unsigned int digit;
        if (!isdigit((unsigned char)value[i])) {
            return 0;
        }
        digit = (unsigned int)(value[i] - '0');
        if (parsed > (UINT64_MAX - digit) / 10u) {
            return 0;
        }
        parsed = parsed * 10u + digit;
    }
    *result = parsed;
    return 1;
}

static int load_required_scalar(yaml_document_t *document, yaml_node_t *mapping,
                                const char *key, char *output, size_t output_size,
                                char *error, size_t error_size) {
    return copy_text(output, output_size,
                     scalar_value(mapping_value(document, mapping, key)),
                     key, error, error_size);
}

static int valid_sha256(const char *value, char normalized[65]) {
    size_t i;
    if (!value || strlen(value) != 64) {
        return 0;
    }
    for (i = 0; i < 64; i++) {
        if (!isxdigit((unsigned char)value[i])) {
            return 0;
        }
        normalized[i] = (char)tolower((unsigned char)value[i]);
    }
    normalized[64] = '\0';
    return 1;
}

static int supported_abi(const char *name) {
    return strcmp(name, "armeabi-v7a") == 0 || strcmp(name, "arm64-v8a") == 0 ||
           strcmp(name, "x86") == 0 || strcmp(name, "x86_64") == 0;
}

static int valid_kind_and_name(const char *kind, const char *name) {
    if (strcmp(kind, "executable") == 0) {
        return strcmp(name, "fpatch") == 0;
    }
    if (strcmp(kind, "runtime") == 0) {
        return supported_abi(name);
    }
    if (strcmp(kind, "bootstrap-dex") == 0) {
        return strcmp(name, "java") == 0 || strcmp(name, "kotlin") == 0;
    }
    if (strcmp(kind, "license") == 0) {
        return strcmp(name, "FalconPatch") == 0;
    }
    return strcmp(kind, "sdk-header") == 0 && name[0] && !strchr(name, '/') &&
           !strchr(name, '\\');
}

static const char *kind_prefix(const char *kind) {
    if (strcmp(kind, "executable") == 0) {
        return "host/";
    }
    if (strcmp(kind, "runtime") == 0) {
        return "android/runtime/";
    }
    if (strcmp(kind, "bootstrap-dex") == 0) {
        return "android/bootstrap/";
    }
    if (strcmp(kind, "license") == 0) {
        return "";
    }
    return "android/sdk/include/";
}

static int starts_with(const char *value, const char *prefix) {
    return strncmp(value, prefix, strlen(prefix)) == 0;
}

static int metadata_file_is_unique(const FpatchArtifactBundle *bundle,
                                   const FpatchArtifactFile *candidate) {
    size_t i;
    for (i = 0; i < bundle->file_count; i++) {
        if (strcmp(bundle->files[i].path, candidate->path) == 0 ||
            (strcmp(bundle->files[i].kind, candidate->kind) == 0 &&
             strcmp(bundle->files[i].name, candidate->name) == 0)) {
            return 0;
        }
    }
    return 1;
}

static int parse_metadata(const TarEntry *entries, size_t entry_count,
                          const TarEntry *metadata, FpatchArtifactBundle *bundle,
                          char *error, size_t error_size) {
    yaml_parser_t parser;
    yaml_document_t document;
    yaml_node_t *root;
    yaml_node_t *host;
    yaml_node_t *files;
    yaml_node_item_t *item;
    uint64_t number;
    size_t executable_count = 0;
    size_t runtime_count = 0;
    size_t bootstrap_count = 0;
    int parser_ready = 0;
    int document_ready = 0;
    int success = 0;

    if (!metadata->size || metadata->size > MAX_METADATA_SIZE) {
        set_error(error, error_size, "Artifact metadata is empty or exceeds the 1 MiB limit.");
        return 0;
    }
    if (!yaml_parser_initialize(&parser)) {
        set_error(error, error_size, "Cannot initialize the artifact metadata parser.");
        return 0;
    }
    parser_ready = 1;
    yaml_parser_set_input_string(&parser, metadata->data, metadata->size);
    if (!yaml_parser_load(&parser, &document)) {
        set_error(error, error_size, "Artifact metadata JSON is invalid at line %lu: %s",
                  (unsigned long)parser.problem_mark.line + 1,
                  parser.problem ? parser.problem : "parse error");
        goto done;
    }
    document_ready = 1;
    root = yaml_document_get_root_node(&document);
    if (!mapping_keys_are_unique(&document, root, error, error_size)) {
        goto done;
    }
    if (!parse_unsigned(scalar_value(mapping_value(&document, root, "schema_version")),
                        &number) || number != FPATCH_ARTIFACT_SCHEMA_VERSION) {
        set_error(error, error_size, "Unsupported artifact schema_version; expected %u.",
                  FPATCH_ARTIFACT_SCHEMA_VERSION);
        goto done;
    }
    bundle->schema_version = (unsigned int)number;
    if (!parse_unsigned(scalar_value(mapping_value(&document, root, "runtime_api")),
                        &number) || number != FPATCH_RUNTIME_API_VERSION) {
        set_error(error, error_size, "Incompatible artifact runtime_api; expected %u.",
                  FPATCH_RUNTIME_API_VERSION);
        goto done;
    }
    bundle->runtime_api = (unsigned int)number;
    if (!load_required_scalar(&document, root, "falconpatch_version",
                              bundle->falconpatch_version,
                              sizeof(bundle->falconpatch_version), error, error_size) ||
        !load_required_scalar(&document, root, "package", bundle->package,
                              sizeof(bundle->package), error, error_size)) {
        goto done;
    }
    host = mapping_value(&document, root, "host");
    if (!mapping_keys_are_unique(&document, host, error, error_size) ||
        !load_required_scalar(&document, host, "platform", bundle->platform,
                              sizeof(bundle->platform), error, error_size) ||
        !load_required_scalar(&document, host, "arch", bundle->arch,
                              sizeof(bundle->arch), error, error_size)) {
        goto done;
    }
    if ((strcmp(bundle->platform, "windows") != 0 &&
         strcmp(bundle->platform, "linux") != 0 &&
         strcmp(bundle->platform, "macos") != 0) ||
        (strcmp(bundle->arch, "x86_64") != 0 && strcmp(bundle->arch, "arm64") != 0)) {
        set_error(error, error_size, "Artifact metadata has an unsupported host platform or arch.");
        goto done;
    }
    {
        char expected_package[128];
        int written = snprintf(expected_package, sizeof(expected_package), "%s-%s",
                               bundle->platform, bundle->arch);
        if (written < 0 || (size_t)written >= sizeof(expected_package) ||
            strcmp(bundle->package, expected_package) != 0) {
            set_error(error, error_size,
                      "Artifact package must match host metadata (%s).", expected_package);
            goto done;
        }
    }

    files = mapping_value(&document, root, "files");
    if (!files || files->type != YAML_SEQUENCE_NODE) {
        set_error(error, error_size, "Artifact metadata field 'files' must be an array.");
        goto done;
    }
    for (item = files->data.sequence.items.start;
         item < files->data.sequence.items.top; item++) {
        yaml_node_t *node = yaml_document_get_node(&document, *item);
        FpatchArtifactFile candidate;
        const TarEntry *tar_entry = NULL;
        char calculated[65];

        memset(&candidate, 0, sizeof(candidate));
        if (bundle->file_count >= FPATCH_MAX_ARTIFACT_FILES ||
            !mapping_keys_are_unique(&document, node, error, error_size) ||
            !load_required_scalar(&document, node, "kind", candidate.kind,
                                  sizeof(candidate.kind), error, error_size) ||
            !load_required_scalar(&document, node, "name", candidate.name,
                                  sizeof(candidate.name), error, error_size) ||
            !load_required_scalar(&document, node, "path", candidate.path,
                                  sizeof(candidate.path), error, error_size)) {
            if (bundle->file_count >= FPATCH_MAX_ARTIFACT_FILES) {
                set_error(error, error_size, "Artifact metadata declares too many files.");
            }
            goto done;
        }
        if (!valid_kind_and_name(candidate.kind, candidate.name) ||
            !safe_relative_path(candidate.path, 0) ||
            !starts_with(candidate.path, kind_prefix(candidate.kind)) ||
            (strcmp(candidate.kind, "license") == 0 &&
             strcmp(candidate.path, "LICENSE") != 0) ||
            strcmp(candidate.path, METADATA_PATH) == 0) {
            set_error(error, error_size, "Invalid artifact file declaration: %s/%s.",
                      candidate.kind, candidate.name);
            goto done;
        }
        if (!parse_unsigned(scalar_value(mapping_value(&document, node, "size")),
                            &number) || !number || number > SIZE_MAX) {
            set_error(error, error_size, "Artifact file '%s' has an invalid size.", candidate.path);
            goto done;
        }
        if (!valid_sha256(scalar_value(mapping_value(&document, node, "sha256")),
                          candidate.sha256)) {
            set_error(error, error_size, "Artifact file '%s' has an invalid SHA-256.", candidate.path);
            goto done;
        }
        if (!metadata_file_is_unique(bundle, &candidate)) {
            set_error(error, error_size, "Artifact metadata declares a duplicate file or resource.");
            goto done;
        }
        if (!find_tar_entry(entries, entry_count, candidate.path, &tar_entry)) {
            set_error(error, error_size, "Artifact bundle is missing declared file: %s", candidate.path);
            goto done;
        }
        if ((uint64_t)tar_entry->size != number) {
            set_error(error, error_size, "Artifact file size does not match metadata: %s", candidate.path);
            goto done;
        }
        fpatch_sha256_hex(tar_entry->data, tar_entry->size, calculated);
        if (strcmp(calculated, candidate.sha256) != 0) {
            set_error(error, error_size, "Artifact file checksum does not match metadata: %s", candidate.path);
            goto done;
        }
        candidate.data = tar_entry->data;
        candidate.size = tar_entry->size;
        bundle->files[bundle->file_count++] = candidate;
        if (strcmp(candidate.kind, "executable") == 0) {
            executable_count++;
        } else if (strcmp(candidate.kind, "runtime") == 0) {
            runtime_count++;
        } else if (strcmp(candidate.kind, "bootstrap-dex") == 0) {
            bootstrap_count++;
        }
    }
    if (executable_count != 1 || !runtime_count || !bootstrap_count) {
        set_error(error, error_size,
                  "Artifact bundle requires one host executable, an Android runtime, and a bootstrap DEX.");
        goto done;
    }
    {
        size_t i;
        for (i = 0; i < entry_count; i++) {
            size_t j;
            int declared = strcmp(entries[i].path, METADATA_PATH) == 0;
            for (j = 0; !declared && j < bundle->file_count; j++) {
                declared = strcmp(entries[i].path, bundle->files[j].path) == 0;
            }
            if (!declared) {
                set_error(error, error_size, "Artifact bundle contains undeclared file: %s",
                          entries[i].path);
                goto done;
            }
        }
    }
    success = 1;

done:
    if (document_ready) {
        yaml_document_delete(&document);
    }
    if (parser_ready) {
        yaml_parser_delete(&parser);
    }
    return success;
}

void fpatch_artifact_bundle_init(FpatchArtifactBundle *bundle) {
    if (bundle) {
        memset(bundle, 0, sizeof(*bundle));
    }
}

void fpatch_artifact_bundle_free(FpatchArtifactBundle *bundle) {
    if (!bundle) {
        return;
    }
    free(bundle->archive_data);
    memset(bundle, 0, sizeof(*bundle));
}

int fpatch_artifact_bundle_load(const char *path, FpatchArtifactBundle *bundle,
                                char *error, size_t error_size) {
    TarEntry entries[FPATCH_MAX_ARTIFACT_FILES + 1];
    const TarEntry *metadata = NULL;
    size_t entry_count = 0;
    int success = 0;

    if (!path || !path[0] || !bundle) {
        set_error(error, error_size, "An artifact bundle path is required.");
        return 0;
    }
    fpatch_artifact_bundle_init(bundle);
    if (error && error_size) {
        error[0] = '\0';
    }
    if (!inflate_tar(path, &bundle->archive_data, &bundle->archive_size,
                     error, error_size) ||
        !parse_tar(bundle->archive_data, bundle->archive_size, entries,
                   &entry_count, error, error_size) ||
        !find_tar_entry(entries, entry_count, METADATA_PATH, &metadata)) {
        if (error && error_size && !error[0]) {
            set_error(error, error_size,
                      "Artifact bundle is missing %s.", METADATA_PATH);
        }
        goto done;
    }
    if (!parse_metadata(entries, entry_count, metadata, bundle, error, error_size)) {
        goto done;
    }
    success = 1;

done:
    if (!success) {
        fpatch_artifact_bundle_free(bundle);
    }
    return success;
}

const FpatchArtifactFile *fpatch_artifact_bundle_find(
    const FpatchArtifactBundle *bundle, const char *kind, const char *name) {
    size_t i;
    if (!bundle || !kind || !name) {
        return NULL;
    }
    for (i = 0; i < bundle->file_count; i++) {
        if (strcmp(bundle->files[i].kind, kind) == 0 &&
            strcmp(bundle->files[i].name, name) == 0) {
            return &bundle->files[i];
        }
    }
    return NULL;
}
