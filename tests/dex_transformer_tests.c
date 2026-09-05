#include "utils/dex_transformer.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void write_u16(unsigned char *data, uint16_t value) {
    data[0] = (unsigned char)value;
    data[1] = (unsigned char)(value >> 8);
}

static void write_u32(unsigned char *data, uint32_t value) {
    data[0] = (unsigned char)value;
    data[1] = (unsigned char)(value >> 8);
    data[2] = (unsigned char)(value >> 16);
    data[3] = (unsigned char)(value >> 24);
}

static size_t put_string(unsigned char *data, size_t offset, const char *value) {
    size_t length = strlen(value);
    data[offset++] = (unsigned char)length;
    memcpy(data + offset, value, length);
    return offset + length + 1;
}

static void make_transform_dex(unsigned char *dex, size_t *size,
                               size_t *url_offset, const char *return_descriptor) {
    const uint32_t string_ids_off = 112;
    const uint32_t type_ids_off = 128;
    const uint32_t proto_ids_off = 136;
    const uint32_t method_ids_off = 148;
    const uint32_t class_defs_off = 156;
    const uint32_t class_data_off = 188;
    const uint32_t code_off = 200;
    const uint32_t string_data_off = 240;
    size_t offset = string_data_off;
    uint32_t class_off;
    uint32_t method_off;
    uint32_t boolean_off;
    uint32_t url_item_off;

    memset(dex, 0, 512);
    memcpy(dex, "dex\n035", 7);
    write_u32(dex + 36, 112);
    write_u32(dex + 40, 0x12345678u);
    write_u32(dex + 56, 4);
    write_u32(dex + 60, string_ids_off);
    write_u32(dex + 64, 2);
    write_u32(dex + 68, type_ids_off);
    write_u32(dex + 72, 1);
    write_u32(dex + 76, proto_ids_off);
    write_u32(dex + 88, 1);
    write_u32(dex + 92, method_ids_off);
    write_u32(dex + 96, 1);
    write_u32(dex + 100, class_defs_off);

    class_off = (uint32_t)offset;
    offset = put_string(dex, offset, "Lcom/example/Config;");
    method_off = (uint32_t)offset;
    offset = put_string(dex, offset, "isDebuggable");
    boolean_off = (uint32_t)offset;
    offset = put_string(dex, offset, return_descriptor);
    url_item_off = (uint32_t)offset;
    offset = put_string(dex, offset, "https://api.prod.com");
    *url_offset = url_item_off + 1u;

    write_u32(dex + string_ids_off, class_off);
    write_u32(dex + string_ids_off + 4, method_off);
    write_u32(dex + string_ids_off + 8, boolean_off);
    write_u32(dex + string_ids_off + 12, url_item_off);
    write_u32(dex + type_ids_off, 0);
    write_u32(dex + type_ids_off + 4, 2);
    write_u32(dex + proto_ids_off + 4, 1);
    write_u16(dex + method_ids_off, 0);
    write_u16(dex + method_ids_off + 2, 0);
    write_u32(dex + method_ids_off + 4, 1);
    write_u32(dex + class_defs_off, 0);
    write_u32(dex + class_defs_off + 24, class_data_off);

    dex[class_data_off] = 0;
    dex[class_data_off + 1] = 0;
    dex[class_data_off + 2] = 1;
    dex[class_data_off + 3] = 0;
    dex[class_data_off + 4] = 0;
    dex[class_data_off + 5] = 9;
    dex[class_data_off + 6] = 0xc8;
    dex[class_data_off + 7] = 0x01;

    write_u16(dex + code_off, 1);
    write_u32(dex + code_off + 12, 4);
    write_u16(dex + code_off + 16, 0x0012);
    write_u16(dex + code_off + 18, 0x000f);
    write_u32(dex + 32, (uint32_t)offset);
    *size = offset;
}

static int test_method_and_string_patch(void) {
    unsigned char dex[512];
    FpatchDexPatch patches[2];
    FpatchDexTransformStats stats = {0};
    size_t applied[2] = {0};
    size_t size;
    size_t url_offset;
    char error[256] = "";

    make_transform_dex(dex, &size, &url_offset, "Z");
    memset(patches, 0, sizeof(patches));
    snprintf(patches[0].target, sizeof(patches[0].target), "com.example.Config");
    snprintf(patches[0].method, sizeof(patches[0].method), "isDebuggable()Z");
    patches[0].action = FPATCH_DEX_PATCH_RETURN_TRUE;
    snprintf(patches[1].target, sizeof(patches[1].target), "com.example.Config");
    snprintf(patches[1].string_from, sizeof(patches[1].string_from),
             "https://api.prod.com");
    snprintf(patches[1].string_to, sizeof(patches[1].string_to),
             "http://10.0.2.2:8080");
    patches[1].action = FPATCH_DEX_PATCH_REPLACE_STRING;

    if (!fpatch_transform_dex(dex, size, patches, 2, applied, &stats,
                              error, sizeof(error))) {
        fprintf(stderr, "Transform failed: %s\n", error);
        return 0;
    }
    return applied[0] == 1 && applied[1] == 1 &&
           stats.methods_patched == 1 && stats.strings_replaced == 1 &&
           dex[216] == 0x12 && dex[217] == 0x10 && dex[218] == 0x0f &&
           memcmp(dex + url_offset, "http://10.0.2.2:8080", 20) == 0 &&
           (dex[8] != 0 || dex[9] != 0 || dex[10] != 0 || dex[11] != 0);
}

static int test_rejects_length_change(void) {
    unsigned char dex[512];
    FpatchDexPatch patch;
    size_t applied = 0;
    size_t size;
    size_t ignored;
    char error[256] = "";

    make_transform_dex(dex, &size, &ignored, "Z");
    memset(&patch, 0, sizeof(patch));
    snprintf(patch.target, sizeof(patch.target), "com.example.Config");
    snprintf(patch.string_from, sizeof(patch.string_from), "https://api.prod.com");
    snprintf(patch.string_to, sizeof(patch.string_to), "short");
    patch.action = FPATCH_DEX_PATCH_REPLACE_STRING;
    return !fpatch_transform_dex(dex, size, &patch, 1, &applied, NULL,
                                 error, sizeof(error)) &&
           strstr(error, "equal encoded") != NULL;
}

static int test_unmatched_target_is_unchanged(void) {
    unsigned char dex[512];
    FpatchDexPatch patch;
    FpatchDexTransformStats stats = {0};
    size_t applied = 0;
    size_t size;
    size_t ignored;
    char error[256] = "";

    make_transform_dex(dex, &size, &ignored, "Z");
    memset(&patch, 0, sizeof(patch));
    snprintf(patch.target, sizeof(patch.target), "com.example.Missing");
    snprintf(patch.method, sizeof(patch.method), "isDebuggable()Z");
    patch.action = FPATCH_DEX_PATCH_RETURN_TRUE;
    return fpatch_transform_dex(dex, size, &patch, 1, &applied, &stats,
                                error, sizeof(error)) &&
           applied == 0 && stats.methods_patched == 0;
}

static int test_rejects_try_catch_method(void) {
    unsigned char dex[512];
    FpatchDexPatch patch;
    size_t applied = 0;
    size_t size;
    size_t ignored;
    char error[256] = "";

    make_transform_dex(dex, &size, &ignored, "Z");
    write_u16(dex + 206, 1);
    memset(&patch, 0, sizeof(patch));
    snprintf(patch.target, sizeof(patch.target), "com.example.Config");
    snprintf(patch.method, sizeof(patch.method), "isDebuggable()Z");
    patch.action = FPATCH_DEX_PATCH_RETURN_TRUE;
    return !fpatch_transform_dex(dex, size, &patch, 1, &applied, NULL,
                                 error, sizeof(error)) &&
           strstr(error, "try/catch") != NULL;
}

static int test_return_actions(void) {
    struct ActionCase {
        const char *descriptor;
        FpatchDexPatchAction action;
        unsigned char first_opcode;
        unsigned char return_opcode;
        size_t return_offset;
        int wide;
    } cases[] = {
        {"Z", FPATCH_DEX_PATCH_RETURN_FALSE, 0x12, 0x0f, 218, 0},
        {"I", FPATCH_DEX_PATCH_RETURN_ZERO, 0x12, 0x0f, 218, 0},
        {"J", FPATCH_DEX_PATCH_RETURN_ZERO, 0x16, 0x10, 220, 1},
        {"Ljava/lang/String;", FPATCH_DEX_PATCH_RETURN_NULL, 0x12, 0x11, 218, 0},
        {"V", FPATCH_DEX_PATCH_RETURN_VOID, 0x0e, 0x00, 0, 0}
    };
    size_t i;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        unsigned char dex[512];
        FpatchDexPatch patch;
        size_t applied = 0;
        size_t size;
        size_t ignored;
        char error[256] = "";

        make_transform_dex(dex, &size, &ignored, cases[i].descriptor);
        if (cases[i].wide) {
            write_u16(dex + 200, 2);
        }
        memset(&patch, 0, sizeof(patch));
        snprintf(patch.target, sizeof(patch.target), "com.example.Config");
        snprintf(patch.method, sizeof(patch.method), "isDebuggable()%s",
                 cases[i].descriptor);
        patch.action = cases[i].action;
        if (!fpatch_transform_dex(dex, size, &patch, 1, &applied, NULL,
                                  error, sizeof(error)) ||
            applied != 1 || dex[216] != cases[i].first_opcode ||
            (cases[i].return_offset &&
             dex[cases[i].return_offset] != cases[i].return_opcode)) {
            fprintf(stderr, "Return action failed for %s: %s\n",
                    cases[i].descriptor, error);
            return 0;
        }
    }
    return 1;
}

int main(void) {
    if (!test_method_and_string_patch()) {
        fprintf(stderr, "Method/string transform test failed.\n");
        return 1;
    }
    if (!test_rejects_length_change()) {
        fprintf(stderr, "String length validation test failed.\n");
        return 1;
    }
    if (!test_unmatched_target_is_unchanged()) {
        fprintf(stderr, "Unmatched target test failed.\n");
        return 1;
    }
    if (!test_rejects_try_catch_method()) {
        fprintf(stderr, "Try/catch method validation test failed.\n");
        return 1;
    }
    if (!test_return_actions()) {
        fprintf(stderr, "Return action coverage test failed.\n");
        return 1;
    }
    puts("Declarative DEX transformer tests passed.");
    return 0;
}
