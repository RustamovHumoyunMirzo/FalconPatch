#include "utils/apk_detacher.h"
#include "utils/apk_signer.h"
#include "utils/file_utils.h"
#include "utils/sha256.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zip.h>

#define FP_DEX_NO_INDEX 0xffffffffu

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

static uint16_t read_u16(const unsigned char *data) {
    return (uint16_t)data[0] | (uint16_t)((uint16_t)data[1] << 8);
}

static uint32_t read_u32(const unsigned char *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static void write_u32(unsigned char *data, uint32_t value) {
    data[0] = (unsigned char)value;
    data[1] = (unsigned char)(value >> 8);
    data[2] = (unsigned char)(value >> 16);
    data[3] = (unsigned char)(value >> 24);
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
                                 int *is_load_library) {
    uint16_t class_idx;
    uint32_t name_idx;
    char owner[128];
    char name[64];

    *is_load_library = 0;
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
    if (strcmp(owner, "Ljava/lang/System;") != 0) {
        return 0;
    }
    if (strcmp(name, "loadLibrary") == 0) {
        *is_load_library = 1;
        return 1;
    }
    return strcmp(name, "load") == 0;
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

static void dex_fix_header(unsigned char *data, size_t size) {
    unsigned char digest[32];
    uint32_t a = 1;
    uint32_t b = 0;
    size_t i;

    if (size < 32) {
        return;
    }
    memset(data + 8, 0, 24);
    fpatch_sha256(data + 32, size - 32, digest);
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
                                   uint32_t method_ids_off,
                                   uint32_t method_ids_size,
                                   const char *module_name,
                                   const char *library_name) {
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
            if ((op == 0x71u || op == 0x77u) && width == 3) {
                uint32_t method_index = insns[at + 1];
                int is_load_library = 0;
                uint16_t arg_register = 0;
                unsigned int arg_count = (insns[at] >> 8) & 0xffu;
                if (arg_count > 0 &&
                    method_is_system_load(data, size, string_ids_off, string_ids_size,
                                          type_ids_off, type_ids_size,
                                          method_ids_off, method_ids_size,
                                          method_index, &is_load_library)) {
                    if (op == 0x71u) {
                        arg_register = (uint16_t)(insns[at + 2] & 0x0fu);
                    } else {
                        arg_register = insns[at + 2];
                    }
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
                        }
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

size_t fpatch_repair_dex_load_calls(unsigned char *data, size_t size,
                                    const char *module_name,
                                    const char *library_name) {
    uint32_t string_ids_size;
    uint32_t string_ids_off;
    uint32_t type_ids_size;
    uint32_t type_ids_off;
    uint32_t method_ids_size;
    uint32_t method_ids_off;
    uint32_t class_defs_size;
    uint32_t class_defs_off;
    size_t repaired = 0;
    uint32_t i;

    if (size < 112 || memcmp(data, "dex\n", 4) != 0) {
        return 0;
    }
    string_ids_size = read_u32(data + 56);
    string_ids_off = read_u32(data + 60);
    type_ids_size = read_u32(data + 64);
    type_ids_off = read_u32(data + 68);
    method_ids_size = read_u32(data + 88);
    method_ids_off = read_u32(data + 92);
    class_defs_size = read_u32(data + 96);
    class_defs_off = read_u32(data + 100);
    if (class_defs_off + (size_t)class_defs_size * 32u > size) {
        return 0;
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
                                                 method_ids_off, method_ids_size,
                                                 module_name, library_name);
            }
        }
    }
    if (repaired) {
        dex_fix_header(data, size);
    }
    return repaired;
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
            size_t repaired;
            if (!read_entry(source, name, &dex, &dex_size, error, error_size)) {
                goto done;
            }
            repaired = fpatch_repair_dex_load_calls(dex, dex_size, module_name, library);
            result->repaired_load_calls += repaired;
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
