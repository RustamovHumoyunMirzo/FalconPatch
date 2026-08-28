#include "utils/apk_detacher.h"

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
    offset += length;
    data[offset++] = 0;
    return offset;
}

static size_t put_uleb(unsigned char *data, size_t offset, uint32_t value) {
    do {
        unsigned char byte = (unsigned char)(value & 0x7fu);
        value >>= 7;
        if (value) {
            byte |= 0x80u;
        }
        data[offset++] = byte;
    } while (value);
    return offset;
}

static void make_dex(unsigned char *dex, size_t *size,
                     const char *method_name, const char *literal,
                     int use_move) {
    const uint32_t string_ids_off = 112;
    const uint32_t type_ids_off = 128;
    const uint32_t proto_ids_off = 136;
    const uint32_t method_ids_off = 148;
    const uint32_t class_defs_off = 156;
    const uint32_t class_data_off = 188;
    const uint32_t code_off = 200;
    const uint32_t string_data_off = 240;
    size_t offset = string_data_off;
    uint32_t system_string_off;
    uint32_t method_name_off;
    uint32_t literal_off;
    uint32_t test_string_off;

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

    system_string_off = (uint32_t)offset;
    offset = put_string(dex, offset, "Ljava/lang/System;");
    method_name_off = (uint32_t)offset;
    offset = put_string(dex, offset, method_name);
    literal_off = (uint32_t)offset;
    offset = put_string(dex, offset, literal);
    test_string_off = (uint32_t)offset;
    offset = put_string(dex, offset, "LTest;");

    write_u32(dex + string_ids_off, system_string_off);
    write_u32(dex + string_ids_off + 4, method_name_off);
    write_u32(dex + string_ids_off + 8, literal_off);
    write_u32(dex + string_ids_off + 12, test_string_off);
    write_u32(dex + type_ids_off, 0);
    write_u32(dex + type_ids_off + 4, 3);
    write_u16(dex + method_ids_off, 0);
    write_u16(dex + method_ids_off + 2, 0);
    write_u32(dex + method_ids_off + 4, 1);
    write_u32(dex + class_defs_off + 24, class_data_off);

    dex[class_data_off] = 0;
    dex[class_data_off + 1] = 0;
    dex[class_data_off + 2] = 1;
    dex[class_data_off + 3] = 0;
    dex[class_data_off + 4] = 0;
    dex[class_data_off + 5] = 0;
    dex[class_data_off + 6] = 0xc8;
    dex[class_data_off + 7] = 0x01;

    write_u16(dex + code_off, use_move ? 2 : 1);
    write_u16(dex + code_off + 4, 1);
    write_u32(dex + code_off + 12, use_move ? 6 : 5);
    write_u16(dex + code_off + 16, use_move ? 0x011a : 0x001a);
    write_u16(dex + code_off + 18, 2);
    if (use_move) {
        write_u16(dex + code_off + 20, 0x1007);
        write_u16(dex + code_off + 22, 0x0171);
        write_u16(dex + code_off + 24, 0);
        write_u16(dex + code_off + 26, 0);
    } else {
        write_u16(dex + code_off + 20, 0x0171);
        write_u16(dex + code_off + 22, 0);
        write_u16(dex + code_off + 24, 0);
    }

    write_u32(dex + 32, (uint32_t)offset);
    *size = offset;
}

static void make_native_dex(unsigned char *dex, size_t *size) {
    const uint32_t string_ids_off = 112;
    const uint32_t type_ids_off = 136;
    const uint32_t proto_ids_off = 152;
    const uint32_t method_ids_off = 176;
    const uint32_t class_defs_off = 192;
    const uint32_t native_class_data_off = 256;
    const uint32_t caller_class_data_off = 264;
    const uint32_t code_off = 272;
    const uint32_t string_data_off = 304;
    size_t offset = string_data_off;
    uint32_t bridge_off;
    uint32_t ping_off;
    uint32_t int_off;
    uint32_t void_off;
    uint32_t test_off;
    uint32_t call_off;

    memset(dex, 0, 512);
    memcpy(dex, "dex\n035", 7);
    write_u32(dex + 36, 112);
    write_u32(dex + 40, 0x12345678u);
    write_u32(dex + 56, 6);
    write_u32(dex + 60, string_ids_off);
    write_u32(dex + 64, 4);
    write_u32(dex + 68, type_ids_off);
    write_u32(dex + 72, 2);
    write_u32(dex + 76, proto_ids_off);
    write_u32(dex + 88, 2);
    write_u32(dex + 92, method_ids_off);
    write_u32(dex + 96, 2);
    write_u32(dex + 100, class_defs_off);

    bridge_off = (uint32_t)offset;
    offset = put_string(dex, offset, "Lcom/example/Bridge;");
    ping_off = (uint32_t)offset;
    offset = put_string(dex, offset, "ping");
    int_off = (uint32_t)offset;
    offset = put_string(dex, offset, "I");
    void_off = (uint32_t)offset;
    offset = put_string(dex, offset, "V");
    test_off = (uint32_t)offset;
    offset = put_string(dex, offset, "LTest;");
    call_off = (uint32_t)offset;
    offset = put_string(dex, offset, "call");

    write_u32(dex + string_ids_off, bridge_off);
    write_u32(dex + string_ids_off + 4, ping_off);
    write_u32(dex + string_ids_off + 8, int_off);
    write_u32(dex + string_ids_off + 12, void_off);
    write_u32(dex + string_ids_off + 16, test_off);
    write_u32(dex + string_ids_off + 20, call_off);
    write_u32(dex + type_ids_off, 0);
    write_u32(dex + type_ids_off + 4, 2);
    write_u32(dex + type_ids_off + 8, 3);
    write_u32(dex + type_ids_off + 12, 4);
    write_u32(dex + proto_ids_off + 4, 1);
    write_u32(dex + proto_ids_off + 12 + 4, 2);
    write_u16(dex + method_ids_off, 0);
    write_u16(dex + method_ids_off + 2, 0);
    write_u32(dex + method_ids_off + 4, 1);
    write_u16(dex + method_ids_off + 8, 3);
    write_u16(dex + method_ids_off + 10, 1);
    write_u32(dex + method_ids_off + 12, 5);
    write_u32(dex + class_defs_off, 0);
    write_u32(dex + class_defs_off + 24, native_class_data_off);
    write_u32(dex + class_defs_off + 32, 3);
    write_u32(dex + class_defs_off + 32 + 24, caller_class_data_off);

    offset = native_class_data_off;
    offset = put_uleb(dex, offset, 0);
    offset = put_uleb(dex, offset, 0);
    offset = put_uleb(dex, offset, 1);
    offset = put_uleb(dex, offset, 0);
    offset = put_uleb(dex, offset, 0);
    offset = put_uleb(dex, offset, 0x0109u);
    offset = put_uleb(dex, offset, 0);

    offset = caller_class_data_off;
    offset = put_uleb(dex, offset, 0);
    offset = put_uleb(dex, offset, 0);
    offset = put_uleb(dex, offset, 1);
    offset = put_uleb(dex, offset, 0);
    offset = put_uleb(dex, offset, 1);
    offset = put_uleb(dex, offset, 1);
    offset = put_uleb(dex, offset, code_off);

    write_u16(dex + code_off, 1);
    write_u16(dex + code_off + 4, 1);
    write_u32(dex + code_off + 12, 5);
    write_u16(dex + code_off + 16, 0x0071);
    write_u16(dex + code_off + 18, 0);
    write_u16(dex + code_off + 20, 0);
    write_u16(dex + code_off + 22, 0x000a);
    write_u16(dex + code_off + 24, 0x000f);

    write_u32(dex + 32, (uint32_t)offset > string_data_off ? (uint32_t)offset : (uint32_t)string_data_off);
    if (offset < string_data_off) {
        offset = string_data_off;
    }
    while (offset < string_data_off) {
        offset++;
    }
    *size = 384;
}

int main(void) {
    unsigned char dex[512];
    size_t size;

    make_dex(dex, &size, "loadLibrary", "demo", 0);
    if (fpatch_repair_dex_load_calls(dex, size, "demo", "libdemo.so") != 1) {
        fprintf(stderr, "Expected one loadLibrary call to be repaired.\n");
        return 1;
    }
    if (dex[220] != 0 || dex[221] != 0 || dex[222] != 0 ||
        dex[223] != 0 || dex[224] != 0 || dex[225] != 0) {
        fprintf(stderr, "Invoke instruction was not replaced by nops.\n");
        return 1;
    }
    if (dex[8] == 0 && dex[9] == 0 && dex[10] == 0 && dex[11] == 0) {
        fprintf(stderr, "DEX checksum was not refreshed.\n");
        return 1;
    }
    make_dex(dex, &size, "load", "/data/local/tmp/libdemo.so", 1);
    if (fpatch_repair_dex_load_calls(dex, size, "demo", "libdemo.so") != 1) {
        fprintf(stderr, "Expected one moved System.load call to be repaired.\n");
        return 1;
    }
    if (dex[222] != 0 || dex[223] != 0 || dex[224] != 0 ||
        dex[225] != 0 || dex[226] != 0 || dex[227] != 0) {
        fprintf(stderr, "Moved invoke instruction was not replaced by nops.\n");
        return 1;
    }
    {
        const char *exports[] = { "Java_com_example_Bridge_ping" };
        size_t skipped = 0;
        make_native_dex(dex, &size);
        if (fpatch_repair_dex_jni_calls(dex, size, exports, 1, &skipped) != 1 ||
            skipped != 0) {
            fprintf(stderr, "Expected one static JNI call to be repaired.\n");
            return 1;
        }
        if (dex[288] != 0 || dex[289] != 0 || dex[290] != 0 ||
            dex[291] != 0 || dex[292] != 0 || dex[293] != 0 ||
            dex[294] != 0x12 || dex[295] != 0) {
            fprintf(stderr, "JNI invoke or move-result was not safely repaired.\n");
            return 1;
        }
    }
    puts("DEX smart repair tests passed.");
    return 0;
}
