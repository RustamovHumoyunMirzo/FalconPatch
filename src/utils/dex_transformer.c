#include "utils/dex_transformer.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

typedef struct {
    unsigned char *data;
    size_t size;
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
} DexView;

typedef struct {
    uint32_t state[5];
    uint64_t bit_count;
    unsigned char block[64];
    size_t block_size;
} Sha1State;

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

static int checked_range(uint32_t offset, uint32_t count, size_t width,
                         size_t size) {
    return offset <= size && width <= size - offset &&
           count <= (size - offset) / width;
}

static int read_uleb128(const unsigned char *data, size_t size,
                        size_t *offset, uint32_t *value) {
    uint32_t result = 0;
    unsigned int shift;

    for (shift = 0; shift < 35; shift += 7) {
        unsigned char byte;
        if (*offset >= size) {
            return 0;
        }
        byte = data[(*offset)++];
        if (shift == 28 && (byte & 0xf0u) != 0) {
            return 0;
        }
        result |= (uint32_t)(byte & 0x7fu) << shift;
        if ((byte & 0x80u) == 0) {
            *value = result;
            return 1;
        }
    }
    return 0;
}

static int dex_string(const DexView *dex, uint32_t index,
                      const unsigned char **text, size_t *byte_length,
                      uint32_t *utf16_length) {
    uint32_t offset;
    size_t cursor;
    const unsigned char *end;

    if (index >= dex->string_ids_size) {
        return 0;
    }
    offset = read_u32(dex->data + dex->string_ids_off + index * 4u);
    cursor = offset;
    if (offset >= dex->size ||
        !read_uleb128(dex->data, dex->size, &cursor, utf16_length)) {
        return 0;
    }
    end = (const unsigned char *)memchr(dex->data + cursor, 0,
                                        dex->size - cursor);
    if (!end) {
        return 0;
    }
    *text = dex->data + cursor;
    *byte_length = (size_t)(end - *text);
    return 1;
}

static int type_descriptor(const DexView *dex, uint32_t type_index,
                           const unsigned char **text, size_t *length) {
    uint32_t ignored;
    uint32_t string_index;

    if (type_index >= dex->type_ids_size) {
        return 0;
    }
    string_index = read_u32(dex->data + dex->type_ids_off + type_index * 4u);
    return dex_string(dex, string_index, text, length, &ignored);
}

static int append_bytes(char *output, size_t output_size, size_t *used,
                        const unsigned char *value, size_t value_size) {
    if (value_size >= output_size - *used) {
        return 0;
    }
    memcpy(output + *used, value, value_size);
    *used += value_size;
    output[*used] = '\0';
    return 1;
}

static int method_signature(const DexView *dex, uint32_t proto_index,
                            char *output, size_t output_size) {
    uint32_t return_type;
    uint32_t parameters_off;
    const unsigned char *descriptor;
    size_t descriptor_size;
    size_t used = 0;
    uint32_t count;
    uint32_t i;

    if (proto_index >= dex->proto_ids_size || output_size < 4) {
        return 0;
    }
    return_type = read_u32(dex->data + dex->proto_ids_off + proto_index * 12u + 4u);
    parameters_off = read_u32(dex->data + dex->proto_ids_off + proto_index * 12u + 8u);
    output[used++] = '(';
    output[used] = '\0';
    if (parameters_off != 0) {
        if (!checked_range(parameters_off, 1, 4, dex->size) ||
            parameters_off > UINT32_MAX - 4u) {
            return 0;
        }
        count = read_u32(dex->data + parameters_off);
        if (!checked_range(parameters_off + 4u, count, 2, dex->size)) {
            return 0;
        }
        for (i = 0; i < count; i++) {
            uint16_t type_index = read_u16(dex->data + parameters_off + 4u + i * 2u);
            if (!type_descriptor(dex, type_index, &descriptor, &descriptor_size) ||
                !append_bytes(output, output_size, &used, descriptor, descriptor_size)) {
                return 0;
            }
        }
    }
    if (used + 1 >= output_size) {
        return 0;
    }
    output[used++] = ')';
    output[used] = '\0';
    if (!type_descriptor(dex, return_type, &descriptor, &descriptor_size) ||
        !append_bytes(output, output_size, &used, descriptor, descriptor_size)) {
        return 0;
    }
    return 1;
}

static int normalize_target(const char *target, char *descriptor, size_t size) {
    size_t input_size = strlen(target);
    size_t i;
    size_t used = 0;

    size_t prefix = target[0] == 'L' ? 0u : 1u;
    size_t suffix = input_size && target[input_size - 1] == ';' ? 0u : 1u;

    if (!input_size || input_size > SIZE_MAX - prefix - suffix - 1u ||
        input_size + prefix + suffix + 1u > size) {
        return 0;
    }
    if (target[0] != 'L') {
        descriptor[used++] = 'L';
    }
    for (i = 0; i < input_size; i++) {
        char value = target[i] == '.' ? '/' : target[i];
        descriptor[used++] = value;
    }
    if (descriptor[used - 1] != ';') {
        descriptor[used++] = ';';
    }
    descriptor[used] = '\0';
    return 1;
}

static int find_class(const DexView *dex, const char *target,
                      uint32_t *class_index, uint32_t *class_data_off) {
    char wanted[640];
    uint32_t i;

    if (!normalize_target(target, wanted, sizeof(wanted))) {
        return 0;
    }
    for (i = 0; i < dex->class_defs_size; i++) {
        const unsigned char *descriptor;
        size_t descriptor_size;
        uint32_t index = read_u32(dex->data + dex->class_defs_off + i * 32u);
        if (type_descriptor(dex, index, &descriptor, &descriptor_size) &&
            strlen(wanted) == descriptor_size &&
            memcmp(wanted, descriptor, descriptor_size) == 0) {
            *class_index = index;
            *class_data_off = read_u32(dex->data + dex->class_defs_off + i * 32u + 24u);
            return 1;
        }
    }
    return 0;
}

static int patch_code_item(DexView *dex, uint32_t code_off,
                           FpatchDexPatchAction action,
                           const char *return_type,
                           char *error, size_t error_size) {
    uint16_t registers_size;
    uint16_t tries_size;
    uint32_t insns_size;
    unsigned char *insns;
    size_t required = 2;

    if (!checked_range(code_off, 1, 16, dex->size)) {
        snprintf(error, error_size, "DEX method has an invalid code_item offset.");
        return 0;
    }
    registers_size = read_u16(dex->data + code_off);
    tries_size = read_u16(dex->data + code_off + 6u);
    insns_size = read_u32(dex->data + code_off + 12u);
    if (!checked_range(code_off + 16u, insns_size, 2, dex->size)) {
        snprintf(error, error_size, "DEX method has a truncated instruction stream.");
        return 0;
    }
    if (tries_size != 0) {
        snprintf(error, error_size,
                 "Constant-return patches do not support methods with try/catch blocks.");
        return 0;
    }
    if (action == FPATCH_DEX_PATCH_RETURN_VOID) {
        if (strcmp(return_type, "V") != 0) {
            snprintf(error, error_size, "return_void requires a void method.");
            return 0;
        }
        required = 1;
    } else if (action == FPATCH_DEX_PATCH_RETURN_TRUE ||
               action == FPATCH_DEX_PATCH_RETURN_FALSE) {
        if (strcmp(return_type, "Z") != 0) {
            snprintf(error, error_size, "Boolean return actions require a Z return type.");
            return 0;
        }
    } else if (action == FPATCH_DEX_PATCH_RETURN_NULL) {
        if (return_type[0] != 'L' && return_type[0] != '[') {
            snprintf(error, error_size, "return_null requires an object or array return type.");
            return 0;
        }
    } else if (action == FPATCH_DEX_PATCH_RETURN_ZERO) {
        if (!strchr("BCSIFJD", return_type[0]) || return_type[1] != '\0') {
            snprintf(error, error_size, "return_zero requires a numeric primitive return type.");
            return 0;
        }
        if (return_type[0] == 'J' || return_type[0] == 'D') {
            required = 3;
        }
    } else {
        snprintf(error, error_size, "Unsupported constant-return DEX action.");
        return 0;
    }
    if (insns_size < required ||
        (required > 1 && registers_size == 0) ||
        (required == 3 && registers_size < 2)) {
        snprintf(error, error_size, "DEX method is too small for the selected return action.");
        return 0;
    }
    insns = dex->data + code_off + 16u;
    memset(insns, 0, (size_t)insns_size * 2u);
    if (action == FPATCH_DEX_PATCH_RETURN_VOID) {
        insns[0] = 0x0e;
    } else if (action == FPATCH_DEX_PATCH_RETURN_ZERO &&
               (return_type[0] == 'J' || return_type[0] == 'D')) {
        insns[0] = 0x16;
        insns[4] = 0x10;
    } else {
        insns[0] = 0x12;
        if (action == FPATCH_DEX_PATCH_RETURN_TRUE) {
            insns[1] = 0x10;
        }
        insns[2] = (action == FPATCH_DEX_PATCH_RETURN_NULL) ? 0x11 : 0x0f;
    }
    return 1;
}

static int patch_method_list(DexView *dex, size_t *offset, uint32_t count,
                             uint32_t class_index, const char *method_name,
                             const char *signature, FpatchDexPatchAction action,
                             size_t *patched, char *error, size_t error_size) {
    uint32_t method_index = 0;
    uint32_t i;

    if (count > (dex->size - *offset) / 3u) {
        snprintf(error, error_size, "Encoded method count exceeds target class_data.");
        return 0;
    }
    for (i = 0; i < count; i++) {
        uint32_t method_diff;
        uint32_t access_flags;
        uint32_t code_off;
        const unsigned char *name;
        size_t name_size;
        uint32_t ignored;
        uint16_t owner;
        uint16_t proto;
        uint32_t name_index;
        char actual_signature[1024];

        if (!read_uleb128(dex->data, dex->size, offset, &method_diff) ||
            !read_uleb128(dex->data, dex->size, offset, &access_flags) ||
            !read_uleb128(dex->data, dex->size, offset, &code_off) ||
            UINT32_MAX - method_index < method_diff) {
            snprintf(error, error_size, "Malformed encoded_method list in target class.");
            return 0;
        }
        (void)access_flags;
        method_index += method_diff;
        if (method_index >= dex->method_ids_size) {
            snprintf(error, error_size, "Target class references an invalid method_id.");
            return 0;
        }
        owner = read_u16(dex->data + dex->method_ids_off + method_index * 8u);
        proto = read_u16(dex->data + dex->method_ids_off + method_index * 8u + 2u);
        name_index = read_u32(dex->data + dex->method_ids_off + method_index * 8u + 4u);
        if (!dex_string(dex, name_index, &name, &name_size, &ignored) ||
            !method_signature(dex, proto, actual_signature, sizeof(actual_signature))) {
            snprintf(error, error_size, "Target method metadata is malformed.");
            return 0;
        }
        if (owner == class_index && strlen(method_name) == name_size &&
            memcmp(method_name, name, name_size) == 0 &&
            strcmp(signature, actual_signature) == 0) {
            const char *return_type = strchr(actual_signature, ')');
            if (!return_type || code_off == 0) {
                snprintf(error, error_size, "Target method has no patchable implementation.");
                return 0;
            }
            if (!patch_code_item(dex, code_off, action, return_type + 1,
                                 error, error_size)) {
                return 0;
            }
            (*patched)++;
        }
    }
    return 1;
}

static int patch_method(DexView *dex, uint32_t class_index,
                        uint32_t class_data_off, const FpatchDexPatch *patch,
                        size_t *patched, char *error, size_t error_size) {
    const char *open = strchr(patch->method, '(');
    char method_name[512];
    size_t method_name_size;
    size_t offset = class_data_off;
    uint32_t static_fields;
    uint32_t instance_fields;
    uint32_t direct_methods;
    uint32_t virtual_methods;
    uint32_t ignored;
    uint32_t i;

    if (!open || open == patch->method) {
        snprintf(error, error_size, "Invalid method selector: %s", patch->method);
        return 0;
    }
    method_name_size = (size_t)(open - patch->method);
    if (method_name_size >= sizeof(method_name)) {
        snprintf(error, error_size, "DEX method name is too long.");
        return 0;
    }
    memcpy(method_name, patch->method, method_name_size);
    method_name[method_name_size] = '\0';
    if (class_data_off == 0 || class_data_off >= dex->size ||
        !read_uleb128(dex->data, dex->size, &offset, &static_fields) ||
        !read_uleb128(dex->data, dex->size, &offset, &instance_fields) ||
        !read_uleb128(dex->data, dex->size, &offset, &direct_methods) ||
        !read_uleb128(dex->data, dex->size, &offset, &virtual_methods)) {
        snprintf(error, error_size, "Target class has malformed class_data.");
        return 0;
    }
    if (static_fields > UINT32_MAX - instance_fields) {
        snprintf(error, error_size, "Target class field count overflows.");
        return 0;
    }
    if (static_fields + instance_fields > (dex->size - offset) / 2u ||
        direct_methods > UINT32_MAX - virtual_methods) {
        snprintf(error, error_size, "Target class member counts exceed class_data.");
        return 0;
    }
    for (i = 0; i < static_fields + instance_fields; i++) {
        if (!read_uleb128(dex->data, dex->size, &offset, &ignored) ||
            !read_uleb128(dex->data, dex->size, &offset, &ignored)) {
            snprintf(error, error_size, "Target class has malformed encoded fields.");
            return 0;
        }
    }
    if (!patch_method_list(dex, &offset, direct_methods, class_index,
                           method_name, open, patch->action, patched,
                           error, error_size) ||
        !patch_method_list(dex, &offset, virtual_methods, class_index,
                           method_name, open, patch->action, patched,
                           error, error_size)) {
        return 0;
    }
    return 1;
}

static int utf8_to_mutf8(const char *input, unsigned char **output,
                         size_t *output_size, uint32_t *utf16_size) {
    const unsigned char *bytes = (const unsigned char *)input;
    size_t input_size = strlen(input);
    size_t cursor = 0;
    size_t capacity;
    unsigned char *encoded;
    size_t used = 0;
    uint32_t units = 0;

    if (input_size > (SIZE_MAX - 1u) / 3u) {
        return 0;
    }
    capacity = input_size * 3u + 1u;
    encoded = (unsigned char *)malloc(capacity);
    if (!encoded) {
        return 0;
    }
    while (cursor < input_size) {
        uint32_t codepoint;
        size_t consumed;
        if (bytes[cursor] < 0x80u) {
            codepoint = bytes[cursor];
            consumed = 1;
        } else if (cursor + 1u < input_size &&
                   (bytes[cursor] & 0xe0u) == 0xc0u &&
                   (bytes[cursor + 1u] & 0xc0u) == 0x80u) {
            codepoint = ((uint32_t)(bytes[cursor] & 0x1fu) << 6) |
                        (uint32_t)(bytes[cursor + 1u] & 0x3fu);
            consumed = 2;
            if (codepoint < 0x80u) {
                free(encoded);
                return 0;
            }
        } else if (cursor + 2u < input_size &&
                   (bytes[cursor] & 0xf0u) == 0xe0u &&
                   (bytes[cursor + 1u] & 0xc0u) == 0x80u &&
                   (bytes[cursor + 2u] & 0xc0u) == 0x80u) {
            codepoint = ((uint32_t)(bytes[cursor] & 0x0fu) << 12) |
                        ((uint32_t)(bytes[cursor + 1u] & 0x3fu) << 6) |
                        (uint32_t)(bytes[cursor + 2u] & 0x3fu);
            consumed = 3;
            if (codepoint < 0x800u || (codepoint >= 0xd800u && codepoint <= 0xdfffu)) {
                free(encoded);
                return 0;
            }
        } else if (cursor + 3u < input_size &&
                   (bytes[cursor] & 0xf8u) == 0xf0u &&
                   (bytes[cursor + 1u] & 0xc0u) == 0x80u &&
                   (bytes[cursor + 2u] & 0xc0u) == 0x80u &&
                   (bytes[cursor + 3u] & 0xc0u) == 0x80u) {
            codepoint = ((uint32_t)(bytes[cursor] & 0x07u) << 18) |
                        ((uint32_t)(bytes[cursor + 1u] & 0x3fu) << 12) |
                        ((uint32_t)(bytes[cursor + 2u] & 0x3fu) << 6) |
                        (uint32_t)(bytes[cursor + 3u] & 0x3fu);
            consumed = 4;
            if (codepoint < 0x10000u || codepoint > 0x10ffffu) {
                free(encoded);
                return 0;
            }
        } else {
            free(encoded);
            return 0;
        }
        cursor += consumed;
        if (codepoint == 0) {
            encoded[used++] = 0xc0u;
            encoded[used++] = 0x80u;
            units++;
        } else if (codepoint <= 0x7fu) {
            encoded[used++] = (unsigned char)codepoint;
            units++;
        } else if (codepoint <= 0x7ffu) {
            encoded[used++] = (unsigned char)(0xc0u | (codepoint >> 6));
            encoded[used++] = (unsigned char)(0x80u | (codepoint & 0x3fu));
            units++;
        } else if (codepoint <= 0xffffu) {
            encoded[used++] = (unsigned char)(0xe0u | (codepoint >> 12));
            encoded[used++] = (unsigned char)(0x80u | ((codepoint >> 6) & 0x3fu));
            encoded[used++] = (unsigned char)(0x80u | (codepoint & 0x3fu));
            units++;
        } else {
            uint32_t value = codepoint - 0x10000u;
            uint16_t pair[2];
            size_t j;
            pair[0] = (uint16_t)(0xd800u + (value >> 10));
            pair[1] = (uint16_t)(0xdc00u + (value & 0x3ffu));
            for (j = 0; j < 2; j++) {
                encoded[used++] = (unsigned char)(0xe0u | (pair[j] >> 12));
                encoded[used++] = (unsigned char)(0x80u | ((pair[j] >> 6) & 0x3fu));
                encoded[used++] = (unsigned char)(0x80u | (pair[j] & 0x3fu));
            }
            units += 2;
        }
    }
    *output = encoded;
    *output_size = used;
    *utf16_size = units;
    return 1;
}

static int replace_string(DexView *dex, const FpatchDexPatch *patch,
                          size_t *replaced, char *error, size_t error_size) {
    unsigned char *from = NULL;
    unsigned char *to = NULL;
    size_t from_size = 0;
    size_t to_size = 0;
    uint32_t from_utf16 = 0;
    uint32_t to_utf16 = 0;
    uint32_t i;
    int result = 0;

    if (!utf8_to_mutf8(patch->string_from, &from, &from_size, &from_utf16) ||
        !utf8_to_mutf8(patch->string_to, &to, &to_size, &to_utf16)) {
        snprintf(error, error_size, "replace_string contains invalid UTF-8.");
        goto done;
    }
    if (from_size != to_size || from_utf16 != to_utf16) {
        snprintf(error, error_size,
                 "replace_string from/to values must have equal encoded and UTF-16 lengths.");
        goto done;
    }
    for (i = 0; i < dex->string_ids_size; i++) {
        const unsigned char *text;
        size_t text_size;
        uint32_t text_utf16;
        if (!dex_string(dex, i, &text, &text_size, &text_utf16)) {
            snprintf(error, error_size, "DEX contains malformed string_data.");
            goto done;
        }
        if (text_size == from_size && text_utf16 == from_utf16 &&
            memcmp(text, from, from_size) == 0) {
            memcpy((unsigned char *)text, to, to_size);
            (*replaced)++;
        }
    }
    result = 1;

done:
    free(to);
    free(from);
    return result;
}

static uint32_t rotate_left(uint32_t value, unsigned int bits) {
    return (value << bits) | (value >> (32u - bits));
}

static void sha1_transform(Sha1State *state, const unsigned char *block) {
    uint32_t words[80];
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;
    uint32_t e;
    uint32_t i;

    for (i = 0; i < 16; i++) {
        words[i] = ((uint32_t)block[i * 4u] << 24) |
                   ((uint32_t)block[i * 4u + 1u] << 16) |
                   ((uint32_t)block[i * 4u + 2u] << 8) |
                   (uint32_t)block[i * 4u + 3u];
    }
    for (i = 16; i < 80; i++) {
        words[i] = rotate_left(words[i - 3] ^ words[i - 8] ^
                               words[i - 14] ^ words[i - 16], 1);
    }
    a = state->state[0];
    b = state->state[1];
    c = state->state[2];
    d = state->state[3];
    e = state->state[4];
    for (i = 0; i < 80; i++) {
        uint32_t function;
        uint32_t constant;
        uint32_t temporary;
        if (i < 20) {
            function = (b & c) | ((~b) & d);
            constant = 0x5a827999u;
        } else if (i < 40) {
            function = b ^ c ^ d;
            constant = 0x6ed9eba1u;
        } else if (i < 60) {
            function = (b & c) | (b & d) | (c & d);
            constant = 0x8f1bbcdcu;
        } else {
            function = b ^ c ^ d;
            constant = 0xca62c1d6u;
        }
        temporary = rotate_left(a, 5) + function + e + constant + words[i];
        e = d;
        d = c;
        c = rotate_left(b, 30);
        b = a;
        a = temporary;
    }
    state->state[0] += a;
    state->state[1] += b;
    state->state[2] += c;
    state->state[3] += d;
    state->state[4] += e;
}

static void sha1_update(Sha1State *state, const unsigned char *data, size_t size) {
    state->bit_count += (uint64_t)size * 8u;
    while (size > 0) {
        size_t available = sizeof(state->block) - state->block_size;
        size_t take = size < available ? size : available;
        memcpy(state->block + state->block_size, data, take);
        state->block_size += take;
        data += take;
        size -= take;
        if (state->block_size == sizeof(state->block)) {
            sha1_transform(state, state->block);
            state->block_size = 0;
        }
    }
}

static void sha1_finish(Sha1State *state, unsigned char digest[20]) {
    uint64_t bits = state->bit_count;
    unsigned int i;

    state->block[state->block_size++] = 0x80u;
    if (state->block_size > 56) {
        while (state->block_size < 64) {
            state->block[state->block_size++] = 0;
        }
        sha1_transform(state, state->block);
        state->block_size = 0;
    }
    while (state->block_size < 56) {
        state->block[state->block_size++] = 0;
    }
    for (i = 0; i < 8; i++) {
        state->block[63u - i] = (unsigned char)(bits >> (i * 8u));
    }
    sha1_transform(state, state->block);
    for (i = 0; i < 5; i++) {
        digest[i * 4u] = (unsigned char)(state->state[i] >> 24);
        digest[i * 4u + 1u] = (unsigned char)(state->state[i] >> 16);
        digest[i * 4u + 2u] = (unsigned char)(state->state[i] >> 8);
        digest[i * 4u + 3u] = (unsigned char)state->state[i];
    }
}

static void fix_header(unsigned char *data, size_t size) {
    Sha1State sha1;
    unsigned char digest[20];
    uint32_t checksum;

    memset(&sha1, 0, sizeof(sha1));
    sha1.state[0] = 0x67452301u;
    sha1.state[1] = 0xefcdab89u;
    sha1.state[2] = 0x98badcfeu;
    sha1.state[3] = 0x10325476u;
    sha1.state[4] = 0xc3d2e1f0u;
    sha1_update(&sha1, data + 32, size - 32);
    sha1_finish(&sha1, digest);
    memcpy(data + 12, digest, sizeof(digest));
    checksum = (uint32_t)adler32(0L, Z_NULL, 0);
    checksum = (uint32_t)adler32(checksum, data + 12, (uInt)(size - 12));
    write_u32(data + 8, checksum);
}

static int init_view(DexView *dex, unsigned char *data, size_t size,
                     char *error, size_t error_size) {
    if (size < 112 || memcmp(data, "dex\n", 4) != 0 || data[7] != 0 ||
        read_u32(data + 32) != size || read_u32(data + 36) < 112 ||
        read_u32(data + 40) != 0x12345678u) {
        snprintf(error, error_size, "APK contains an unsupported or malformed DEX file.");
        return 0;
    }
    memset(dex, 0, sizeof(*dex));
    dex->data = data;
    dex->size = size;
    dex->string_ids_size = read_u32(data + 56);
    dex->string_ids_off = read_u32(data + 60);
    dex->type_ids_size = read_u32(data + 64);
    dex->type_ids_off = read_u32(data + 68);
    dex->proto_ids_size = read_u32(data + 72);
    dex->proto_ids_off = read_u32(data + 76);
    dex->method_ids_size = read_u32(data + 88);
    dex->method_ids_off = read_u32(data + 92);
    dex->class_defs_size = read_u32(data + 96);
    dex->class_defs_off = read_u32(data + 100);
    if (!checked_range(dex->string_ids_off, dex->string_ids_size, 4, size) ||
        !checked_range(dex->type_ids_off, dex->type_ids_size, 4, size) ||
        !checked_range(dex->proto_ids_off, dex->proto_ids_size, 12, size) ||
        !checked_range(dex->method_ids_off, dex->method_ids_size, 8, size) ||
        !checked_range(dex->class_defs_off, dex->class_defs_size, 32, size)) {
        snprintf(error, error_size, "DEX header table ranges are invalid.");
        return 0;
    }
    return 1;
}

int fpatch_transform_dex(unsigned char *data, size_t size,
                         const FpatchDexPatch *patches, size_t patch_count,
                         size_t *applied_counts,
                         FpatchDexTransformStats *stats,
                         char *error, size_t error_size) {
    DexView dex;
    FpatchDexTransformStats local_stats = {0};
    size_t changed = 0;
    size_t i;

    if (!data || !patches || !applied_counts ||
        !init_view(&dex, data, size, error, error_size)) {
        return 0;
    }
    for (i = 0; i < patch_count; i++) {
        uint32_t class_index;
        uint32_t class_data_off;
        size_t applied = 0;
        if (!find_class(&dex, patches[i].target, &class_index, &class_data_off)) {
            continue;
        }
        if (patches[i].action == FPATCH_DEX_PATCH_REPLACE_STRING) {
            if (!replace_string(&dex, &patches[i], &applied, error, error_size)) {
                return 0;
            }
            local_stats.strings_replaced += applied;
        } else {
            if (!patch_method(&dex, class_index, class_data_off, &patches[i],
                              &applied, error, error_size)) {
                return 0;
            }
            local_stats.methods_patched += applied;
        }
        applied_counts[i] += applied;
        changed += applied;
    }
    if (changed) {
        fix_header(data, size);
    }
    if (stats) {
        stats->methods_patched += local_stats.methods_patched;
        stats->strings_replaced += local_stats.strings_replaced;
    }
    return 1;
}
