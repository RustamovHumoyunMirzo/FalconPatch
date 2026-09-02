#include "utils/apk_inspector.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zip.h>

#define AXML_CHUNK_XML 0x00080003u
#define AXML_CHUNK_STRING_POOL 0x0001u
#define AXML_CHUNK_RESOURCE_MAP 0x0180u
#define AXML_CHUNK_START_ELEMENT 0x0102u

#define APK_SIG_V2 0x7109871au
#define APK_SIG_V3 0xf05368c0u
#define APK_SIG_V31 0x1b93ad61u
#define ACC_NATIVE 0x0100u

typedef struct {
    char **items;
    size_t count;
} StringPool;

typedef struct {
    uint32_t state[8];
    uint64_t bit_len;
    unsigned char data[64];
    size_t data_len;
} Sha256;

static int read_zip_entry(zip_t *apk, const char *name, unsigned char **data, size_t *size);

static uint16_t read_u16(const unsigned char *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t read_u32(const unsigned char *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t read_u64(const unsigned char *p) {
    return (uint64_t)read_u32(p) | ((uint64_t)read_u32(p + 4) << 32);
}

static uint32_t read_length8(const unsigned char **cursor, const unsigned char *end) {
    uint32_t len;

    if (*cursor >= end) {
        return 0;
    }

    len = **cursor;
    (*cursor)++;
    if ((len & 0x80u) != 0) {
        if (*cursor >= end) {
            return 0;
        }
        len = ((len & 0x7fu) << 8) | **cursor;
        (*cursor)++;
    }

    return len;
}

static uint32_t read_length16(const unsigned char **cursor, const unsigned char *end) {
    uint32_t len;

    if (*cursor + 2 > end) {
        return 0;
    }

    len = read_u16(*cursor);
    *cursor += 2;
    if ((len & 0x8000u) != 0) {
        if (*cursor + 2 > end) {
            return 0;
        }
        len = ((len & 0x7fffu) << 16) | read_u16(*cursor);
        *cursor += 2;
    }

    return len;
}

static void set_text(char *dest, size_t size, const char *src) {
    if (size == 0) {
        return;
    }
    if (!src) {
        dest[0] = '\0';
        return;
    }
    strncpy(dest, src, size - 1);
    dest[size - 1] = '\0';
}

static int ends_with(const char *value, const char *suffix) {
    size_t value_len = strlen(value);
    size_t suffix_len = strlen(suffix);
    return value_len >= suffix_len &&
           strcmp(value + value_len - suffix_len, suffix) == 0;
}

static int starts_with(const char *value, const char *prefix) {
    return strncmp(value, prefix, strlen(prefix)) == 0;
}

static int contains_text(const char *value, const char *needle) {
    return strstr(value, needle) != NULL;
}

static const char *compression_method_name(zip_uint16_t method) {
    switch (method) {
    case ZIP_CM_STORE:
        return "stored";
    case ZIP_CM_DEFLATE:
        return "deflated";
    default:
        return "other";
    }
}

static const char *elf_machine_name(uint16_t machine) {
    switch (machine) {
    case 3:
        return "x86";
    case 40:
        return "armeabi-v7a";
    case 62:
        return "x86_64";
    case 183:
        return "arm64-v8a";
    default:
        return "unknown";
    }
}

static void parse_elf_header(NativeLib *lib, const unsigned char *data, size_t size) {
    if (size < 20 || data[0] != 0x7f || data[1] != 'E' ||
        data[2] != 'L' || data[3] != 'F') {
        return;
    }

    lib->elf_valid = 1;
    lib->elf_class = data[4] == 2 ? 64 : data[4] == 1 ? 32 : 0;
    if (data[5] == 1) {
        lib->elf_machine = read_u16(data + 18);
    } else if (data[5] == 2) {
        lib->elf_machine = (uint16_t)data[19] | ((uint16_t)data[18] << 8);
    }
}

static void get_abi_from_lib_path(const char *entry_name, char *abi, size_t abi_size) {
    const char *start;
    const char *end;
    size_t len;

    if (abi_size == 0) {
        return;
    }
    abi[0] = '\0';

    if (!starts_with(entry_name, "lib/")) {
        return;
    }

    start = entry_name + 4;
    end = strchr(start, '/');
    if (!end) {
        return;
    }

    len = (size_t)(end - start);
    if (len == 0 || len >= abi_size) {
        return;
    }

    memcpy(abi, start, len);
    abi[len] = '\0';
}

static void normalize_application_class(ManifestInfo *manifest) {
    char normalized[256];

    if (!manifest->package_name[0] || !manifest->application_class[0]) {
        return;
    }

    if (manifest->application_class[0] == '.') {
        snprintf(normalized, sizeof(normalized), "%s%s",
                 manifest->package_name, manifest->application_class);
        set_text(manifest->application_class, sizeof(manifest->application_class), normalized);
    } else if (!strchr(manifest->application_class, '.')) {
        snprintf(normalized, sizeof(normalized), "%s.%s",
                 manifest->package_name, manifest->application_class);
        set_text(manifest->application_class, sizeof(manifest->application_class), normalized);
    }
}

static void add_abi(ApkInfo *info, const char *entry_name) {
    char abi[32];
    size_t i;

    get_abi_from_lib_path(entry_name, abi, sizeof(abi));
    if (!abi[0]) {
        return;
    }

    for (i = 0; i < info->abi_count; i++) {
        if (strcmp(info->abis[i], abi) == 0) {
            return;
        }
    }

    if (info->abi_count < 16) {
        set_text(info->abis[info->abi_count++], sizeof(info->abis[0]), abi);
    }
}

static void add_native_lib(ApkInfo *info, zip_t *apk, zip_uint64_t index, const char *entry_name) {
    NativeLib *lib;
    zip_stat_t st;
    unsigned char *data;
    size_t data_size;

    info->native_lib_count++;
    add_abi(info, entry_name);

    if (info->native_lib_count > MAX_NATIVE_LIBS) {
        return;
    }

    lib = &info->native_libs[info->native_lib_count - 1];
    memset(lib, 0, sizeof(*lib));
    set_text(lib->path, sizeof(lib->path), entry_name);
    get_abi_from_lib_path(entry_name, lib->abi, sizeof(lib->abi));

    zip_stat_init(&st);
    if (zip_stat_index(apk, index, 0, &st) == 0) {
        if (st.valid & ZIP_STAT_SIZE) {
            lib->size = (unsigned long long)st.size;
        }
        if (st.valid & ZIP_STAT_COMP_SIZE) {
            lib->compressed_size = (unsigned long long)st.comp_size;
        }
        if (st.valid & ZIP_STAT_COMP_METHOD) {
            lib->compression_method = st.comp_method;
        }
    }

    if (read_zip_entry(apk, entry_name, &data, &data_size)) {
        parse_elf_header(lib, data, data_size);
        free(data);
    }
}

static uint32_t rotr32(uint32_t value, uint32_t bits) {
    return (value >> bits) | (value << (32u - bits));
}

static void sha256_transform(Sha256 *ctx, const unsigned char data[64]) {
    static const uint32_t k[64] = {
        0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
        0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
        0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
        0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
        0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
        0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
        0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
        0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
        0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
        0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
        0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
        0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
        0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
        0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
        0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
        0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
    };
    uint32_t w[64];
    uint32_t a, b, c, d, e, f, g, h;
    size_t i;

    for (i = 0; i < 16; i++) {
        w[i] = ((uint32_t)data[i * 4] << 24) |
               ((uint32_t)data[i * 4 + 1] << 16) |
               ((uint32_t)data[i * 4 + 2] << 8) |
               (uint32_t)data[i * 4 + 3];
    }
    for (i = 16; i < 64; i++) {
        uint32_t s0 = rotr32(w[i - 15], 7) ^ rotr32(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = rotr32(w[i - 2], 17) ^ rotr32(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    a = ctx->state[0]; b = ctx->state[1]; c = ctx->state[2]; d = ctx->state[3];
    e = ctx->state[4]; f = ctx->state[5]; g = ctx->state[6]; h = ctx->state[7];

    for (i = 0; i < 64; i++) {
        uint32_t s1 = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t temp1 = h + s1 + ch + k[i] + w[i];
        uint32_t s0 = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = s0 + maj;
        h = g; g = f; f = e; e = d + temp1;
        d = c; c = b; b = a; a = temp1 + temp2;
    }

    ctx->state[0] += a; ctx->state[1] += b; ctx->state[2] += c; ctx->state[3] += d;
    ctx->state[4] += e; ctx->state[5] += f; ctx->state[6] += g; ctx->state[7] += h;
}

static void sha256_init(Sha256 *ctx) {
    ctx->data_len = 0;
    ctx->bit_len = 0;
    ctx->state[0] = 0x6a09e667u; ctx->state[1] = 0xbb67ae85u;
    ctx->state[2] = 0x3c6ef372u; ctx->state[3] = 0xa54ff53au;
    ctx->state[4] = 0x510e527fu; ctx->state[5] = 0x9b05688cu;
    ctx->state[6] = 0x1f83d9abu; ctx->state[7] = 0x5be0cd19u;
}

static void sha256_update(Sha256 *ctx, const unsigned char *data, size_t len) {
    size_t i;
    for (i = 0; i < len; i++) {
        ctx->data[ctx->data_len++] = data[i];
        if (ctx->data_len == 64) {
            sha256_transform(ctx, ctx->data);
            ctx->bit_len += 512;
            ctx->data_len = 0;
        }
    }
}

static void sha256_final(Sha256 *ctx, unsigned char hash[32]) {
    size_t i = ctx->data_len;
    size_t j;

    ctx->data[i++] = 0x80;
    if (i > 56) {
        while (i < 64) {
            ctx->data[i++] = 0;
        }
        sha256_transform(ctx, ctx->data);
        i = 0;
    }
    while (i < 56) {
        ctx->data[i++] = 0;
    }

    ctx->bit_len += (uint64_t)ctx->data_len * 8u;
    for (j = 0; j < 8; j++) {
        ctx->data[63 - j] = (unsigned char)(ctx->bit_len >> (j * 8));
    }
    sha256_transform(ctx, ctx->data);

    for (i = 0; i < 4; i++) {
        for (j = 0; j < 8; j++) {
            hash[j * 4 + i] = (unsigned char)(ctx->state[j] >> (24 - i * 8));
        }
    }
}

static void print_sha256(const unsigned char hash[32]) {
    size_t i;
    for (i = 0; i < 32; i++) {
        printf("%02X%s", hash[i], i + 1 == 32 ? "" : ":");
    }
}

static int read_zip_entry(zip_t *apk, const char *name, unsigned char **data, size_t *size) {
    zip_stat_t st;
    zip_file_t *file;
    zip_int64_t read_total;

    *data = NULL;
    *size = 0;

    if (zip_stat(apk, name, 0, &st) != 0 || st.size == 0) {
        return 0;
    }

    file = zip_fopen(apk, name, 0);
    if (!file) {
        return 0;
    }

    *data = (unsigned char *)malloc((size_t)st.size);
    if (!*data) {
        zip_fclose(file);
        return 0;
    }

    read_total = zip_fread(file, *data, st.size);
    zip_fclose(file);
    if (read_total < 0 || (zip_uint64_t)read_total != st.size) {
        free(*data);
        *data = NULL;
        return 0;
    }

    *size = (size_t)st.size;
    return 1;
}

static int read_uleb128(const unsigned char *data, size_t size, size_t *offset, uint32_t *value) {
    uint32_t result = 0;
    int shift = 0;

    for (int i = 0; i < 5; i++) {
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

    if (index >= string_ids_size || string_ids_off + index * 4u + 4u > size) {
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
                                   uint32_t string_ids_off, uint32_t string_ids_size,
                                   uint32_t type_ids_off, uint32_t type_ids_size,
                                   uint32_t type_index, char *out, size_t out_size) {
    uint32_t descriptor_index;

    if (type_index >= type_ids_size || type_ids_off + type_index * 4u + 4u > size) {
        if (out_size > 0) {
            out[0] = '\0';
        }
        return 0;
    }

    descriptor_index = read_u32(data + type_ids_off + type_index * 4u);
    return get_dex_string(data, size, string_ids_off, string_ids_size,
                          descriptor_index, out, out_size);
}

static void append_text(char *dest, size_t dest_size, const char *src) {
    size_t used;
    if (dest_size == 0 || !src) {
        return;
    }
    used = strlen(dest);
    if (used + 1 >= dest_size) {
        return;
    }
    strncat(dest, src, dest_size - used - 1);
}

static void format_descriptor(const char *descriptor, char *out, size_t out_size) {
    int arrays = 0;
    char base[192];
    size_t len;

    if (out_size == 0) {
        return;
    }
    out[0] = '\0';
    if (!descriptor || !descriptor[0]) {
        set_text(out, out_size, "unknown");
        return;
    }

    while (*descriptor == '[') {
        arrays++;
        descriptor++;
    }

    switch (*descriptor) {
    case 'V': set_text(base, sizeof(base), "void"); break;
    case 'Z': set_text(base, sizeof(base), "boolean"); break;
    case 'B': set_text(base, sizeof(base), "byte"); break;
    case 'C': set_text(base, sizeof(base), "char"); break;
    case 'S': set_text(base, sizeof(base), "short"); break;
    case 'I': set_text(base, sizeof(base), "int"); break;
    case 'J': set_text(base, sizeof(base), "long"); break;
    case 'F': set_text(base, sizeof(base), "float"); break;
    case 'D': set_text(base, sizeof(base), "double"); break;
    case 'L':
        len = strlen(descriptor);
        if (len > 2 && descriptor[len - 1] == ';') {
            size_t copy_len = len - 2;
            if (copy_len >= sizeof(base)) {
                copy_len = sizeof(base) - 1;
            }
            memcpy(base, descriptor + 1, copy_len);
            base[copy_len] = '\0';
            for (size_t i = 0; base[i]; i++) {
                if (base[i] == '/') {
                    base[i] = '.';
                }
            }
        } else {
            set_text(base, sizeof(base), descriptor);
        }
        break;
    default:
        set_text(base, sizeof(base), descriptor);
        break;
    }

    set_text(out, out_size, base);
    for (int i = 0; i < arrays; i++) {
        append_text(out, out_size, "[]");
    }
}

static void format_dex_type(const unsigned char *data, size_t size,
                            uint32_t string_ids_off, uint32_t string_ids_size,
                            uint32_t type_ids_off, uint32_t type_ids_size,
                            uint32_t type_index, char *out, size_t out_size) {
    char descriptor[256];

    if (get_dex_type_descriptor(data, size, string_ids_off, string_ids_size,
                                type_ids_off, type_ids_size, type_index,
                                descriptor, sizeof(descriptor))) {
        format_descriptor(descriptor, out, out_size);
    } else if (out_size > 0) {
        set_text(out, out_size, "unknown");
    }
}

static void get_method_ref(const unsigned char *data, size_t size,
                           uint32_t string_ids_off, uint32_t string_ids_size,
                           uint32_t type_ids_off, uint32_t type_ids_size,
                           uint32_t proto_ids_off, uint32_t proto_ids_size,
                           uint32_t method_ids_off, uint32_t method_ids_size,
                           uint32_t method_index, char *class_out, size_t class_size,
                           char *name_out, size_t name_size,
                           char *params_out, size_t params_size,
                           char *return_out, size_t return_size) {
    uint16_t class_idx;
    uint16_t proto_idx;
    uint32_t name_idx;
    uint32_t return_type_idx;
    uint32_t parameters_off;

    if (class_size) class_out[0] = '\0';
    if (name_size) name_out[0] = '\0';
    if (params_size) params_out[0] = '\0';
    if (return_size) return_out[0] = '\0';

    if (method_index >= method_ids_size || method_ids_off + method_index * 8u + 8u > size) {
        return;
    }

    class_idx = read_u16(data + method_ids_off + method_index * 8u);
    proto_idx = read_u16(data + method_ids_off + method_index * 8u + 2u);
    name_idx = read_u32(data + method_ids_off + method_index * 8u + 4u);

    format_dex_type(data, size, string_ids_off, string_ids_size,
                    type_ids_off, type_ids_size, class_idx, class_out, class_size);
    get_dex_string(data, size, string_ids_off, string_ids_size,
                   name_idx, name_out, name_size);

    if (proto_idx >= proto_ids_size || proto_ids_off + proto_idx * 12u + 12u > size) {
        set_text(params_out, params_size, "unknown");
        set_text(return_out, return_size, "unknown");
        return;
    }

    return_type_idx = read_u32(data + proto_ids_off + proto_idx * 12u + 4u);
    parameters_off = read_u32(data + proto_ids_off + proto_idx * 12u + 8u);
    format_dex_type(data, size, string_ids_off, string_ids_size,
                    type_ids_off, type_ids_size, return_type_idx, return_out, return_size);

    if (parameters_off == 0) {
        set_text(params_out, params_size, "none");
    } else if (parameters_off + 4u <= size) {
        uint32_t param_count = read_u32(data + parameters_off);
        size_t pos = parameters_off + 4u;
        params_out[0] = '\0';
        for (uint32_t i = 0; i < param_count && pos + 2u <= size; i++, pos += 2u) {
            char type_name[128];
            format_dex_type(data, size, string_ids_off, string_ids_size,
                            type_ids_off, type_ids_size, read_u16(data + pos),
                            type_name, sizeof(type_name));
            if (i > 0) {
                append_text(params_out, params_size, ", ");
            }
            append_text(params_out, params_size, type_name);
        }
        if (!params_out[0]) {
            set_text(params_out, params_size, "none");
        }
    } else {
        set_text(params_out, params_size, "unknown");
    }
}

static void add_native_method(ApkInfo *info, const char *dex_name,
                              const char *class_name, const char *method_name,
                              const char *params, const char *return_type) {
    NativeMethod *method;

    info->native_method_count++;
    if (info->native_method_count > MAX_NATIVE_METHODS) {
        return;
    }

    method = &info->native_methods[info->native_method_count - 1];
    set_text(method->dex_file, sizeof(method->dex_file), dex_name);
    set_text(method->class_name, sizeof(method->class_name), class_name);
    set_text(method->method_name, sizeof(method->method_name), method_name);
    set_text(method->params, sizeof(method->params), params);
    set_text(method->return_type, sizeof(method->return_type), return_type);
}

static void add_load_call(ApkInfo *info, const char *dex_name,
                          const char *class_name, const char *method_name,
                          const char *api, const char *argument) {
    NativeLoadCall *call;

    info->load_call_count++;
    if (info->load_call_count > MAX_LOAD_CALLS) {
        return;
    }

    call = &info->load_calls[info->load_call_count - 1];
    set_text(call->dex_file, sizeof(call->dex_file), dex_name);
    set_text(call->class_name, sizeof(call->class_name), class_name);
    set_text(call->method_name, sizeof(call->method_name), method_name);
    set_text(call->api, sizeof(call->api), api);
    set_text(call->argument, sizeof(call->argument), argument);
}

static int is_system_load_call(const char *class_name, const char *method_name,
                               const char *params, const char *return_type,
                               char *api_out, size_t api_size,
                               int *string_arg_position) {
    if ((strcmp(class_name, "java.lang.System") != 0 &&
         strcmp(class_name, "java.lang.Runtime") != 0) ||
        strcmp(params, "java.lang.String") != 0 ||
        strcmp(return_type, "void") != 0) {
        return 0;
    }

    *string_arg_position = strcmp(class_name, "java.lang.Runtime") == 0 ? 1 : 0;
    if (strcmp(method_name, "loadLibrary") == 0) {
        set_text(api_out, api_size, "loadLibrary");
        return 1;
    }
    if (strcmp(method_name, "load") == 0) {
        set_text(api_out, api_size, "load");
        return 1;
    }

    return 0;
}

static int invoke_argument_register(const unsigned char *data, size_t insns_off,
                                    uint32_t pc, uint8_t op, uint8_t arg_count,
                                    unsigned int arg_position, uint16_t *reg) {
    uint16_t regs;
    if (arg_position >= arg_count) {
        return 0;
    }
    regs = read_u16(data + insns_off + (pc + 2u) * 2u);
    if (op >= 0x74u && op <= 0x78u) {
        *reg = (uint16_t)(regs + arg_position);
        return 1;
    }
    switch (arg_position) {
        case 0:
            *reg = (uint16_t)(regs & 0x0fu);
            return 1;
        case 1:
            *reg = (uint16_t)((regs >> 4) & 0x0fu);
            return 1;
        case 2:
            *reg = (uint16_t)((regs >> 8) & 0x0fu);
            return 1;
        case 3:
            *reg = (uint16_t)((regs >> 12) & 0x0fu);
            return 1;
        case 4:
            *reg = (uint16_t)((read_u16(data + insns_off + pc * 2u) >> 8) & 0x0fu);
            return 1;
        default:
            return 0;
    }
}

static void scan_code_for_load_calls(ApkInfo *info, const char *dex_name,
                                     const unsigned char *data, size_t size,
                                     uint32_t string_ids_off, uint32_t string_ids_size,
                                     uint32_t type_ids_off, uint32_t type_ids_size,
                                     uint32_t proto_ids_off, uint32_t proto_ids_size,
                                     uint32_t method_ids_off, uint32_t method_ids_size,
                                     const char *owner_class, const char *owner_method,
                                     uint32_t code_off) {
    uint16_t registers_size;
    uint32_t insns_size;
    size_t insns_off;
    char reg_strings[256][256];

    if (code_off == 0 || code_off + 16u > size) {
        return;
    }

    registers_size = read_u16(data + code_off);
    insns_size = read_u32(data + code_off + 12u);
    insns_off = code_off + 16u;
    if (registers_size > 256 || insns_off + (size_t)insns_size * 2u > size) {
        return;
    }

    memset(reg_strings, 0, sizeof(reg_strings));

    for (uint32_t pc = 0; pc < insns_size;) {
        uint16_t insn = read_u16(data + insns_off + pc * 2u);
        uint8_t op = (uint8_t)(insn & 0xffu);
        uint8_t aa = (uint8_t)(insn >> 8);
        uint32_t advance = 1;

        if (op == 0x1au && pc + 1u < insns_size) {
            uint16_t string_idx = read_u16(data + insns_off + (pc + 1u) * 2u);
            if (aa < registers_size) {
                get_dex_string(data, size, string_ids_off, string_ids_size,
                               string_idx, reg_strings[aa], sizeof(reg_strings[aa]));
            }
            advance = 2;
        } else if (op == 0x1bu && pc + 2u < insns_size) {
            uint32_t string_idx = read_u32(data + insns_off + (pc + 1u) * 2u);
            if (aa < registers_size) {
                get_dex_string(data, size, string_ids_off, string_ids_size,
                               string_idx, reg_strings[aa], sizeof(reg_strings[aa]));
            }
            advance = 3;
        } else if (((op >= 0x6eu && op <= 0x72u) ||
                    (op >= 0x74u && op <= 0x78u)) &&
                   pc + 2u < insns_size) {
            uint16_t method_idx = read_u16(data + insns_off + (pc + 1u) * 2u);
            uint8_t arg_count = (uint8_t)((insn >> 8) & 0x0fu);
            char class_name[256];
            char method_name[128];
            char params[512];
            char return_type[128];
            char api[16];
            int string_arg_position = 0;
            uint16_t arg_register = 0;

            if (op >= 0x74u && op <= 0x78u) {
                arg_count = (uint8_t)(insn >> 8);
            }

            get_method_ref(data, size, string_ids_off, string_ids_size,
                           type_ids_off, type_ids_size, proto_ids_off, proto_ids_size,
                           method_ids_off, method_ids_size, method_idx,
                           class_name, sizeof(class_name), method_name, sizeof(method_name),
                           params, sizeof(params), return_type, sizeof(return_type));

            if (is_system_load_call(class_name, method_name, params, return_type,
                                    api, sizeof(api), &string_arg_position) &&
                invoke_argument_register(data, insns_off, pc, op, arg_count,
                                         (unsigned int)string_arg_position,
                                         &arg_register) &&
                arg_register < registers_size) {
                add_load_call(info, dex_name, owner_class, owner_method, api,
                              reg_strings[arg_register][0] ? reg_strings[arg_register] : "dynamic/unknown");
            }
            advance = 3;
        } else if (op == 0x77u && pc + 2u < insns_size) {
            uint16_t method_idx = read_u16(data + insns_off + (pc + 1u) * 2u);
            uint16_t first_reg = read_u16(data + insns_off + (pc + 2u) * 2u);
            uint8_t arg_count = (uint8_t)(insn >> 8);
            char class_name[256];
            char method_name[128];
            char params[512];
            char return_type[128];
            char api[16];
            int string_arg_position = 0;

            get_method_ref(data, size, string_ids_off, string_ids_size,
                           type_ids_off, type_ids_size, proto_ids_off, proto_ids_size,
                           method_ids_off, method_ids_size, method_idx,
                           class_name, sizeof(class_name), method_name, sizeof(method_name),
                           params, sizeof(params), return_type, sizeof(return_type));

            if (arg_count > 0 &&
                is_system_load_call(class_name, method_name, params, return_type,
                                    api, sizeof(api), &string_arg_position) &&
                string_arg_position < arg_count &&
                first_reg + (uint16_t)string_arg_position < registers_size) {
                add_load_call(info, dex_name, owner_class, owner_method, api,
                              reg_strings[first_reg + string_arg_position][0] ?
                              reg_strings[first_reg + string_arg_position] : "dynamic/unknown");
            }
            advance = 3;
        }

        pc += advance;
    }
}

static void parse_encoded_methods(ApkInfo *info, const char *dex_name,
                                  const unsigned char *data, size_t size,
                                  uint32_t string_ids_off, uint32_t string_ids_size,
                                  uint32_t type_ids_off, uint32_t type_ids_size,
                                  uint32_t proto_ids_off, uint32_t proto_ids_size,
                                  uint32_t method_ids_off, uint32_t method_ids_size,
                                  const char *owner_class, uint32_t count,
                                  size_t *offset, uint32_t *last_method_idx) {
    for (uint32_t i = 0; i < count; i++) {
        uint32_t method_idx_diff;
        uint32_t access_flags;
        uint32_t code_off;
        uint32_t method_idx;
        char class_name[256];
        char method_name[128];
        char params[512];
        char return_type[128];

        if (!read_uleb128(data, size, offset, &method_idx_diff) ||
            !read_uleb128(data, size, offset, &access_flags) ||
            !read_uleb128(data, size, offset, &code_off)) {
            return;
        }

        *last_method_idx += method_idx_diff;
        method_idx = *last_method_idx;
        get_method_ref(data, size, string_ids_off, string_ids_size,
                       type_ids_off, type_ids_size, proto_ids_off, proto_ids_size,
                       method_ids_off, method_ids_size, method_idx,
                       class_name, sizeof(class_name), method_name, sizeof(method_name),
                       params, sizeof(params), return_type, sizeof(return_type));

        if (access_flags & ACC_NATIVE) {
            add_native_method(info, dex_name,
                              class_name[0] ? class_name : owner_class,
                              method_name, params, return_type);
        }
        if (code_off) {
            scan_code_for_load_calls(info, dex_name, data, size,
                                     string_ids_off, string_ids_size,
                                     type_ids_off, type_ids_size,
                                     proto_ids_off, proto_ids_size,
                                     method_ids_off, method_ids_size,
                                     owner_class, method_name, code_off);
        }
    }
}

static void parse_dex(ApkInfo *info, const char *dex_name,
                      const unsigned char *data, size_t size) {
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

    if (size < 112 || memcmp(data, "dex\n", 4) != 0) {
        return;
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

    if (string_ids_off > size || type_ids_off > size || proto_ids_off > size ||
        method_ids_off > size || class_defs_off > size) {
        return;
    }

    for (uint32_t i = 0; i < class_defs_size; i++) {
        size_t class_def = class_defs_off + i * 32u;
        uint32_t class_idx;
        uint32_t class_data_off;
        uint32_t static_fields;
        uint32_t instance_fields;
        uint32_t direct_methods;
        uint32_t virtual_methods;
        uint32_t last_method_idx = 0;
        size_t offset;
        char owner_class[256];

        if (class_def + 32u > size) {
            break;
        }

        class_idx = read_u32(data + class_def);
        class_data_off = read_u32(data + class_def + 24u);
        if (class_data_off == 0 || class_data_off >= size) {
            continue;
        }

        format_dex_type(data, size, string_ids_off, string_ids_size,
                        type_ids_off, type_ids_size, class_idx,
                        owner_class, sizeof(owner_class));

        offset = class_data_off;
        if (!read_uleb128(data, size, &offset, &static_fields) ||
            !read_uleb128(data, size, &offset, &instance_fields) ||
            !read_uleb128(data, size, &offset, &direct_methods) ||
            !read_uleb128(data, size, &offset, &virtual_methods)) {
            continue;
        }

        for (uint32_t field = 0; field < static_fields + instance_fields; field++) {
            uint32_t ignored;
            if (!read_uleb128(data, size, &offset, &ignored) ||
                !read_uleb128(data, size, &offset, &ignored)) {
                break;
            }
        }

        parse_encoded_methods(info, dex_name, data, size,
                              string_ids_off, string_ids_size,
                              type_ids_off, type_ids_size,
                              proto_ids_off, proto_ids_size,
                              method_ids_off, method_ids_size,
                              owner_class, direct_methods, &offset, &last_method_idx);
        last_method_idx = 0;
        parse_encoded_methods(info, dex_name, data, size,
                              string_ids_off, string_ids_size,
                              type_ids_off, type_ids_size,
                              proto_ids_off, proto_ids_size,
                              method_ids_off, method_ids_size,
                              owner_class, virtual_methods, &offset, &last_method_idx);
    }
}

static char *decode_utf16_string(const unsigned char *src, size_t bytes) {
    size_t chars = bytes / 2;
    char *out = (char *)calloc(chars * 3 + 1, 1);
    size_t i;
    size_t j = 0;

    if (!out) {
        return NULL;
    }

    for (i = 0; i < chars; i++) {
        uint16_t ch = read_u16(src + i * 2);
        if (ch < 0x80) {
            out[j++] = (char)ch;
        } else if (ch < 0x800) {
            out[j++] = (char)(0xc0 | (ch >> 6));
            out[j++] = (char)(0x80 | (ch & 0x3f));
        } else {
            out[j++] = (char)(0xe0 | (ch >> 12));
            out[j++] = (char)(0x80 | ((ch >> 6) & 0x3f));
            out[j++] = (char)(0x80 | (ch & 0x3f));
        }
    }

    return out;
}

static int parse_string_pool(const unsigned char *data, size_t size, size_t offset, StringPool *pool) {
    uint32_t chunk_size;
    uint32_t string_count;
    uint32_t flags;
    uint32_t strings_start;
    int is_utf8;
    size_t i;

    if (offset + 28 > size || read_u16(data + offset) != AXML_CHUNK_STRING_POOL) {
        return 0;
    }

    chunk_size = read_u32(data + offset + 4);
    string_count = read_u32(data + offset + 8);
    flags = read_u32(data + offset + 16);
    strings_start = read_u32(data + offset + 20);
    is_utf8 = (flags & 0x00000100u) != 0;

    if (offset + chunk_size > size || string_count > 100000u) {
        return 0;
    }

    pool->items = (char **)calloc(string_count, sizeof(char *));
    pool->count = string_count;
    if (!pool->items) {
        return 0;
    }

    for (i = 0; i < string_count; i++) {
        size_t string_offset_pos = offset + 28 + i * 4;
        size_t string_pos;
        if (string_offset_pos + 4 > size) {
            break;
        }
        string_pos = offset + strings_start + read_u32(data + string_offset_pos);
        if (string_pos >= offset + chunk_size || string_pos >= size) {
            continue;
        }

        if (is_utf8) {
            uint32_t byte_len;
            const unsigned char *p = data + string_pos;
            const unsigned char *chunk_end = data + offset + chunk_size;
            if (p >= chunk_end) {
                continue;
            }
            (void)read_length8(&p, chunk_end);
            byte_len = read_length8(&p, chunk_end);
            if (p + byte_len > chunk_end) {
                continue;
            }
            pool->items[i] = (char *)calloc(byte_len + 1, 1);
            if (pool->items[i]) {
                memcpy(pool->items[i], p, byte_len);
            }
        } else {
            uint32_t char_len;
            const unsigned char *p = data + string_pos;
            const unsigned char *chunk_end = data + offset + chunk_size;
            if (p >= chunk_end) {
                continue;
            }
            char_len = read_length16(&p, chunk_end);
            if (p + char_len * 2 > chunk_end) {
                continue;
            }
            pool->items[i] = decode_utf16_string(p, char_len * 2);
        }
    }

    return 1;
}

static void free_string_pool(StringPool *pool) {
    size_t i;
    for (i = 0; i < pool->count; i++) {
        free(pool->items[i]);
    }
    free(pool->items);
    pool->items = NULL;
    pool->count = 0;
}

static const char *pool_string(const StringPool *pool, uint32_t index) {
    if (index == 0xffffffffu || index >= pool->count) {
        return NULL;
    }
    return pool->items[index];
}

static int attr_bool(const unsigned char *attr) {
    uint32_t data_type = attr[15];
    uint32_t data_value = read_u32(attr + 16);
    if (data_type == 0x12u) {
        return data_value != 0 ? 1 : 0;
    }
    return -1;
}

static uint32_t attr_int(const unsigned char *attr, const StringPool *pool) {
    uint32_t raw_index = read_u32(attr + 8);
    uint32_t data_type = attr[15];
    if (data_type >= 0x10u && data_type <= 0x1fu) {
        return read_u32(attr + 16);
    }
    if (raw_index != 0xffffffffu) {
        const char *raw = pool_string(pool, raw_index);
        if (raw) {
            return (uint32_t)strtoul(raw, NULL, 10);
        }
    }
    return 0;
}

static const char *attr_string(const unsigned char *attr, const StringPool *pool) {
    uint32_t raw_index = read_u32(attr + 8);
    uint32_t data_type = attr[15];
    if (raw_index != 0xffffffffu) {
        return pool_string(pool, raw_index);
    }
    if (data_type == 0x03u) {
        return pool_string(pool, read_u32(attr + 16));
    }
    return NULL;
}

static void parse_manifest_element(const unsigned char *chunk, size_t chunk_size,
                                   const StringPool *pool, ManifestInfo *manifest) {
    const char *element_name;
    uint32_t name_index;
    uint16_t attr_count;
    size_t attrs_offset;
    size_t i;

    if (chunk_size < 36) {
        return;
    }

    name_index = read_u32(chunk + 20);
    element_name = pool_string(pool, name_index);
    attr_count = read_u16(chunk + 28);
    attrs_offset = 16u + read_u16(chunk + 24);
    if (attrs_offset == 0 || attrs_offset >= chunk_size) {
        attrs_offset = 36;
    }

    for (i = 0; i < attr_count; i++) {
        const unsigned char *attr = chunk + attrs_offset + i * 20;
        const char *attr_name;
        const char *text_value;
        int bool_value;

        if (attrs_offset + i * 20 + 20 > chunk_size) {
            break;
        }

        attr_name = pool_string(pool, read_u32(attr + 4));
        if (!element_name || !attr_name) {
            continue;
        }

        text_value = attr_string(attr, pool);
        bool_value = attr_bool(attr);

        if (strcmp(element_name, "manifest") == 0) {
            if (strcmp(attr_name, "package") == 0) {
                set_text(manifest->package_name, sizeof(manifest->package_name), text_value);
            } else if (strcmp(attr_name, "versionName") == 0) {
                set_text(manifest->version_name, sizeof(manifest->version_name), text_value);
            } else if (strcmp(attr_name, "versionCode") == 0) {
                manifest->version_code = attr_int(attr, pool);
            }
        } else if (strcmp(element_name, "uses-sdk") == 0) {
            if (strcmp(attr_name, "minSdkVersion") == 0) {
                manifest->min_sdk = attr_int(attr, pool);
            } else if (strcmp(attr_name, "targetSdkVersion") == 0) {
                manifest->target_sdk = attr_int(attr, pool);
            }
        } else if (strcmp(element_name, "application") == 0) {
            if (strcmp(attr_name, "name") == 0) {
                set_text(manifest->application_class, sizeof(manifest->application_class), text_value);
            } else if (strcmp(attr_name, "debuggable") == 0) {
                manifest->debuggable = bool_value;
            } else if (strcmp(attr_name, "testOnly") == 0) {
                manifest->test_only = bool_value;
            }
        }
    }
}

static int parse_android_manifest(const unsigned char *data, size_t size, ManifestInfo *manifest) {
    StringPool pool;
    size_t offset;

    memset(&pool, 0, sizeof(pool));
    memset(manifest, 0, sizeof(*manifest));
    manifest->debuggable = 0;
    manifest->test_only = 0;

    if (size < 8 || read_u32(data) != AXML_CHUNK_XML) {
        return 0;
    }

    offset = 8;
    while (offset + 8 <= size) {
        uint16_t type = read_u16(data + offset);
        uint32_t chunk_size = read_u32(data + offset + 4);
        if (chunk_size < 8 || offset + chunk_size > size) {
            break;
        }

        if (type == AXML_CHUNK_STRING_POOL) {
            parse_string_pool(data, size, offset, &pool);
        } else if (type == AXML_CHUNK_START_ELEMENT && pool.items) {
            parse_manifest_element(data + offset, chunk_size, &pool, manifest);
        } else if (type == AXML_CHUNK_RESOURCE_MAP) {
            /* The string names are enough for this starter inspector. */
        }

        offset += chunk_size;
    }

    manifest->has_manifest = pool.items != NULL;
    normalize_application_class(manifest);
    free_string_pool(&pool);
    return manifest->has_manifest;
}

static int compute_cert_hash(zip_t *apk, const char *entry_name, ApkInfo *info) {
    unsigned char *data;
    size_t size;
    Sha256 sha;

    if (!read_zip_entry(apk, entry_name, &data, &size)) {
        return 0;
    }

    sha256_init(&sha);
    sha256_update(&sha, data, size);
    sha256_final(&sha, info->cert_sha256);
    info->has_cert = 1;
    free(data);
    return 1;
}

static void detect_signing_block(ApkInfo *info) {
    FILE *file = fopen(info->path, "rb");
    unsigned char *tail;
    unsigned char *block;
    long file_size;
    long tail_size;
    long eocd = -1;
    uint32_t central_dir_offset;
    uint64_t block_size;
    long block_start;
    size_t pos;

    if (!file) {
        return;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return;
    }
    file_size = ftell(file);
    tail_size = file_size < 70000 ? file_size : 70000;
    if (tail_size <= 0 || fseek(file, file_size - tail_size, SEEK_SET) != 0) {
        fclose(file);
        return;
    }

    tail = (unsigned char *)malloc((size_t)tail_size);
    if (!tail) {
        fclose(file);
        return;
    }
    if (fread(tail, 1, (size_t)tail_size, file) != (size_t)tail_size) {
        free(tail);
        fclose(file);
        return;
    }

    for (long i = tail_size - 22; i >= 0; i--) {
        if (read_u32(tail + i) == 0x06054b50u) {
            eocd = file_size - tail_size + i;
            break;
        }
    }
    if (eocd < 0 || eocd - (file_size - tail_size) + 20 > tail_size) {
        free(tail);
        fclose(file);
        return;
    }

    central_dir_offset = read_u32(tail + (eocd - (file_size - tail_size)) + 16);
    free(tail);
    if (central_dir_offset < 24 || fseek(file, (long)central_dir_offset - 24, SEEK_SET) != 0) {
        fclose(file);
        return;
    }

    {
        unsigned char footer[24];
        if (fread(footer, 1, sizeof(footer), file) != sizeof(footer)) {
            fclose(file);
            return;
        }
        if (memcmp(footer + 8, "APK Sig Block 42", 16) != 0) {
            fclose(file);
            return;
        }
        block_size = read_u64(footer);
    }

    if (block_size > (uint64_t)central_dir_offset || block_size < 24) {
        fclose(file);
        return;
    }

    block_start = (long)((uint64_t)central_dir_offset - block_size - 8u);
    if (block_start < 0 || fseek(file, block_start, SEEK_SET) != 0) {
        fclose(file);
        return;
    }

    block = (unsigned char *)malloc((size_t)block_size + 8u);
    if (!block) {
        fclose(file);
        return;
    }
    if (fread(block, 1, (size_t)block_size + 8u, file) != (size_t)block_size + 8u) {
        free(block);
        fclose(file);
        return;
    }

    pos = 8;
    while (pos + 12 <= (size_t)block_size) {
        uint64_t pair_size = read_u64(block + pos);
        uint32_t id;
        if (pair_size < 4 || pos + 8u + pair_size > (size_t)block_size) {
            break;
        }
        id = read_u32(block + pos + 8);
        if (id == APK_SIG_V2) {
            info->sig_v2 = 1;
        } else if (id == APK_SIG_V3) {
            info->sig_v3 = 1;
        } else if (id == APK_SIG_V31) {
            info->sig_v31 = 1;
        }
        pos += 8u + (size_t)pair_size;
    }

    free(block);
    fclose(file);
}

int fpatch_inspect_apk(const char *apk_path, ApkInfo *info) {
    int error = 0;
    zip_t *apk = zip_open(apk_path, ZIP_RDONLY, &error);
    zip_int64_t entries;
    zip_int64_t i;

    memset(info, 0, sizeof(*info));
    info->path = apk_path;
    info->manifest.debuggable = -1;
    info->manifest.test_only = -1;

    if (!apk) {
        fprintf(stderr, "Error: Could not open APK/ZIP '%s' (zip error %d)\n", apk_path, error);
        return 0;
    }

    info->is_apk = ends_with(apk_path, ".apk");
    entries = zip_get_num_entries(apk, 0);
    for (i = 0; i < entries; i++) {
        const char *name = zip_get_name(apk, (zip_uint64_t)i, 0);
        if (!name) {
            continue;
        }

        if (strcmp(name, "AndroidManifest.xml") == 0) {
            unsigned char *manifest_data;
            size_t manifest_size;
            info->has_manifest = 1;
            if (read_zip_entry(apk, name, &manifest_data, &manifest_size)) {
                parse_android_manifest(manifest_data, manifest_size, &info->manifest);
                free(manifest_data);
            }
        } else if (strcmp(name, "resources.arsc") == 0) {
            info->has_resources = 1;
        } else if (starts_with(name, "classes") && ends_with(name, ".dex")) {
            unsigned char *dex_data;
            size_t dex_size;
            info->dex_count++;
            if (read_zip_entry(apk, name, &dex_data, &dex_size)) {
                parse_dex(info, name, dex_data, dex_size);
                free(dex_data);
            }
        } else if (starts_with(name, "lib/") && ends_with(name, ".so")) {
            add_native_lib(info, apk, (zip_uint64_t)i, name);
        } else if (starts_with(name, "META-INF/") &&
                   (ends_with(name, ".RSA") || ends_with(name, ".DSA") || ends_with(name, ".EC"))) {
            if (!info->has_cert) {
                compute_cert_hash(apk, name, info);
            }
        }

        if (contains_text(name, "falconpatch") || contains_text(name, "fpatch")) {
            info->has_falcon_bootstrap = 1;
        }
    }

    zip_close(apk);
    detect_signing_block(info);
    return 1;
}

void fpatch_print_csv_abis(const ApkInfo *info) {
    size_t i;
    if (info->abi_count == 0) {
        printf("none");
        return;
    }
    for (i = 0; i < info->abi_count; i++) {
        printf("%s%s", i == 0 ? "" : ", ", info->abis[i]);
    }
}

static void print_bool(int value) {
    if (value < 0) {
        printf("unknown");
    } else {
        printf("%s", value ? "yes" : "no");
    }
}

static void print_sig_schemes(const ApkInfo *info) {
    int printed = 0;
    if (info->sig_v2) {
        printf("v2");
        printed = 1;
    }
    if (info->sig_v3) {
        printf("%sv3", printed ? ", " : "");
        printed = 1;
    }
    if (info->sig_v31) {
        printf("%sv3.1", printed ? ", " : "");
        printed = 1;
    }
    if (!printed && info->has_cert) {
        printf("v1/JAR");
    } else if (!printed) {
        printf("unknown");
    }
}

static void print_native_bridge_summary(const ApkInfo *info) {
    int native_printed = info->native_method_count < MAX_NATIVE_METHODS ?
                         info->native_method_count : MAX_NATIVE_METHODS;
    int load_printed = info->load_call_count < MAX_LOAD_CALLS ?
                       info->load_call_count : MAX_LOAD_CALLS;

    puts("  Java/Kotlin -> NDK");
    printf("    Native declarations: %d\n", info->native_method_count);
    printf("    Library load calls: %d\n", info->load_call_count);

    puts("    Modules loaded:");
    if (info->load_call_count == 0) {
        puts("      none found");
    } else {
        for (int i = 0; i < load_printed; i++) {
            const NativeLoadCall *call = &info->load_calls[i];
            printf("      %s(%s) in %s.%s [%s]\n",
                   call->api, call->argument, call->class_name,
                   call->method_name, call->dex_file);
        }
        if (info->load_call_count > MAX_LOAD_CALLS) {
            printf("      ... %d more load calls omitted\n", info->load_call_count - MAX_LOAD_CALLS);
        }
    }

    puts("    Native methods:");
    if (info->native_method_count == 0) {
        puts("      none found");
    } else {
        for (int i = 0; i < native_printed; i++) {
            const NativeMethod *method = &info->native_methods[i];
            printf("      %s.%s(%s): %s [%s]\n",
                   method->class_name, method->method_name,
                   method->params, method->return_type, method->dex_file);
        }
        if (info->native_method_count > MAX_NATIVE_METHODS) {
            printf("      ... %d more native declarations omitted\n",
                   info->native_method_count - MAX_NATIVE_METHODS);
        }
    }
}

void fpatch_print_inspect_report(const ApkInfo *info) {
    const ManifestInfo *m = &info->manifest;
    int debuggable = m->debuggable == 1;

    puts("Source");
    printf("  File: %s\n", info->path);
    printf("  Type: %s\n", info->is_apk ? "standalone-apk" : "zip-like-apk");
    printf("  Package: %s\n", m->package_name[0] ? m->package_name : "unknown");
    printf("  Version: %s", m->version_name[0] ? m->version_name : "unknown");
    if (m->version_code) {
        printf(" (%u)", m->version_code);
    }
    printf("\n");
    printf("  Min SDK: %s", m->min_sdk ? "" : "unknown");
    if (m->min_sdk) {
        printf("%u", m->min_sdk);
    }
    printf("\n");
    printf("  Target SDK: %s", m->target_sdk ? "" : "unknown");
    if (m->target_sdk) {
        printf("%u", m->target_sdk);
    }
    printf("\n\n");

    puts("Security");
    printf("  Debuggable: ");
    print_bool(m->debuggable);
    printf("\n");
    printf("  Test-only: ");
    print_bool(m->test_only);
    printf("\n");
    printf("  Certificate: ");
    if (info->has_cert) {
        printf("SHA-256 ");
        print_sha256(info->cert_sha256);
    } else {
        printf("unknown");
    }
    printf("\n");
    printf("  APK Signature Schemes: ");
    print_sig_schemes(info);
    printf("\n\n");

    puts("Code");
    printf("  DEX files: %d\n", info->dex_count);
    printf("  Application class: %s\n", m->application_class[0] ? m->application_class : "unknown");
    printf("  Native ABIs: ");
    fpatch_print_csv_abis(info);
    printf("\n");
    printf("  Native libraries: %d\n", info->native_lib_count);
    print_native_bridge_summary(info);
    printf("\n");

    puts("FalconPatch");
    printf("  Existing bootstrap: %s\n", info->has_falcon_bootstrap ? "found" : "not found");
    printf("  Provider bootstrap possible: %s\n", info->has_manifest ? "yes" : "unknown");
    printf("  Additional DEX possible: %s\n", info->dex_count > 0 ? "yes" : "unknown");
    printf("  Manifest patch required: %s\n", info->has_falcon_bootstrap ? "no" : "yes");
    printf("  Resigning required: yes\n\n");

    puts("Strategies");
    printf("  Integrated loader: %s\n", info->has_falcon_bootstrap ? "available" : "unavailable");
    printf("  JVMTI: %s\n", debuggable ? "available" : "unavailable - application is not debuggable");
    printf("  Startup wrapper: %s\n", debuggable ? "available" : "unavailable - application is not debuggable");
    printf("  Manifest debug patch: %s\n", info->has_manifest ? "available" : "unavailable");
    printf("  Bootstrap APK patch: %s\n", info->has_manifest ? "available" : "unavailable");
}

void fpatch_print_ndk_report(const ApkInfo *info) {
    int has_64 = 0;
    int has_32 = 0;
    int compressed_libs = 0;
    int missing_elf = 0;
    int abi_header_mismatches = 0;
    int printed = info->native_lib_count < MAX_NATIVE_LIBS ?
                  info->native_lib_count : MAX_NATIVE_LIBS;

    for (int i = 0; i < printed; i++) {
        const NativeLib *lib = &info->native_libs[i];
        const char *machine_abi = elf_machine_name(lib->elf_machine);

        if (strcmp(lib->abi, "arm64-v8a") == 0 || strcmp(lib->abi, "x86_64") == 0) {
            has_64 = 1;
        } else if (lib->abi[0]) {
            has_32 = 1;
        }

        if (lib->compression_method == ZIP_CM_DEFLATE) {
            compressed_libs++;
        }
        if (!lib->elf_valid) {
            missing_elf++;
        } else if (lib->abi[0] && strcmp(machine_abi, "unknown") != 0 &&
                   strcmp(lib->abi, machine_abi) != 0) {
            abi_header_mismatches++;
        }
    }

    puts("NDK");
    printf("  File: %s\n", info->path);
    printf("  Package: %s\n", info->manifest.package_name[0] ? info->manifest.package_name : "unknown");
    printf("  Native libraries: %d\n", info->native_lib_count);
    printf("  Native ABIs: ");
    fpatch_print_csv_abis(info);
    printf("\n");
    printf("  ABI model: %s\n",
           has_32 && has_64 ? "mixed 32/64-bit" :
           has_64 ? "64-bit only" :
           has_32 ? "32-bit only" : "none");
    printf("  Compressed native libraries: %d\n", compressed_libs);
    printf("  ELF parse failures: %d\n", missing_elf);
    printf("  ABI/header mismatches: %d\n", abi_header_mismatches);
    printf("  Extraction required before load: %s\n",
           compressed_libs > 0 ? "yes" : info->native_lib_count > 0 ? "no" : "not applicable");

    puts("");
    puts("Native Libraries");
    if (info->native_lib_count == 0) {
        puts("  none");
        return;
    }

    for (int i = 0; i < printed; i++) {
        const NativeLib *lib = &info->native_libs[i];

        printf("  %s\n", lib->path);
        printf("    ABI: %s\n", lib->abi[0] ? lib->abi : "unknown");
        printf("    Size: %llu bytes\n", lib->size);
        if (lib->compressed_size > 0) {
            printf("    Compressed size: %llu bytes\n", lib->compressed_size);
        }
        printf("    ZIP compression: %s\n", compression_method_name(lib->compression_method));
        if (lib->elf_valid) {
            printf("    ELF: %d-bit %s\n", lib->elf_class, elf_machine_name(lib->elf_machine));
        } else {
            printf("    ELF: unreadable or not an ELF shared object\n");
        }
    }

    if (info->native_lib_count > MAX_NATIVE_LIBS) {
        printf("  ... %d more native libraries omitted\n", info->native_lib_count - MAX_NATIVE_LIBS);
    }
}
