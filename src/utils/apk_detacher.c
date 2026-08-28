#include "utils/apk_detacher.h"
#include "utils/apk_signer.h"
#include "utils/file_utils.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zip.h>

#define FP_DEX_NO_INDEX 0xffffffffu
#define FP_DEX_ACC_NATIVE 0x0100u
#define FPATCH_MAX_JNI_EXPORTS 512
#define FPATCH_MAX_NATIVE_STRINGS 1024

typedef struct {
    char symbols[FPATCH_MAX_JNI_EXPORTS][384];
    char strings[FPATCH_MAX_NATIVE_STRINGS][256];
    size_t count;
    size_t string_count;
    int has_register_natives;
} FpatchJniExportSet;

typedef struct {
    size_t load_calls;
    size_t native_calls;
    size_t skipped_native_calls;
} FpatchDexRepairStats;

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

static int jni_symbol_char(unsigned char value) {
    return isalnum(value) || value == '_';
}

static int jni_export_exists(const FpatchJniExportSet *exports,
                             const char *symbol) {
    size_t i;
    if (!symbol || symbol[0] == '\0') {
        return 0;
    }
    for (i = 0; i < exports->count; i++) {
        if (strcmp(exports->symbols[i], symbol) == 0) {
            return 1;
        }
    }
    return 0;
}

static void add_jni_export(FpatchJniExportSet *exports, const char *symbol) {
    size_t i;
    if (!symbol || strncmp(symbol, "Java_", 5) != 0) {
        return;
    }
    for (i = 0; i < exports->count; i++) {
        if (strcmp(exports->symbols[i], symbol) == 0) {
            return;
        }
    }
    if (exports->count < FPATCH_MAX_JNI_EXPORTS) {
        snprintf(exports->symbols[exports->count],
                 sizeof(exports->symbols[exports->count]), "%s", symbol);
        exports->count++;
    }
}

static int native_string_exists(const FpatchJniExportSet *exports,
                                const char *value) {
    size_t i;
    if (!exports || !value || !value[0]) {
        return 0;
    }
    for (i = 0; i < exports->string_count; i++) {
        if (strcmp(exports->strings[i], value) == 0) {
            return 1;
        }
    }
    return 0;
}

static void add_native_string(FpatchJniExportSet *exports,
                              const char *value) {
    size_t length;
    if (!exports || !value) {
        return;
    }
    length = strlen(value);
    if (length < 2 || length >= sizeof(exports->strings[0])) {
        return;
    }
    if (strcmp(value, "RegisterNatives") == 0 ||
        strcmp(value, "JNI RegisterNatives called with pending exception") == 0) {
        exports->has_register_natives = 1;
    }
    if (exports->string_count < FPATCH_MAX_NATIVE_STRINGS &&
        !native_string_exists(exports, value)) {
        snprintf(exports->strings[exports->string_count],
                 sizeof(exports->strings[exports->string_count]), "%s", value);
        exports->string_count++;
    }
}

static void collect_jni_exports(FpatchJniExportSet *exports,
                                const unsigned char *data, size_t size) {
    size_t i = 0;
    size_t printable_start = 0;
    size_t printable_size = 0;
    while (i + 5 < size) {
        char symbol[384];
        size_t j = 0;
        if (data[i] >= 32 && data[i] <= 126) {
            if (printable_size == 0) {
                printable_start = i;
            }
            printable_size++;
        } else {
            if (printable_size >= 2 && printable_size < sizeof(exports->strings[0])) {
                char value[256];
                memcpy(value, data + printable_start, printable_size);
                value[printable_size] = '\0';
                add_native_string(exports, value);
            }
            printable_size = 0;
        }
        if (i + 5 > size || memcmp(data + i, "Java_", 5) != 0) {
            i++;
            continue;
        }
        while (i < size && j + 1 < sizeof(symbol) &&
               jni_symbol_char(data[i])) {
            symbol[j++] = (char)data[i++];
        }
        symbol[j] = '\0';
        if (j > 5) {
            add_jni_export(exports, symbol);
        }
        if (j == 0) {
            i++;
        }
    }
    if (printable_size >= 2 && printable_size < sizeof(exports->strings[0])) {
        char value[256];
        memcpy(value, data + printable_start, printable_size);
        value[printable_size] = '\0';
        add_native_string(exports, value);
    }
}

static int jni_append_escaped(char *out, size_t out_size, size_t *used,
                              const char *value, int slash_as_separator) {
    size_t i;
    for (i = 0; value[i]; i++) {
        char chunk[8];
        char c = value[i];
        if (slash_as_separator && (c == '/' || c == '.')) {
            snprintf(chunk, sizeof(chunk), "_");
        } else if (c == '_') {
            snprintf(chunk, sizeof(chunk), "_1");
        } else if (c == ';') {
            snprintf(chunk, sizeof(chunk), "_2");
        } else if (c == '[') {
            snprintf(chunk, sizeof(chunk), "_3");
        } else if (isalnum((unsigned char)c)) {
            chunk[0] = c;
            chunk[1] = '\0';
        } else {
            snprintf(chunk, sizeof(chunk), "_0%04x", (unsigned int)(unsigned char)c);
        }
        if (*used + strlen(chunk) + 1 >= out_size) {
            return 0;
        }
        memcpy(out + *used, chunk, strlen(chunk) + 1);
        *used += strlen(chunk);
    }
    return 1;
}

static int jni_short_symbol(char *out, size_t out_size,
                            const char *owner_descriptor,
                            const char *method_name) {
    size_t used = 0;
    size_t owner_size;
    char owner[256];
    if (!out_size || !owner_descriptor || !method_name ||
        owner_descriptor[0] != 'L') {
        return 0;
    }
    owner_size = strlen(owner_descriptor);
    if (owner_size < 3 || owner_descriptor[owner_size - 1] != ';' ||
        owner_size - 2 >= sizeof(owner)) {
        return 0;
    }
    memcpy(owner, owner_descriptor + 1, owner_size - 2);
    owner[owner_size - 2] = '\0';
    snprintf(out, out_size, "Java_");
    used = strlen(out);
    return jni_append_escaped(out, out_size, &used, owner, 1) &&
           used + 2 < out_size &&
           (out[used++] = '_', out[used] = '\0',
            jni_append_escaped(out, out_size, &used, method_name, 0));
}

static uint16_t read_u16(const unsigned char *data) {
    return (uint16_t)data[0] | (uint16_t)((uint16_t)data[1] << 8);
}

static uint32_t read_u32(const unsigned char *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static int checked_range(uint32_t offset, uint32_t count,
                         size_t item_size, size_t size) {
    size_t start = (size_t)offset;
    size_t total;
    if (count == 0) {
        return 1;
    }
    if (start > size ||
        (item_size != 0u && (size_t)count > (SIZE_MAX / item_size))) {
        return 0;
    }
    total = (size_t)count * item_size;
    return total <= size - start;
}

static void write_u32(unsigned char *data, uint32_t value) {
    data[0] = (unsigned char)value;
    data[1] = (unsigned char)(value >> 8);
    data[2] = (unsigned char)(value >> 16);
    data[3] = (unsigned char)(value >> 24);
}

static uint32_t rol32(uint32_t value, unsigned int bits) {
    return (value << bits) | (value >> (32u - bits));
}

static void write_be32(unsigned char *data, uint32_t value) {
    data[0] = (unsigned char)(value >> 24);
    data[1] = (unsigned char)(value >> 16);
    data[2] = (unsigned char)(value >> 8);
    data[3] = (unsigned char)value;
}

static void sha1_digest(const unsigned char *data, size_t size,
                        unsigned char digest[20]) {
    uint32_t h0 = 0x67452301u;
    uint32_t h1 = 0xefcdab89u;
    uint32_t h2 = 0x98badcfeu;
    uint32_t h3 = 0x10325476u;
    uint32_t h4 = 0xc3d2e1f0u;
    uint64_t bit_size;
    size_t padded_size;
    unsigned char *message;
    size_t offset;

    if (size > (SIZE_MAX - 9u) || size > (UINT64_MAX / 8u)) {
        memset(digest, 0, 20);
        return;
    }
    bit_size = (uint64_t)size * 8u;
    padded_size = size + 1u + 8u;
    while (padded_size % 64u != 0) {
        if (padded_size == SIZE_MAX) {
            memset(digest, 0, 20);
            return;
        }
        padded_size++;
    }
    message = (unsigned char *)calloc(padded_size, 1);
    if (!message) {
        memset(digest, 0, 20);
        return;
    }
    memcpy(message, data, size);
    message[size] = 0x80u;
    for (offset = 0; offset < 8; offset++) {
        message[padded_size - 1u - offset] = (unsigned char)(bit_size >> (offset * 8u));
    }
    for (offset = 0; offset < padded_size; offset += 64u) {
        uint32_t w[80];
        uint32_t a;
        uint32_t b;
        uint32_t c;
        uint32_t d;
        uint32_t e;
        uint32_t i;
        for (i = 0; i < 16; i++) {
            const unsigned char *word = message + offset + i * 4u;
            w[i] = ((uint32_t)word[0] << 24) | ((uint32_t)word[1] << 16) |
                   ((uint32_t)word[2] << 8) | (uint32_t)word[3];
        }
        for (i = 16; i < 80; i++) {
            w[i] = rol32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        }
        a = h0;
        b = h1;
        c = h2;
        d = h3;
        e = h4;
        for (i = 0; i < 80; i++) {
            uint32_t f;
            uint32_t k;
            uint32_t temp;
            if (i < 20) {
                f = (b & c) | ((~b) & d);
                k = 0x5a827999u;
            } else if (i < 40) {
                f = b ^ c ^ d;
                k = 0x6ed9eba1u;
            } else if (i < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8f1bbcdcu;
            } else {
                f = b ^ c ^ d;
                k = 0xca62c1d6u;
            }
            temp = rol32(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = rol32(b, 30);
            b = a;
            a = temp;
        }
        h0 += a;
        h1 += b;
        h2 += c;
        h3 += d;
        h4 += e;
    }
    free(message);
    write_be32(digest, h0);
    write_be32(digest + 4, h1);
    write_be32(digest + 8, h2);
    write_be32(digest + 12, h3);
    write_be32(digest + 16, h4);
}

static int read_uleb128(const unsigned char *data, size_t size,
                        size_t *offset, uint32_t *value) {
    uint32_t result = 0;
    int shift = 0;
    int i;

    for (i = 0; i < 5; i++) {
        unsigned char byte;
        if (*offset >= size) {
            return 0;
        }
        byte = data[(*offset)++];
        result |= (uint32_t)(byte & 0x7fu) << shift;
        if ((byte & 0x80u) == 0) {
            *value = result;
            return 1;
        }
        shift += 7;
    }
    return 0;
}

static int get_dex_string(const unsigned char *data, size_t size,
                          uint32_t string_ids_off, uint32_t string_ids_size,
                          uint32_t index, char *out, size_t out_size) {
    size_t offset;
    size_t j = 0;
    uint32_t ignored_len;

    if (out_size == 0) {
        return 0;
    }
    out[0] = '\0';
    if (index >= string_ids_size ||
        string_ids_off + index * 4u + 4u > size) {
        return 0;
    }
    offset = read_u32(data + string_ids_off + index * 4u);
    if (offset >= size || !read_uleb128(data, size, &offset, &ignored_len)) {
        return 0;
    }
    while (offset < size && data[offset] != 0 && j + 1 < out_size) {
        out[j++] = (char)data[offset++];
    }
    out[j] = '\0';
    return 1;
}

static int get_dex_type_descriptor(const unsigned char *data, size_t size,
                                   uint32_t string_ids_off,
                                   uint32_t string_ids_size,
                                   uint32_t type_ids_off,
                                   uint32_t type_ids_size,
                                   uint32_t type_index,
                                   char *out, size_t out_size) {
    uint32_t descriptor_index;
    if (type_index >= type_ids_size ||
        type_ids_off + type_index * 4u + 4u > size) {
        if (out_size) {
            out[0] = '\0';
        }
        return 0;
    }
    descriptor_index = read_u32(data + type_ids_off + type_index * 4u);
    return get_dex_string(data, size, string_ids_off, string_ids_size,
                          descriptor_index, out, out_size);
}

static int method_is_system_load(const unsigned char *data, size_t size,
                                 uint32_t string_ids_off,
                                 uint32_t string_ids_size,
                                 uint32_t type_ids_off,
                                 uint32_t type_ids_size,
                                 uint32_t method_ids_off,
                                 uint32_t method_ids_size,
                                 uint32_t method_index,
                                 int *is_load_library,
                                 int *string_arg_position) {
    uint16_t class_idx;
    uint32_t name_idx;
    char owner[128];
    char name[64];

    *is_load_library = 0;
    *string_arg_position = 0;
    if (method_index >= method_ids_size ||
        method_ids_off + method_index * 8u + 8u > size) {
        return 0;
    }
    class_idx = read_u16(data + method_ids_off + method_index * 8u);
    name_idx = read_u32(data + method_ids_off + method_index * 8u + 4u);
    if (!get_dex_type_descriptor(data, size, string_ids_off, string_ids_size,
                                 type_ids_off, type_ids_size, class_idx,
                                 owner, sizeof(owner)) ||
        !get_dex_string(data, size, string_ids_off, string_ids_size,
                        name_idx, name, sizeof(name))) {
        return 0;
    }
    if (strcmp(owner, "Ljava/lang/System;") != 0 &&
        strcmp(owner, "Ljava/lang/Runtime;") != 0) {
        return 0;
    }
    if (strcmp(owner, "Ljava/lang/Runtime;") == 0) {
        *string_arg_position = 1;
    }
    if (strcmp(name, "loadLibrary") == 0) {
        *is_load_library = 1;
        return 1;
    }
    return strcmp(name, "load") == 0;
}

static int get_dex_method_info(const unsigned char *data, size_t size,
                               uint32_t string_ids_off,
                               uint32_t string_ids_size,
                               uint32_t type_ids_off,
                               uint32_t type_ids_size,
                               uint32_t proto_ids_off,
                               uint32_t proto_ids_size,
                               uint32_t method_ids_off,
                               uint32_t method_ids_size,
                               uint32_t method_index,
                               char *owner, size_t owner_size,
                               char *name, size_t name_size,
                               char *return_type, size_t return_type_size) {
    uint16_t class_idx;
    uint16_t proto_idx;
    uint32_t name_idx;
    uint32_t return_type_idx;

    if (owner_size) {
        owner[0] = '\0';
    }
    if (name_size) {
        name[0] = '\0';
    }
    if (return_type_size) {
        return_type[0] = '\0';
    }
    if (method_index >= method_ids_size ||
        method_ids_off + method_index * 8u + 8u > size) {
        return 0;
    }
    class_idx = read_u16(data + method_ids_off + method_index * 8u);
    proto_idx = read_u16(data + method_ids_off + method_index * 8u + 2u);
    name_idx = read_u32(data + method_ids_off + method_index * 8u + 4u);
    if (proto_idx >= proto_ids_size ||
        proto_ids_off + (uint32_t)proto_idx * 12u + 8u > size) {
        return 0;
    }
    return_type_idx = read_u32(data + proto_ids_off + (uint32_t)proto_idx * 12u + 4u);
    return get_dex_type_descriptor(data, size, string_ids_off, string_ids_size,
                                   type_ids_off, type_ids_size, class_idx,
                                   owner, owner_size) &&
           get_dex_string(data, size, string_ids_off, string_ids_size,
                          name_idx, name, name_size) &&
           get_dex_type_descriptor(data, size, string_ids_off, string_ids_size,
                                   type_ids_off, type_ids_size, return_type_idx,
                                   return_type, return_type_size);
}

static int get_dex_proto_signature(const unsigned char *data, size_t size,
                                   uint32_t string_ids_off,
                                   uint32_t string_ids_size,
                                   uint32_t type_ids_off,
                                   uint32_t type_ids_size,
                                   uint32_t proto_ids_off,
                                   uint32_t proto_ids_size,
                                   uint16_t proto_idx,
                                   char *out, size_t out_size) {
    uint32_t return_type_idx;
    uint32_t parameters_off;
    uint32_t parameter_count = 0;
    size_t used = 0;
    uint32_t i;

    if (!out_size || proto_idx >= proto_ids_size ||
        proto_ids_off + (uint32_t)proto_idx * 12u + 12u > size) {
        return 0;
    }
    out[0] = '\0';
    return_type_idx = read_u32(data + proto_ids_off + (uint32_t)proto_idx * 12u + 4u);
    parameters_off = read_u32(data + proto_ids_off + (uint32_t)proto_idx * 12u + 8u);
    if (used + 2 >= out_size) {
        return 0;
    }
    out[used++] = '(';
    out[used] = '\0';
    if (parameters_off) {
        if (parameters_off > UINT32_MAX - 4u ||
            !checked_range(parameters_off, 1, 4u, size)) {
            return 0;
        }
        parameter_count = read_u32(data + parameters_off);
        if (!checked_range(parameters_off + 4u, parameter_count, 2u, size)) {
            return 0;
        }
    }
    for (i = 0; i < parameter_count; i++) {
        char type[128];
        uint16_t type_idx = read_u16(data + parameters_off + 4u + i * 2u);
        size_t length;
        if (!get_dex_type_descriptor(data, size, string_ids_off, string_ids_size,
                                     type_ids_off, type_ids_size, type_idx,
                                     type, sizeof(type))) {
            return 0;
        }
        length = strlen(type);
        if (used + length + 2 >= out_size) {
            return 0;
        }
        memcpy(out + used, type, length + 1);
        used += length;
    }
    if (used + 2 >= out_size) {
        return 0;
    }
    out[used++] = ')';
    out[used] = '\0';
    {
        char return_type[128];
        size_t length;
        if (!get_dex_type_descriptor(data, size, string_ids_off, string_ids_size,
                                     type_ids_off, type_ids_size, return_type_idx,
                                     return_type, sizeof(return_type))) {
            return 0;
        }
        length = strlen(return_type);
        if (used + length + 1 >= out_size) {
            return 0;
        }
        memcpy(out + used, return_type, length + 1);
    }
    return 1;
}

static int get_dex_proto_parameter_signature(const unsigned char *data, size_t size,
                                             uint32_t string_ids_off,
                                             uint32_t string_ids_size,
                                             uint32_t type_ids_off,
                                             uint32_t type_ids_size,
                                             uint32_t proto_ids_off,
                                             uint32_t proto_ids_size,
                                             uint16_t proto_idx,
                                             char *out, size_t out_size) {
    char full[256];
    const char *start;
    const char *end;
    size_t length;
    if (!get_dex_proto_signature(data, size, string_ids_off, string_ids_size,
                                 type_ids_off, type_ids_size,
                                 proto_ids_off, proto_ids_size,
                                 proto_idx, full, sizeof(full))) {
        return 0;
    }
    start = strchr(full, '(');
    end = strchr(full, ')');
    if (!start || !end || end < start || !out_size) {
        return 0;
    }
    start++;
    length = (size_t)(end - start);
    if (length >= out_size) {
        return 0;
    }
    memcpy(out, start, length);
    out[length] = '\0';
    return 1;
}

static int owner_inner_name(const char *owner_descriptor,
                            char *out, size_t out_size) {
    size_t owner_size;
    if (!owner_descriptor || owner_descriptor[0] != 'L' || !out_size) {
        return 0;
    }
    owner_size = strlen(owner_descriptor);
    if (owner_size < 3 || owner_descriptor[owner_size - 1] != ';' ||
        owner_size - 2 >= out_size) {
        return 0;
    }
    memcpy(out, owner_descriptor + 1, owner_size - 2);
    out[owner_size - 2] = '\0';
    return 1;
}

static int jni_long_symbol(const unsigned char *data, size_t size,
                           uint32_t string_ids_off,
                           uint32_t string_ids_size,
                           uint32_t type_ids_off,
                           uint32_t type_ids_size,
                           uint32_t proto_ids_off,
                           uint32_t proto_ids_size,
                           uint16_t proto_idx,
                           char *out, size_t out_size,
                           const char *owner_descriptor,
                           const char *method_name) {
    char parameters[256];
    size_t used;
    if (!jni_short_symbol(out, out_size, owner_descriptor, method_name) ||
        !get_dex_proto_parameter_signature(data, size,
                                           string_ids_off, string_ids_size,
                                           type_ids_off, type_ids_size,
                                           proto_ids_off, proto_ids_size,
                                           proto_idx, parameters,
                                           sizeof(parameters))) {
        return 0;
    }
    used = strlen(out);
    if (used + 3 >= out_size) {
        return 0;
    }
    out[used++] = '_';
    out[used++] = '_';
    out[used] = '\0';
    return jni_append_escaped(out, out_size, &used, parameters, 1);
}

static void slash_to_dot(char *value) {
    size_t i;
    for (i = 0; value[i]; i++) {
        if (value[i] == '/') {
            value[i] = '.';
        }
    }
}

static int method_matches_register_natives(const unsigned char *data, size_t size,
                                           uint32_t string_ids_off,
                                           uint32_t string_ids_size,
                                           uint32_t type_ids_off,
                                           uint32_t type_ids_size,
                                           uint32_t proto_ids_off,
                                           uint32_t proto_ids_size,
                                           uint32_t method_ids_off,
                                           uint32_t method_ids_size,
                                           uint32_t method_index,
                                           const FpatchJniExportSet *exports) {
    uint16_t class_idx;
    uint16_t proto_idx;
    uint32_t name_idx;
    char owner[256];
    char owner_inner[256];
    char owner_dot[256];
    char name[128];
    char signature[256];

    if (!exports || !exports->has_register_natives ||
        method_index >= method_ids_size ||
        method_ids_off + method_index * 8u + 8u > size) {
        return 0;
    }
    class_idx = read_u16(data + method_ids_off + method_index * 8u);
    proto_idx = read_u16(data + method_ids_off + method_index * 8u + 2u);
    name_idx = read_u32(data + method_ids_off + method_index * 8u + 4u);
    if (!get_dex_type_descriptor(data, size, string_ids_off, string_ids_size,
                                 type_ids_off, type_ids_size, class_idx,
                                 owner, sizeof(owner)) ||
        !owner_inner_name(owner, owner_inner, sizeof(owner_inner)) ||
        !get_dex_string(data, size, string_ids_off, string_ids_size,
                        name_idx, name, sizeof(name)) ||
        !get_dex_proto_signature(data, size, string_ids_off, string_ids_size,
                                 type_ids_off, type_ids_size,
                                 proto_ids_off, proto_ids_size,
                                 proto_idx, signature, sizeof(signature))) {
        return 0;
    }
    snprintf(owner_dot, sizeof(owner_dot), "%s", owner_inner);
    slash_to_dot(owner_dot);
    return native_string_exists(exports, name) &&
           native_string_exists(exports, signature) &&
           (native_string_exists(exports, owner) ||
            native_string_exists(exports, owner_inner) ||
            native_string_exists(exports, owner_dot));
}

static int method_matches_jni_export(const unsigned char *data, size_t size,
                                     uint32_t string_ids_off,
                                     uint32_t string_ids_size,
                                     uint32_t type_ids_off,
                                     uint32_t type_ids_size,
                                     uint32_t proto_ids_off,
                                     uint32_t proto_ids_size,
                                     uint32_t method_ids_off,
                                     uint32_t method_ids_size,
                                     uint32_t method_index,
                                     const FpatchJniExportSet *exports) {
    char owner[256];
    char name[128];
    char return_type[64];
    char symbol[384];
    char long_symbol[384];
    uint16_t proto_idx;
    if (!exports || (exports->count == 0 && !exports->has_register_natives)) {
        return 0;
    }
    if (method_index >= method_ids_size ||
        method_ids_off + method_index * 8u + 4u > size) {
        return 0;
    }
    proto_idx = read_u16(data + method_ids_off + method_index * 8u + 2u);
    if (!get_dex_method_info(data, size, string_ids_off, string_ids_size,
                             type_ids_off, type_ids_size,
                             proto_ids_off, proto_ids_size,
                             method_ids_off, method_ids_size,
                             method_index, owner, sizeof(owner),
                             name, sizeof(name),
                             return_type, sizeof(return_type))) {
        return 0;
    }
    (void)return_type;
    return ((jni_short_symbol(symbol, sizeof(symbol), owner, name) &&
             jni_export_exists(exports, symbol)) ||
            (jni_long_symbol(data, size,
                             string_ids_off, string_ids_size,
                             type_ids_off, type_ids_size,
                             proto_ids_off, proto_ids_size,
                             proto_idx, long_symbol, sizeof(long_symbol),
                             owner, name) &&
             jni_export_exists(exports, long_symbol))) ||
           method_matches_register_natives(data, size,
                                           string_ids_off, string_ids_size,
                                           type_ids_off, type_ids_size,
                                           proto_ids_off, proto_ids_size,
                                           method_ids_off, method_ids_size,
                                           method_index, exports);
}

static int dex_entry_name(const char *name) {
    const char *cursor;
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
        cursor++;
    }
    return strcmp(cursor, ".dex") == 0;
}

static int dex_load_literal_matches(const char *literal,
                                    int is_load_library,
                                    const char *module_name,
                                    const char *library_name) {
    if (!literal || !literal[0]) {
        return 0;
    }
    if (is_load_library) {
        return strcmp(literal, module_name) == 0 ||
               strcmp(literal, library_name) == 0;
    }
    return strcmp(fpatch_path_basename(literal), library_name) == 0 ||
           ends_with_case(literal, library_name);
}

static int invoke_argument_register(const uint16_t *insns, size_t at,
                                    unsigned int op, unsigned int arg_count,
                                    unsigned int arg_position,
                                    uint16_t *reg) {
    if (arg_position >= arg_count) {
        return 0;
    }
    if (op >= 0x74u && op <= 0x78u) {
        *reg = (uint16_t)(insns[at + 2] + arg_position);
        return 1;
    }
    switch (arg_position) {
        case 0:
            *reg = (uint16_t)(insns[at + 2] & 0x0fu);
            return 1;
        case 1:
            *reg = (uint16_t)((insns[at + 2] >> 4) & 0x0fu);
            return 1;
        case 2:
            *reg = (uint16_t)((insns[at + 2] >> 8) & 0x0fu);
            return 1;
        case 3:
            *reg = (uint16_t)((insns[at + 2] >> 12) & 0x0fu);
            return 1;
        case 4:
            *reg = (uint16_t)((insns[at] >> 8) & 0x0fu);
            return 1;
        default:
            return 0;
    }
}

static size_t dex_payload_width(const uint16_t *insns, size_t at, size_t count) {
    uint16_t ident = insns[at];
    if (ident == 0x0100u && at + 4 <= count) {
        uint16_t size = insns[at + 1];
        return 4u + (size_t)size * 2u;
    }
    if (ident == 0x0200u && at + 2 <= count) {
        uint16_t size = insns[at + 1];
        return 2u + (size_t)size * 4u;
    }
    if (ident == 0x0300u && at + 4 <= count) {
        uint16_t element_width = insns[at + 1];
        uint32_t size = (uint32_t)insns[at + 2] |
                        ((uint32_t)insns[at + 3] << 16);
        size_t bytes = (size_t)element_width * (size_t)size;
        return 4u + (bytes + 1u) / 2u;
    }
    return 1;
}

static size_t dex_instruction_width(const uint16_t *insns, size_t at, size_t count) {
    unsigned int op = insns[at] & 0xffu;
    if (op == 0x00u) {
        return dex_payload_width(insns, at, count);
    }
    if (op == 0x14u || op == 0x17u || op == 0x1bu ||
        op == 0x03u || op == 0x06u || op == 0x09u ||
        op == 0x26u || op == 0x2au || op == 0x2bu ||
        op == 0x2cu || (op >= 0x6eu && op <= 0x72u) ||
        (op >= 0x74u && op <= 0x78u)) {
        return 3;
    }
    if (op == 0x18u) {
        return 5;
    }
    if (op == 0x02u || op == 0x05u || op == 0x08u ||
        op == 0x13u || op == 0x15u || op == 0x16u ||
        op == 0x19u || op == 0x1au || op == 0x1cu ||
        op == 0x1fu || op == 0x20u || op == 0x22u ||
        op == 0x23u || op == 0x29u ||
        (op >= 0x2du && op <= 0x3du) ||
        (op >= 0x44u && op <= 0x6du) ||
        (op >= 0x90u && op <= 0xafu) ||
        (op >= 0xd0u && op <= 0xe2u)) {
        return 2;
    }
    return 1;
}

static int instruction_dest_register(const uint16_t *insns, size_t at,
                                     size_t width, uint16_t *reg) {
    unsigned int op = insns[at] & 0xffu;
    unsigned int high = (insns[at] >> 8) & 0xffu;

    (void)width;
    if ((op >= 0x01u && op <= 0x09u) || op == 0x0bu ||
        (op >= 0x12u && op <= 0x1cu) || op == 0x1fu ||
        op == 0x21u || op == 0x22u ||
        (op >= 0x60u && op <= 0x6du) ||
        (op >= 0x7bu && op <= 0x8fu)) {
        *reg = (uint16_t)high;
        return 1;
    }
    if (op == 0x20u || op == 0x23u ||
        (op >= 0x2du && op <= 0x3du) ||
        (op >= 0x44u && op <= 0x5fu) ||
        (op >= 0x90u && op <= 0xafu) ||
        (op >= 0xd0u && op <= 0xe2u)) {
        *reg = (uint16_t)(high & 0x0fu);
        return 1;
    }
    return 0;
}

static int propagate_move_string(const uint16_t *insns, size_t at,
                                 size_t width, uint32_t *register_strings,
                                 uint16_t registers_size) {
    unsigned int op = insns[at] & 0xffu;
    unsigned int high = (insns[at] >> 8) & 0xffu;
    uint16_t dest;
    uint16_t source;

    if (op == 0x01u || op == 0x04u || op == 0x07u) {
        dest = (uint16_t)(high & 0x0fu);
        source = (uint16_t)(high >> 4);
    } else if ((op == 0x02u || op == 0x05u || op == 0x08u) && width >= 2) {
        dest = (uint16_t)high;
        source = insns[at + 1];
    } else if ((op == 0x03u || op == 0x06u || op == 0x09u) && width >= 3) {
        dest = insns[at + 1];
        source = insns[at + 2];
    } else {
        return 0;
    }
    if (dest < registers_size) {
        register_strings[dest] = source < registers_size
            ? register_strings[source] : FP_DEX_NO_INDEX;
    }
    return 1;
}

static int dex_return_is_wide(const char *return_type) {
    return return_type && (strcmp(return_type, "J") == 0 ||
                           strcmp(return_type, "D") == 0);
}

static int dex_return_is_void(const char *return_type) {
    return return_type && strcmp(return_type, "V") == 0;
}

static int repair_native_invoke_result(uint16_t *insns, size_t at,
                                       size_t width, size_t count,
                                       const char *return_type) {
    unsigned int next_op;
    unsigned int dest;

    if (dex_return_is_void(return_type)) {
        return 1;
    }
    if (at + width >= count) {
        return 1;
    }
    next_op = insns[at + width] & 0xffu;
    if (next_op != 0x0au && next_op != 0x0bu && next_op != 0x0cu) {
        return 1;
    }
    dest = (insns[at + width] >> 8) & 0xffu;
    if (dex_return_is_wide(return_type) || next_op == 0x0bu) {
        if (next_op != 0x0bu || dest > 255u) {
            return 0;
        }
        insns[at] = (uint16_t)(0x0016u | (dest << 8));
        insns[at + 1] = 0;
        insns[at + 2] = 0;
        insns[at + width] = 0;
        return 2;
    }
    if (dest > 15u) {
        return 0;
    }
    insns[at + width] = (uint16_t)(0x0012u | (dest << 8));
    return 1;
}

static int method_is_repairable_native(const unsigned char *data, size_t size,
                                       uint32_t string_ids_off,
                                       uint32_t string_ids_size,
                                       uint32_t type_ids_off,
                                       uint32_t type_ids_size,
                                       uint32_t proto_ids_off,
                                       uint32_t proto_ids_size,
                                       uint32_t method_ids_off,
                                       uint32_t method_ids_size,
                                       uint32_t method_index,
                                       const unsigned char *native_methods,
                                       char *return_type,
                                       size_t return_type_size) {
    char owner[256];
    char name[128];

    if (!native_methods || method_index >= method_ids_size ||
        !native_methods[method_index]) {
        return 0;
    }
    return get_dex_method_info(data, size, string_ids_off, string_ids_size,
                               type_ids_off, type_ids_size,
                               proto_ids_off, proto_ids_size,
                               method_ids_off, method_ids_size,
                               method_index, owner, sizeof(owner),
                               name, sizeof(name),
                               return_type, return_type_size);
}

static void dex_fix_header(unsigned char *data, size_t size) {
    unsigned char digest[20];
    uint32_t a = 1;
    uint32_t b = 0;
    size_t i;

    if (size < 32) {
        return;
    }
    memset(data + 8, 0, 24);
    sha1_digest(data + 32, size - 32, digest);
    memcpy(data + 12, digest, 20);
    for (i = 12; i < size; i++) {
        a = (a + data[i]) % 65521u;
        b = (b + a) % 65521u;
    }
    write_u32(data + 8, (b << 16) | a);
}

static size_t repair_dex_code_item(unsigned char *data, size_t size,
                                   uint32_t code_off,
                                   uint32_t string_ids_off,
                                   uint32_t string_ids_size,
                                   uint32_t type_ids_off,
                                   uint32_t type_ids_size,
                                   uint32_t proto_ids_off,
                                   uint32_t proto_ids_size,
                                   uint32_t method_ids_off,
                                   uint32_t method_ids_size,
                                   const char *module_name,
                                   const char *library_name,
                                   const unsigned char *native_methods,
                                   FpatchDexRepairStats *stats) {
    uint16_t registers_size;
    uint32_t insns_size;
    uint16_t *insns;
    uint32_t *register_strings = NULL;
    size_t at = 0;
    size_t repaired = 0;

    if (code_off + 16u > size) {
        return 0;
    }
    registers_size = read_u16(data + code_off);
    insns_size = read_u32(data + code_off + 12u);
    if (registers_size == 0 || registers_size > 65535u ||
        insns_size == 0 || code_off + 16u + (size_t)insns_size * 2u > size) {
        return 0;
    }
    register_strings = (uint32_t *)malloc((size_t)registers_size * sizeof(uint32_t));
    if (!register_strings) {
        return 0;
    }
    for (at = 0; at < registers_size; at++) {
        register_strings[at] = FP_DEX_NO_INDEX;
    }
    insns = (uint16_t *)(void *)(data + code_off + 16u);
    at = 0;
    while (at < insns_size) {
        size_t width = dex_instruction_width(insns, at, insns_size);
        unsigned int op = insns[at] & 0xffu;
        uint16_t dest;
        if (width == 0 || at + width > insns_size) {
            break;
        }
        if (op == 0x1au && width >= 2) {
            dest = (uint16_t)((insns[at] >> 8) & 0xffu);
            if (dest < registers_size) {
                register_strings[dest] = insns[at + 1];
            }
        } else if (op == 0x1bu && width >= 3) {
            dest = (uint16_t)((insns[at] >> 8) & 0xffu);
            if (dest < registers_size) {
                register_strings[dest] = (uint32_t)insns[at + 1] |
                                         ((uint32_t)insns[at + 2] << 16);
            }
        } else if (propagate_move_string(insns, at, width, register_strings,
                                         registers_size)) {
            /* Register string state was propagated above. */
        } else {
            if (((op >= 0x6eu && op <= 0x72u) ||
                 (op >= 0x74u && op <= 0x78u)) && width == 3) {
                uint32_t method_index = insns[at + 1];
                int is_load_library = 0;
                int string_arg_position = 0;
                uint16_t arg_register = 0;
                unsigned int arg_count = (insns[at] >> 8) & 0xffu;
                if (arg_count > 0 &&
                    method_is_system_load(data, size, string_ids_off, string_ids_size,
                                          type_ids_off, type_ids_size,
                                          method_ids_off, method_ids_size,
                                          method_index, &is_load_library,
                                          &string_arg_position) &&
                    invoke_argument_register(insns, at, op, arg_count,
                                             (unsigned int)string_arg_position,
                                             &arg_register)) {
                    if (arg_register < registers_size &&
                        register_strings[arg_register] != FP_DEX_NO_INDEX) {
                        char literal[512];
                        if (get_dex_string(data, size, string_ids_off,
                                           string_ids_size,
                                           register_strings[arg_register],
                                           literal, sizeof(literal)) &&
                            dex_load_literal_matches(literal, is_load_library,
                                                     module_name, library_name)) {
                            size_t i;
                            for (i = 0; i < width; i++) {
                                insns[at + i] = 0;
                            }
                            repaired++;
                            if (stats) {
                                stats->load_calls++;
                            }
                        }
                    }
                }
            }
            if (((op >= 0x6eu && op <= 0x72u) ||
                 (op >= 0x74u && op <= 0x78u)) && width == 3) {
                uint32_t method_index = insns[at + 1];
                char return_type[64];
                if (method_is_repairable_native(data, size,
                                                string_ids_off, string_ids_size,
                                                type_ids_off, type_ids_size,
                                                proto_ids_off, proto_ids_size,
                                                method_ids_off, method_ids_size,
                                                method_index, native_methods,
                                                return_type, sizeof(return_type))) {
                    int repair_kind = repair_native_invoke_result(insns, at, width,
                                                                  insns_size,
                                                                  return_type);
                    if (repair_kind) {
                        size_t i;
                        if (repair_kind == 1) {
                            for (i = 0; i < width; i++) {
                                insns[at + i] = 0;
                            }
                        }
                        repaired++;
                        if (stats) {
                            stats->native_calls++;
                        }
                    } else if (stats) {
                        stats->skipped_native_calls++;
                    }
                }
            }
            if (instruction_dest_register(insns, at, width, &dest) &&
                dest < registers_size) {
                register_strings[dest] = FP_DEX_NO_INDEX;
            }
        }
        at += width;
    }
    free(register_strings);
    return repaired;
}

static void mark_repairable_native_methods(const unsigned char *data, size_t size,
                                           uint32_t string_ids_off,
                                           uint32_t string_ids_size,
                                           uint32_t type_ids_off,
                                           uint32_t type_ids_size,
                                           uint32_t proto_ids_off,
                                           uint32_t proto_ids_size,
                                           uint32_t method_ids_off,
                                           uint32_t method_ids_size,
                                           uint32_t class_defs_off,
                                           uint32_t class_defs_size,
                                           const FpatchJniExportSet *exports,
                                           unsigned char *native_methods) {
    uint32_t i;
    (void)proto_ids_off;
    (void)proto_ids_size;
    if (!native_methods || !exports ||
        (exports->count == 0 && !exports->has_register_natives)) {
        return;
    }
    for (i = 0; i < class_defs_size; i++) {
        uint32_t class_data_off = read_u32(data + class_defs_off + i * 32u + 24u);
        size_t offset = class_data_off;
        uint32_t static_fields;
        uint32_t instance_fields;
        uint32_t direct_methods;
        uint32_t virtual_methods;
        uint32_t ignored;
        uint32_t method_index = 0;
        uint32_t item;
        if (class_data_off == 0 || class_data_off >= size ||
            !read_uleb128(data, size, &offset, &static_fields) ||
            !read_uleb128(data, size, &offset, &instance_fields) ||
            !read_uleb128(data, size, &offset, &direct_methods) ||
            !read_uleb128(data, size, &offset, &virtual_methods)) {
            continue;
        }
        for (item = 0; item < static_fields + instance_fields; item++) {
            if (!read_uleb128(data, size, &offset, &ignored) ||
                !read_uleb128(data, size, &offset, &ignored)) {
                break;
            }
        }
        if (item != static_fields + instance_fields) {
            continue;
        }
        for (item = 0; item < direct_methods + virtual_methods; item++) {
            uint32_t method_idx_diff;
            uint32_t access_flags;
            uint32_t code_off;
            if (!read_uleb128(data, size, &offset, &method_idx_diff) ||
                !read_uleb128(data, size, &offset, &access_flags) ||
                !read_uleb128(data, size, &offset, &code_off)) {
                break;
            }
            (void)code_off;
            method_index += method_idx_diff;
            if (method_index < method_ids_size &&
                (access_flags & FP_DEX_ACC_NATIVE) &&
                method_matches_jni_export(data, size,
                                          string_ids_off, string_ids_size,
                                          type_ids_off, type_ids_size,
                                          proto_ids_off, proto_ids_size,
                                          method_ids_off, method_ids_size,
                                          method_index, exports)) {
                native_methods[method_index] = 1;
            }
        }
    }
}

static FpatchDexRepairStats repair_dex(unsigned char *data, size_t size,
                                       const char *module_name,
                                       const char *library_name,
                                       const FpatchJniExportSet *exports) {
    uint32_t string_ids_size;
    uint32_t string_ids_off;
    uint32_t type_ids_size;
    uint32_t type_ids_off;
    uint32_t proto_ids_size;
    uint32_t proto_ids_off;
    uint32_t method_ids_size;
    uint32_t method_ids_off;
    uint32_t class_defs_size;
    uint32_t class_defs_off;
    unsigned char *native_methods = NULL;
    FpatchDexRepairStats stats;
    size_t repaired = 0;
    uint32_t i;

    memset(&stats, 0, sizeof(stats));
    if (size < 112 || memcmp(data, "dex\n", 4) != 0) {
        return stats;
    }
    string_ids_size = read_u32(data + 56);
    string_ids_off = read_u32(data + 60);
    type_ids_size = read_u32(data + 64);
    type_ids_off = read_u32(data + 68);
    proto_ids_size = read_u32(data + 72);
    proto_ids_off = read_u32(data + 76);
    method_ids_size = read_u32(data + 88);
    method_ids_off = read_u32(data + 92);
    class_defs_size = read_u32(data + 96);
    class_defs_off = read_u32(data + 100);
    if (!checked_range(class_defs_off, class_defs_size, 32u, size) ||
        !checked_range(method_ids_off, method_ids_size, 8u, size) ||
        !checked_range(proto_ids_off, proto_ids_size, 12u, size) ||
        !checked_range(string_ids_off, string_ids_size, 4u, size) ||
        !checked_range(type_ids_off, type_ids_size, 4u, size)) {
        return stats;
    }
    if (exports && method_ids_size > 0 &&
        (exports->count > 0 || exports->has_register_natives)) {
        native_methods = (unsigned char *)calloc(method_ids_size, 1);
        if (native_methods) {
            mark_repairable_native_methods(data, size,
                                           string_ids_off, string_ids_size,
                                           type_ids_off, type_ids_size,
                                           proto_ids_off, proto_ids_size,
                                           method_ids_off, method_ids_size,
                                           class_defs_off, class_defs_size,
                                           exports, native_methods);
        }
    }
    for (i = 0; i < class_defs_size; i++) {
        uint32_t class_data_off = read_u32(data + class_defs_off + i * 32u + 24u);
        size_t offset = class_data_off;
        uint32_t static_fields;
        uint32_t instance_fields;
        uint32_t direct_methods;
        uint32_t virtual_methods;
        uint32_t ignored;
        uint32_t method_index = 0;
        uint32_t method_group;
        if (class_data_off == 0 || class_data_off >= size ||
            !read_uleb128(data, size, &offset, &static_fields) ||
            !read_uleb128(data, size, &offset, &instance_fields) ||
            !read_uleb128(data, size, &offset, &direct_methods) ||
            !read_uleb128(data, size, &offset, &virtual_methods)) {
            continue;
        }
        for (method_group = 0; method_group < static_fields + instance_fields; method_group++) {
            if (!read_uleb128(data, size, &offset, &ignored) ||
                !read_uleb128(data, size, &offset, &ignored)) {
                break;
            }
        }
        if (method_group != static_fields + instance_fields) {
            continue;
        }
        for (method_group = 0; method_group < direct_methods + virtual_methods; method_group++) {
            uint32_t method_idx_diff;
            uint32_t access_flags;
            uint32_t code_off;
            if (!read_uleb128(data, size, &offset, &method_idx_diff) ||
                !read_uleb128(data, size, &offset, &access_flags) ||
                !read_uleb128(data, size, &offset, &code_off)) {
                break;
            }
            (void)access_flags;
            method_index += method_idx_diff;
            if (code_off != 0) {
                repaired += repair_dex_code_item(data, size, code_off,
                                                 string_ids_off, string_ids_size,
                                                 type_ids_off, type_ids_size,
                                                 proto_ids_off, proto_ids_size,
                                                 method_ids_off, method_ids_size,
                                                 module_name, library_name,
                                                 native_methods, &stats);
            }
        }
    }
    if (repaired) {
        dex_fix_header(data, size);
    }
    free(native_methods);
    return stats;
}

size_t fpatch_repair_dex_load_calls(unsigned char *data, size_t size,
                                    const char *module_name,
                                    const char *library_name) {
    FpatchDexRepairStats stats = repair_dex(data, size, module_name,
                                            library_name, NULL);
    return stats.load_calls;
}

size_t fpatch_repair_dex_jni_calls(unsigned char *data, size_t size,
                                   const char * const *jni_exports,
                                   size_t jni_export_count,
                                   size_t *skipped_calls) {
    FpatchJniExportSet exports;
    FpatchDexRepairStats stats;
    size_t i;
    memset(&exports, 0, sizeof(exports));
    for (i = 0; i < jni_export_count; i++) {
        add_jni_export(&exports, jni_exports[i]);
    }
    stats = repair_dex(data, size, "", "", &exports);
    if (skipped_calls) {
        *skipped_calls = stats.skipped_native_calls;
    }
    return stats.native_calls;
}

size_t fpatch_repair_dex_registered_jni_calls(unsigned char *data, size_t size,
                                              const char * const *native_strings,
                                              size_t native_string_count,
                                              size_t *skipped_calls) {
    FpatchJniExportSet exports;
    FpatchDexRepairStats stats;
    size_t i;
    memset(&exports, 0, sizeof(exports));
    exports.has_register_natives = 1;
    for (i = 0; i < native_string_count; i++) {
        add_native_string(&exports, native_strings[i]);
    }
    stats = repair_dex(data, size, "", "", &exports);
    if (skipped_calls) {
        *skipped_calls = stats.skipped_native_calls;
    }
    return stats.native_calls;
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

static int module_name_from_library(const char *library, char *output, size_t size) {
    const char *start = library;
    size_t length = strlen(library);
    if (strncmp(start, "lib", 3) == 0) {
        start += 3;
        length -= 3;
    }
    if (length > 3 && ends_with_case(start, ".so")) {
        length -= 3;
    }
    if (length == 0 || length >= size) {
        return 0;
    }
    memcpy(output, start, length);
    output[length] = '\0';
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

static int collect_removed_library_exports(zip_t *source,
                                           const FpatchDetachRequest *request,
                                           const char *library,
                                           FpatchJniExportSet *exports,
                                           char *error, size_t error_size) {
    zip_int64_t count = zip_get_num_entries(source, 0);
    zip_int64_t i;
    memset(exports, 0, sizeof(*exports));
    for (i = 0; i < count; i++) {
        const char *name = zip_get_name(source, (zip_uint64_t)i, ZIP_FL_UNCHANGED);
        char abi[32];
        const char *filename = NULL;
        if (!name ||
            !parse_native_entry(name, abi, sizeof(abi), &filename) ||
            strcmp(filename, library) != 0 ||
            !abi_allowed(request, abi)) {
            continue;
        }
        if (request->smart_repair) {
            unsigned char *data = NULL;
            size_t size = 0;
            if (!read_entry(source, name, &data, &size, error, error_size)) {
                return 0;
            }
            collect_jni_exports(exports, data, size);
            free(data);
        }
    }
    return 1;
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
    char module_name[128];
    char raw[FPATCH_PATH_MAX];
    char output_directory[FPATCH_PATH_MAX];
    char keystore[FPATCH_PATH_MAX];
    FpatchInjectProfile signing_profile;
    FpatchJniExportSet jni_exports;
    int success = 0;

    memset(result, 0, sizeof(*result));
    if (!request->target[0] || !request->output[0] || !request->library[0]) {
        snprintf(error, error_size, "detach requires --target, --so, and --out.");
        return 0;
    }
    if (!normalize_library_name(request->library, library, sizeof(library))) {
        snprintf(error, error_size, "Invalid --so library name.");
        return 0;
    }
    if (!module_name_from_library(library, module_name, sizeof(module_name))) {
        snprintf(error, error_size, "Invalid normalized library name.");
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
    if (!collect_removed_library_exports(source, request, library,
                                         &jni_exports, error, error_size)) {
        goto done;
    }
    result->jni_exports = jni_exports.count;
    result->detected_registered_jni = jni_exports.has_register_natives;
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
        if (request->smart_repair && dex_entry_name(name)) {
            unsigned char *dex = NULL;
            size_t dex_size = 0;
            FpatchDexRepairStats repair_stats;
            if (!read_entry(source, name, &dex, &dex_size, error, error_size)) {
                goto done;
            }
            repair_stats = repair_dex(dex, dex_size, module_name, library,
                                      &jni_exports);
            result->repaired_load_calls += repair_stats.load_calls;
            result->repaired_native_calls += repair_stats.native_calls;
            result->skipped_native_calls += repair_stats.skipped_native_calls;
            if (!add_buffer_entry(target, name, dex, dex_size,
                                  ZIP_CM_DEFLATE, error, error_size)) {
                free(dex);
                goto done;
            }
            free(dex);
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
