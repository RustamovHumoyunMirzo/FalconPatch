#include "utils/payload_archive.h"
#include "utils/file_utils.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FPATCH_RECORD_LUA 1u
#define FPATCH_RECORD_NATIVE 2u
#define FPATCH_RECORD_ENTRY 1u

typedef struct {
    unsigned char *data;
    size_t size;
    size_t capacity;
} Buffer;

static void set_error(char *error, size_t error_size, const char *message) {
    if (error && error_size) {
        snprintf(error, error_size, "%s", message);
    }
}

static int reserve(Buffer *buffer, size_t additional) {
    size_t capacity;
    unsigned char *resized;

    if (additional > SIZE_MAX - buffer->size) {
        return 0;
    }
    if (buffer->size + additional <= buffer->capacity) {
        return 1;
    }
    capacity = buffer->capacity ? buffer->capacity : 4096;
    while (capacity < buffer->size + additional) {
        if (capacity > SIZE_MAX / 2) {
            capacity = buffer->size + additional;
            break;
        }
        capacity *= 2;
    }
    resized = (unsigned char *)realloc(buffer->data, capacity);
    if (!resized) {
        return 0;
    }
    buffer->data = resized;
    buffer->capacity = capacity;
    return 1;
}

static int append(Buffer *buffer, const void *data, size_t size) {
    if (!reserve(buffer, size)) {
        return 0;
    }
    memcpy(buffer->data + buffer->size, data, size);
    buffer->size += size;
    return 1;
}

static int append_u16(Buffer *buffer, uint16_t value) {
    unsigned char bytes[2] = {(unsigned char)value, (unsigned char)(value >> 8)};
    return append(buffer, bytes, sizeof(bytes));
}

static int append_u32(Buffer *buffer, uint32_t value) {
    unsigned char bytes[4] = {
        (unsigned char)value, (unsigned char)(value >> 8),
        (unsigned char)(value >> 16), (unsigned char)(value >> 24)
    };
    return append(buffer, bytes, sizeof(bytes));
}

static int append_u64(Buffer *buffer, uint64_t value) {
    return append_u32(buffer, (uint32_t)value) && append_u32(buffer, (uint32_t)(value >> 32));
}

static int append_record(Buffer *buffer, uint8_t type, uint8_t flags,
                         const char *name, const char *aux,
                         const void *data, size_t size) {
    size_t name_size = strlen(name);
    size_t aux_size = strlen(aux);
    unsigned char prefix[2] = {type, flags};

    if (name_size > UINT16_MAX || aux_size > UINT32_MAX) {
        return 0;
    }
    return append(buffer, prefix, sizeof(prefix)) &&
           append_u16(buffer, (uint16_t)name_size) &&
           append_u32(buffer, (uint32_t)aux_size) &&
           append_u64(buffer, (uint64_t)size) &&
           append(buffer, name, name_size) &&
           append(buffer, aux, aux_size) &&
           append(buffer, data, size);
}

static int ends_with(const char *value, const char *suffix) {
    size_t value_size = strlen(value);
    size_t suffix_size = strlen(suffix);
    return value_size >= suffix_size &&
           strcmp(value + value_size - suffix_size, suffix) == 0;
}

int fpatch_native_filename(const FpatchNativeInput *input,
                           char *output, size_t output_size) {
    const char *source;
    int written;

    if (input->name[0]) {
        source = input->name;
        if (ends_with(source, ".so")) {
            written = snprintf(output, output_size, "%s", source);
        } else if (strncmp(source, "lib", 3) == 0) {
            written = snprintf(output, output_size, "%s.so", source);
        } else {
            written = snprintf(output, output_size, "lib%s.so", source);
        }
    } else {
        source = fpatch_path_basename(input->path);
        written = snprintf(output, output_size, "%s", source);
    }
    return written >= 0 && (size_t)written < output_size &&
           strncmp(output, "lib", 3) == 0 && ends_with(output, ".so");
}

static void native_module_name(const FpatchNativeInput *input,
                               const char *filename, char *output, size_t output_size) {
    size_t length;

    if (input->lua_module[0]) {
        snprintf(output, output_size, "%s", input->lua_module);
        return;
    }
    snprintf(output, output_size, "%s", filename + 3);
    length = strlen(output);
    if (length > 3 && strcmp(output + length - 3, ".so") == 0) {
        output[length - 3] = '\0';
    }
}

int fpatch_build_payload(const FpatchInjectProfile *profile,
                         unsigned char **data, size_t *size,
                         char *error, size_t error_size) {
    Buffer buffer = {0};
    uint32_t record_count = 0;
    size_t i;

    *data = NULL;
    *size = 0;
    if (!append(&buffer, "FPB1", 4) || !append_u32(&buffer, 1) ||
        !append_u32(&buffer, 0)) {
        goto memory_error;
    }
    for (i = 0; i < profile->native_count; i++) {
        char filename[256];
        char module_name[128];
        char native_metadata[192];
        int metadata_length;
        if (!fpatch_native_filename(&profile->native[i], filename, sizeof(filename))) {
            set_error(error, error_size,
                      "Native library names must use the lib<name>.so Android convention.");
            free(buffer.data);
            return 0;
        }
        native_module_name(&profile->native[i], filename, module_name, sizeof(module_name));
        metadata_length = snprintf(native_metadata, sizeof(native_metadata), "%s|%s",
                                   profile->native[i].abi,
                                   profile->native[i].init);
        if (metadata_length < 0 || (size_t)metadata_length >= sizeof(native_metadata)) {
            set_error(error, error_size, "Native ABI/initializer metadata is too long.");
            free(buffer.data);
            return 0;
        }
        if (!append_record(&buffer, FPATCH_RECORD_NATIVE, 0, module_name,
                           native_metadata, filename, strlen(filename))) {
            goto memory_error;
        }
        record_count++;
    }
    for (i = 0; i < profile->lua_count; i++) {
        unsigned char *script = NULL;
        size_t script_size = 0;
        const char *name = fpatch_path_basename(profile->lua[i].path);
        if (!fpatch_read_file(profile->lua[i].path, &script, &script_size,
                              error, error_size)) {
            free(buffer.data);
            return 0;
        }
        if (!append_record(&buffer, FPATCH_RECORD_LUA,
                           profile->lua[i].entry ? FPATCH_RECORD_ENTRY : 0,
                           name, profile->lua[i].module, script, script_size)) {
            free(script);
            goto memory_error;
        }
        free(script);
        record_count++;
    }
    buffer.data[8] = (unsigned char)record_count;
    buffer.data[9] = (unsigned char)(record_count >> 8);
    buffer.data[10] = (unsigned char)(record_count >> 16);
    buffer.data[11] = (unsigned char)(record_count >> 24);
    *data = buffer.data;
    *size = buffer.size;
    return 1;

memory_error:
    free(buffer.data);
    set_error(error, error_size, "Out of memory while building the FalconPatch payload.");
    return 0;
}
