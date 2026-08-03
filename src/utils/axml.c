#include "utils/axml.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RES_XML_TYPE 0x0003u
#define RES_STRING_POOL_TYPE 0x0001u
#define RES_XML_RESOURCE_MAP_TYPE 0x0180u
#define RES_XML_START_ELEMENT_TYPE 0x0102u
#define RES_XML_END_ELEMENT_TYPE 0x0103u
#define UTF8_FLAG 0x00000100u
#define NO_INDEX 0xffffffffu
#define TYPE_STRING 0x03u
#define TYPE_INT_DEC 0x10u
#define TYPE_INT_BOOLEAN 0x12u

#define ATTR_NAME 0x01010003u
#define ATTR_ENABLED 0x0101000eu
#define ATTR_DEBUGGABLE 0x0101000fu
#define ATTR_EXPORTED 0x01010010u
#define ATTR_AUTHORITIES 0x01010018u
#define ATTR_INIT_ORDER 0x0101001au
#define ATTR_VALUE 0x01010024u

#define MAX_NEW_STRINGS 32

typedef struct {
    unsigned char *data;
    size_t size;
    size_t capacity;
} Buffer;

typedef struct {
    const unsigned char *chunk;
    size_t chunk_size;
    uint16_t header_size;
    uint32_t string_count;
    uint32_t style_count;
    uint32_t flags;
    uint32_t strings_start;
    uint32_t styles_start;
} StringPool;

typedef struct {
    char value[512];
    uint32_t index;
} AddedString;

typedef struct {
    StringPool pool;
    AddedString added[MAX_NEW_STRINGS];
    size_t added_count;
} StringEditor;

typedef struct {
    uint32_t namespace_index;
    uint32_t name_index;
    uint32_t raw_index;
    uint8_t type;
    uint32_t value;
    uint32_t resource_id;
} XmlAttribute;

static uint16_t read_u16(const unsigned char *data) {
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static uint32_t read_u32(const unsigned char *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

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

static void set_error(char *error, size_t error_size, const char *message) {
    if (error && error_size) {
        snprintf(error, error_size, "%s", message);
    }
}

static int reserve(Buffer *buffer, size_t additional) {
    size_t wanted;
    size_t capacity;
    unsigned char *resized;

    if (additional > SIZE_MAX - buffer->size) {
        return 0;
    }
    wanted = buffer->size + additional;
    if (wanted <= buffer->capacity) {
        return 1;
    }
    capacity = buffer->capacity ? buffer->capacity : 4096;
    while (capacity < wanted) {
        if (capacity > SIZE_MAX / 2) {
            capacity = wanted;
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

static int append_zeroes(Buffer *buffer, size_t count) {
    if (!reserve(buffer, count)) {
        return 0;
    }
    memset(buffer->data + buffer->size, 0, count);
    buffer->size += count;
    return 1;
}

static uint32_t read_length8(const unsigned char **cursor, const unsigned char *end) {
    uint32_t value;
    if (*cursor >= end) {
        return UINT32_MAX;
    }
    value = *(*cursor)++;
    if (value & 0x80u) {
        if (*cursor >= end) {
            return UINT32_MAX;
        }
        value = ((value & 0x7fu) << 8) | *(*cursor)++;
    }
    return value;
}

static uint32_t read_length16(const unsigned char **cursor, const unsigned char *end) {
    uint32_t value;
    if ((size_t)(end - *cursor) < 2) {
        return UINT32_MAX;
    }
    value = read_u16(*cursor);
    *cursor += 2;
    if (value & 0x8000u) {
        if ((size_t)(end - *cursor) < 2) {
            return UINT32_MAX;
        }
        value = ((value & 0x7fffu) << 16) | read_u16(*cursor);
        *cursor += 2;
    }
    return value;
}

static int parse_pool(const unsigned char *chunk, size_t available, StringPool *pool) {
    uint32_t chunk_size;
    uint16_t header_size;

    if (available < 28 || read_u16(chunk) != RES_STRING_POOL_TYPE) {
        return 0;
    }
    header_size = read_u16(chunk + 2);
    chunk_size = read_u32(chunk + 4);
    if (header_size < 28 || chunk_size > available || chunk_size < header_size) {
        return 0;
    }
    memset(pool, 0, sizeof(*pool));
    pool->chunk = chunk;
    pool->chunk_size = chunk_size;
    pool->header_size = header_size;
    pool->string_count = read_u32(chunk + 8);
    pool->style_count = read_u32(chunk + 12);
    pool->flags = read_u32(chunk + 16);
    pool->strings_start = read_u32(chunk + 20);
    pool->styles_start = read_u32(chunk + 24);
    if ((uint64_t)header_size + ((uint64_t)pool->string_count + pool->style_count) * 4 > chunk_size ||
        pool->strings_start >= chunk_size ||
        (pool->styles_start && pool->styles_start > chunk_size)) {
        return 0;
    }
    return 1;
}

static int pool_string(const StringPool *pool, uint32_t index,
                       char *output, size_t output_size) {
    uint32_t offset;
    const unsigned char *cursor;
    const unsigned char *end = pool->chunk + pool->chunk_size;
    size_t written = 0;

    if (index >= pool->string_count || !output_size) {
        return 0;
    }
    offset = read_u32(pool->chunk + pool->header_size + index * 4u);
    if (offset >= pool->chunk_size - pool->strings_start) {
        return 0;
    }
    cursor = pool->chunk + pool->strings_start + offset;
    if (pool->flags & UTF8_FLAG) {
        uint32_t byte_length;
        if (read_length8(&cursor, end) == UINT32_MAX ||
            (byte_length = read_length8(&cursor, end)) == UINT32_MAX ||
            byte_length >= output_size || (size_t)(end - cursor) <= byte_length) {
            return 0;
        }
        memcpy(output, cursor, byte_length);
        output[byte_length] = '\0';
        return 1;
    }
    {
        uint32_t units = read_length16(&cursor, end);
        uint32_t i;
        if (units == UINT32_MAX || (uint64_t)units * 2 + 2 > (uint64_t)(end - cursor)) {
            return 0;
        }
        for (i = 0; i < units && written + 1 < output_size; i++) {
            uint16_t code = read_u16(cursor + i * 2u);
            if (code < 0x80u) {
                output[written++] = (char)code;
            } else if (code < 0x800u && written + 2 < output_size) {
                output[written++] = (char)(0xc0u | (code >> 6));
                output[written++] = (char)(0x80u | (code & 0x3fu));
            } else if (written + 3 < output_size) {
                output[written++] = (char)(0xe0u | (code >> 12));
                output[written++] = (char)(0x80u | ((code >> 6) & 0x3fu));
                output[written++] = (char)(0x80u | (code & 0x3fu));
            } else {
                return 0;
            }
        }
        output[written] = '\0';
        return i == units;
    }
}

static uint32_t editor_find(StringEditor *editor, const char *value) {
    uint32_t i;
    char decoded[512];

    for (i = 0; i < editor->pool.string_count; i++) {
        if (pool_string(&editor->pool, i, decoded, sizeof(decoded)) &&
            strcmp(decoded, value) == 0) {
            return i;
        }
    }
    for (i = 0; i < (uint32_t)editor->added_count; i++) {
        if (strcmp(editor->added[i].value, value) == 0) {
            return editor->added[i].index;
        }
    }
    return NO_INDEX;
}

static uint32_t editor_add(StringEditor *editor, const char *value) {
    uint32_t existing = editor_find(editor, value);
    AddedString *added;
    size_t length;

    if (existing != NO_INDEX) {
        return existing;
    }
    length = strlen(value);
    if (editor->added_count >= MAX_NEW_STRINGS ||
        length >= sizeof(editor->added[0].value)) {
        return NO_INDEX;
    }
    added = &editor->added[editor->added_count];
    memcpy(added->value, value, length + 1);
    added->index = editor->pool.string_count + (uint32_t)editor->added_count;
    editor->added_count++;
    return added->index;
}

static size_t encoded_string_size(const StringPool *pool, const char *value) {
    size_t length = strlen(value);
    if (pool->flags & UTF8_FLAG) {
        return (length < 128 ? 1 : 2) * 2 + length + 1;
    }
    return (length < 0x8000 ? 2 : 4) + length * 2 + 2;
}

static int append_length8(Buffer *buffer, size_t value) {
    unsigned char bytes[2];
    if (value > 0x7fffu) {
        return 0;
    }
    if (value < 0x80u) {
        bytes[0] = (unsigned char)value;
        return append(buffer, bytes, 1);
    }
    bytes[0] = (unsigned char)(0x80u | (value >> 8));
    bytes[1] = (unsigned char)value;
    return append(buffer, bytes, 2);
}

static int append_encoded_string(Buffer *buffer, const StringPool *pool, const char *value) {
    size_t length = strlen(value);
    size_t i;
    unsigned char zero[2] = {0, 0};

    if (pool->flags & UTF8_FLAG) {
        return append_length8(buffer, length) && append_length8(buffer, length) &&
               append(buffer, value, length) && append(buffer, zero, 1);
    }
    if (length >= 0x8000u) {
        return 0;
    }
    {
        unsigned char length_bytes[2];
        write_u16(length_bytes, (uint16_t)length);
        if (!append(buffer, length_bytes, 2)) {
            return 0;
        }
    }
    for (i = 0; i < length; i++) {
        unsigned char unit[2];
        if ((unsigned char)value[i] >= 0x80u) {
            return 0;
        }
        write_u16(unit, (uint16_t)(unsigned char)value[i]);
        if (!append(buffer, unit, 2)) {
            return 0;
        }
    }
    return append(buffer, zero, 2);
}

static size_t pool_used_string_bytes(const StringPool *pool) {
    const unsigned char *strings = pool->chunk + pool->strings_start;
    const unsigned char *limit = pool->chunk +
        (pool->styles_start ? pool->styles_start : pool->chunk_size);
    size_t used = 0;
    uint32_t i;

    for (i = 0; i < pool->string_count; i++) {
        uint32_t offset = read_u32(pool->chunk + pool->header_size + i * 4u);
        const unsigned char *cursor;
        size_t end_offset;
        if (offset >= (uint32_t)(limit - strings)) {
            return 0;
        }
        cursor = strings + offset;
        if (pool->flags & UTF8_FLAG) {
            uint32_t bytes;
            if (read_length8(&cursor, limit) == UINT32_MAX ||
                (bytes = read_length8(&cursor, limit)) == UINT32_MAX ||
                (uint64_t)bytes + 1 > (uint64_t)(limit - cursor)) {
                return 0;
            }
            cursor += bytes + 1;
        } else {
            uint32_t units = read_length16(&cursor, limit);
            if (units == UINT32_MAX || (uint64_t)units * 2 + 2 > (uint64_t)(limit - cursor)) {
                return 0;
            }
            cursor += units * 2u + 2u;
        }
        end_offset = (size_t)(cursor - strings);
        if (end_offset > used) {
            used = end_offset;
        }
    }
    return used;
}

static int build_string_pool(StringEditor *editor, unsigned char **output, size_t *output_size) {
    const StringPool *pool = &editor->pool;
    Buffer buffer = {0};
    uint32_t new_count = pool->string_count + (uint32_t)editor->added_count;
    size_t used = pool_used_string_bytes(pool);
    size_t string_bytes = used;
    uint32_t strings_start;
    uint32_t styles_start = 0;
    size_t styles_size = pool->styles_start ? pool->chunk_size - pool->styles_start : 0;
    size_t i;

    if (!used && pool->string_count) {
        return 0;
    }
    for (i = 0; i < editor->added_count; i++) {
        string_bytes += encoded_string_size(pool, editor->added[i].value);
    }
    strings_start = pool->header_size + (new_count + pool->style_count) * 4u;
    if (styles_size) {
        styles_start = strings_start + (uint32_t)((string_bytes + 3u) & ~3u);
    }
    if (!append(&buffer, pool->chunk, pool->header_size) ||
        !append(&buffer, pool->chunk + pool->header_size,
                pool->string_count * 4u)) {
        goto fail;
    }
    for (i = 0; i < editor->added_count; i++) {
        unsigned char offset_bytes[4];
        write_u32(offset_bytes, (uint32_t)used);
        if (!append(&buffer, offset_bytes, 4)) {
            goto fail;
        }
        used += encoded_string_size(pool, editor->added[i].value);
    }
    if (pool->style_count && !append(&buffer,
            pool->chunk + pool->header_size + pool->string_count * 4u,
            pool->style_count * 4u)) {
        goto fail;
    }
    if (!append(&buffer, pool->chunk + pool->strings_start,
                pool_used_string_bytes(pool))) {
        goto fail;
    }
    for (i = 0; i < editor->added_count; i++) {
        if (!append_encoded_string(&buffer, pool, editor->added[i].value)) {
            goto fail;
        }
    }
    while (buffer.size % 4u) {
        if (!append_zeroes(&buffer, 1)) {
            goto fail;
        }
    }
    if (styles_size && !append(&buffer, pool->chunk + pool->styles_start, styles_size)) {
        goto fail;
    }
    write_u32(buffer.data + 4, (uint32_t)buffer.size);
    write_u32(buffer.data + 8, new_count);
    write_u32(buffer.data + 20, strings_start);
    write_u32(buffer.data + 24, styles_start);
    *output = buffer.data;
    *output_size = buffer.size;
    return 1;

fail:
    free(buffer.data);
    return 0;
}

static int attribute_compare(const void *left, const void *right) {
    const XmlAttribute *a = (const XmlAttribute *)left;
    const XmlAttribute *b = (const XmlAttribute *)right;
    return a->resource_id < b->resource_id ? -1 : a->resource_id > b->resource_id;
}

static int append_start_element(Buffer *buffer, uint32_t name,
                                XmlAttribute *attributes, size_t attribute_count) {
    size_t chunk_size = 36u + attribute_count * 20u;
    unsigned char *chunk;
    size_t i;

    if (chunk_size > UINT32_MAX || !append_zeroes(buffer, chunk_size)) {
        return 0;
    }
    chunk = buffer->data + buffer->size - chunk_size;
    write_u16(chunk, RES_XML_START_ELEMENT_TYPE);
    write_u16(chunk + 2, 16);
    write_u32(chunk + 4, (uint32_t)chunk_size);
    write_u32(chunk + 8, 0);
    write_u32(chunk + 12, NO_INDEX);
    write_u32(chunk + 16, NO_INDEX);
    write_u32(chunk + 20, name);
    write_u16(chunk + 24, 20);
    write_u16(chunk + 26, 20);
    write_u16(chunk + 28, (uint16_t)attribute_count);
    qsort(attributes, attribute_count, sizeof(*attributes), attribute_compare);
    for (i = 0; i < attribute_count; i++) {
        unsigned char *target = chunk + 36u + i * 20u;
        write_u32(target, attributes[i].namespace_index);
        write_u32(target + 4, attributes[i].name_index);
        write_u32(target + 8, attributes[i].raw_index);
        write_u16(target + 12, 8);
        target[14] = 0;
        target[15] = attributes[i].type;
        write_u32(target + 16, attributes[i].value);
    }
    return 1;
}

static int append_end_element(Buffer *buffer, uint32_t name) {
    unsigned char chunk[24] = {0};
    write_u16(chunk, RES_XML_END_ELEMENT_TYPE);
    write_u16(chunk + 2, 16);
    write_u32(chunk + 4, sizeof(chunk));
    write_u32(chunk + 8, 0);
    write_u32(chunk + 12, NO_INDEX);
    write_u32(chunk + 16, NO_INDEX);
    write_u32(chunk + 20, name);
    return append(buffer, chunk, sizeof(chunk));
}

static XmlAttribute string_attribute(uint32_t ns, uint32_t name, uint32_t value,
                                     uint32_t resource_id) {
    XmlAttribute attribute = {ns, name, value, TYPE_STRING, value, resource_id};
    return attribute;
}

static XmlAttribute bool_attribute(uint32_t ns, uint32_t name, int value,
                                   uint32_t resource_id) {
    XmlAttribute attribute = {ns, name, NO_INDEX, TYPE_INT_BOOLEAN,
                              value ? NO_INDEX : 0, resource_id};
    return attribute;
}

static XmlAttribute int_attribute(uint32_t ns, uint32_t name, uint32_t value,
                                  uint32_t resource_id) {
    XmlAttribute attribute = {ns, name, NO_INDEX, TYPE_INT_DEC, value, resource_id};
    return attribute;
}

static int append_bootstrap_nodes(Buffer *buffer, uint32_t android_ns,
                                  uint32_t provider_tag, uint32_t service_tag,
                                  uint32_t metadata_tag, uint32_t name_attr,
                                  uint32_t enabled_attr, uint32_t exported_attr,
                                  uint32_t authorities_attr, uint32_t init_order_attr,
                                  uint32_t value_attr, uint32_t provider_class,
                                  uint32_t service_class, uint32_t authority,
                                  uint32_t metadata_name, uint32_t library_name) {
    XmlAttribute provider[5];
    XmlAttribute service[3];
    XmlAttribute metadata[2];

    provider[0] = string_attribute(android_ns, name_attr, provider_class, ATTR_NAME);
    provider[1] = bool_attribute(android_ns, enabled_attr, 1, ATTR_ENABLED);
    provider[2] = bool_attribute(android_ns, exported_attr, 0, ATTR_EXPORTED);
    provider[3] = string_attribute(android_ns, authorities_attr, authority, ATTR_AUTHORITIES);
    provider[4] = int_attribute(android_ns, init_order_attr, 100, ATTR_INIT_ORDER);
    service[0] = string_attribute(android_ns, name_attr, service_class, ATTR_NAME);
    service[1] = bool_attribute(android_ns, enabled_attr, 1, ATTR_ENABLED);
    service[2] = bool_attribute(android_ns, exported_attr, 0, ATTR_EXPORTED);
    metadata[0] = string_attribute(android_ns, name_attr, metadata_name, ATTR_NAME);
    metadata[1] = string_attribute(android_ns, value_attr, library_name, ATTR_VALUE);

    return append_start_element(buffer, metadata_tag, metadata, 2) &&
           append_end_element(buffer, metadata_tag) &&
           append_start_element(buffer, provider_tag, provider, 5) &&
           append_end_element(buffer, provider_tag) &&
           append_start_element(buffer, service_tag, service, 3) &&
           append_end_element(buffer, service_tag);
}

static uint32_t resource_id_at(const unsigned char *map, size_t map_size, uint32_t string_index) {
    uint16_t header_size;
    size_t count;
    if (!map || map_size < 8) {
        return 0;
    }
    header_size = read_u16(map + 2);
    if (header_size < 8 || header_size > map_size) {
        return 0;
    }
    count = (map_size - header_size) / 4u;
    return string_index < count ? read_u32(map + header_size + string_index * 4u) : 0;
}

static int build_resource_map(const unsigned char *old_map, size_t old_size,
                              uint32_t needed_count,
                              const uint32_t *indices, const uint32_t *ids,
                              size_t mapping_count,
                              unsigned char **output, size_t *output_size) {
    uint16_t header_size = old_map ? read_u16(old_map + 2) : 8;
    size_t old_count = old_map && old_size >= header_size ? (old_size - header_size) / 4u : 0;
    size_t count = old_count > needed_count ? old_count : needed_count;
    size_t size = header_size + count * 4u;
    unsigned char *map = (unsigned char *)calloc(1, size);
    size_t i;

    if (!map || size > UINT32_MAX) {
        free(map);
        return 0;
    }
    if (old_map) {
        memcpy(map, old_map, old_size);
    } else {
        write_u16(map, RES_XML_RESOURCE_MAP_TYPE);
        write_u16(map + 2, 8);
    }
    write_u32(map + 4, (uint32_t)size);
    for (i = 0; i < mapping_count; i++) {
        write_u32(map + header_size + indices[i] * 4u, ids[i]);
    }
    *output = map;
    *output_size = size;
    return 1;
}

static int element_name_equals(const StringPool *pool, const unsigned char *chunk,
                               const char *expected) {
    char name[128];
    return pool_string(pool, read_u32(chunk + 20), name, sizeof(name)) &&
           strcmp(name, expected) == 0;
}

static int element_has_class(const StringPool *pool, const unsigned char *chunk,
                             size_t chunk_size, const char *class_name) {
    uint16_t attribute_start;
    uint16_t attribute_size;
    uint16_t count;
    size_t offset;
    uint16_t i;

    if (chunk_size < 36) {
        return 0;
    }
    attribute_start = read_u16(chunk + 24);
    attribute_size = read_u16(chunk + 26);
    count = read_u16(chunk + 28);
    offset = 16u + attribute_start;
    if (attribute_size < 20 || offset + (size_t)count * attribute_size > chunk_size) {
        return 0;
    }
    for (i = 0; i < count; i++) {
        const unsigned char *attribute = chunk + offset + i * attribute_size;
        char name[64];
        char value[512];
        uint32_t raw = read_u32(attribute + 8);
        if (pool_string(pool, read_u32(attribute + 4), name, sizeof(name)) &&
            strcmp(name, "name") == 0 && raw != NO_INDEX &&
            pool_string(pool, raw, value, sizeof(value)) && strcmp(value, class_name) == 0) {
            return 1;
        }
    }
    return 0;
}

static int build_debuggable_application(const unsigned char *chunk, size_t chunk_size,
                                        const StringPool *pool, const unsigned char *resource_map,
                                        size_t resource_map_size, uint32_t android_ns,
                                        uint32_t debuggable_name,
                                        unsigned char **output, size_t *output_size) {
    uint16_t attribute_start = read_u16(chunk + 24);
    uint16_t attribute_size = read_u16(chunk + 26);
    uint16_t count = read_u16(chunk + 28);
    size_t offset = 16u + attribute_start;
    XmlAttribute *attributes;
    size_t i;
    int found = 0;
    Buffer buffer = {0};

    if (attribute_size < 20 || offset + (size_t)count * attribute_size > chunk_size) {
        return 0;
    }
    attributes = (XmlAttribute *)calloc((size_t)count + 1, sizeof(*attributes));
    if (!attributes) {
        return 0;
    }
    for (i = 0; i < count; i++) {
        const unsigned char *source = chunk + offset + i * attribute_size;
        attributes[i].namespace_index = read_u32(source);
        attributes[i].name_index = read_u32(source + 4);
        attributes[i].raw_index = read_u32(source + 8);
        attributes[i].type = source[15];
        attributes[i].value = read_u32(source + 16);
        attributes[i].resource_id = resource_id_at(resource_map, resource_map_size,
                                                   attributes[i].name_index);
        if (attributes[i].resource_id == ATTR_DEBUGGABLE) {
            attributes[i] = bool_attribute(android_ns, debuggable_name, 1, ATTR_DEBUGGABLE);
            found = 1;
        }
    }
    if (!found) {
        attributes[count++] = bool_attribute(android_ns, debuggable_name, 1, ATTR_DEBUGGABLE);
    }
    if (!append_start_element(&buffer, read_u32(chunk + 20), attributes, count)) {
        free(attributes);
        return 0;
    }
    free(attributes);
    write_u32(buffer.data + 8, read_u32(chunk + 8));
    write_u32(buffer.data + 12, read_u32(chunk + 12));
    write_u32(buffer.data + 16, read_u32(chunk + 16));
    *output = buffer.data;
    *output_size = buffer.size;
    (void)pool;
    return 1;
}

int fpatch_axml_add_bootstrap(const unsigned char *data, size_t size,
                              const char *package_name,
                              const char *runtime_library,
                              const char *bootstrap_language,
                              int make_debuggable,
                              unsigned char **output, size_t *output_size,
                              char *error, size_t error_size) {
    uint16_t root_header_size;
    uint32_t root_size;
    size_t offset;
    size_t pool_offset = SIZE_MAX;
    size_t map_offset = SIZE_MAX;
    size_t map_size = 0;
    size_t app_start_offset = SIZE_MAX;
    size_t app_end_offset = SIZE_MAX;
    int depth = 0;
    int app_depth = -1;
    StringEditor editor;
    char authority_text[512];
    const char *provider_class_text;
    const char *service_class_text;
    uint32_t android_ns;
    uint32_t provider_tag, service_tag, metadata_tag;
    uint32_t name_attr, enabled_attr, exported_attr, authorities_attr, init_order_attr;
    uint32_t value_attr, debuggable_attr;
    uint32_t provider_class, service_class, authority, metadata_name, library_name;
    uint32_t mapped_indices[7];
    uint32_t mapped_ids[7] = {
        ATTR_NAME, ATTR_ENABLED, ATTR_DEBUGGABLE, ATTR_EXPORTED,
        ATTR_AUTHORITIES, ATTR_INIT_ORDER, ATTR_VALUE
    };
    unsigned char *new_pool = NULL;
    size_t new_pool_size = 0;
    unsigned char *new_map = NULL;
    size_t new_map_size = 0;
    unsigned char *new_application = NULL;
    size_t new_application_size = 0;
    Buffer nodes = {0};
    Buffer result = {0};

    *output = NULL;
    *output_size = 0;
    if (!data || size < 8 || read_u16(data) != RES_XML_TYPE) {
        set_error(error, error_size, "AndroidManifest.xml is not binary Android XML.");
        return 0;
    }
    root_header_size = read_u16(data + 2);
    root_size = read_u32(data + 4);
    if (root_header_size < 8 || root_size > size || root_size < root_header_size) {
        set_error(error, error_size, "AndroidManifest.xml has an invalid root chunk.");
        return 0;
    }
    memset(&editor, 0, sizeof(editor));
    offset = root_header_size;
    while (offset + 8 <= root_size) {
        uint16_t type = read_u16(data + offset);
        uint32_t chunk_size = read_u32(data + offset + 4);
        if (chunk_size < 8 || chunk_size > root_size - offset) {
            set_error(error, error_size, "AndroidManifest.xml contains an invalid chunk.");
            return 0;
        }
        if (type == RES_STRING_POOL_TYPE && pool_offset == SIZE_MAX) {
            pool_offset = offset;
            if (!parse_pool(data + offset, chunk_size, &editor.pool)) {
                set_error(error, error_size, "AndroidManifest.xml string pool is invalid.");
                return 0;
            }
        } else if (type == RES_XML_RESOURCE_MAP_TYPE && map_offset == SIZE_MAX) {
            map_offset = offset;
            map_size = chunk_size;
        }
        offset += chunk_size;
    }
    if (pool_offset == SIZE_MAX) {
        set_error(error, error_size, "AndroidManifest.xml has no string pool.");
        return 0;
    }

    provider_class_text = strcmp(bootstrap_language, "kotlin") == 0
        ? "dev.falconpatch.runtime.FalconPatchKotlinProvider"
        : "dev.falconpatch.runtime.FalconPatchProvider";
    service_class_text = strcmp(bootstrap_language, "kotlin") == 0
        ? "dev.falconpatch.runtime.FalconPatchKotlinService"
        : "dev.falconpatch.runtime.FalconPatchService";
    {
        int authority_length = snprintf(authority_text, sizeof(authority_text),
                                        "%s.falconpatch.init", package_name);
        if (authority_length < 0 || (size_t)authority_length >= sizeof(authority_text)) {
            set_error(error, error_size, "Package name is too long for a provider authority.");
            return 0;
        }
    }

    android_ns = editor_find(&editor, "http://schemas.android.com/apk/res/android");
    if (android_ns == NO_INDEX) {
        set_error(error, error_size, "AndroidManifest.xml has no Android XML namespace.");
        return 0;
    }
    provider_tag = editor_add(&editor, "provider");
    service_tag = editor_add(&editor, "service");
    metadata_tag = editor_add(&editor, "meta-data");
    name_attr = editor_add(&editor, "name");
    enabled_attr = editor_add(&editor, "enabled");
    debuggable_attr = editor_add(&editor, "debuggable");
    exported_attr = editor_add(&editor, "exported");
    authorities_attr = editor_add(&editor, "authorities");
    init_order_attr = editor_add(&editor, "initOrder");
    value_attr = editor_add(&editor, "value");
    provider_class = editor_add(&editor, provider_class_text);
    service_class = editor_add(&editor, service_class_text);
    authority = editor_add(&editor, authority_text);
    metadata_name = editor_add(&editor, "dev.falconpatch.library");
    library_name = editor_add(&editor, runtime_library);
    if (library_name == NO_INDEX) {
        set_error(error, error_size, "AndroidManifest.xml needs too many new strings.");
        return 0;
    }

    offset = root_header_size;
    while (offset + 8 <= root_size) {
        uint16_t type = read_u16(data + offset);
        uint32_t chunk_size = read_u32(data + offset + 4);
        const unsigned char *chunk = data + offset;
        if (type == RES_XML_START_ELEMENT_TYPE && chunk_size >= 24) {
            if (element_name_equals(&editor.pool, chunk, "application") && app_start_offset == SIZE_MAX) {
                app_start_offset = offset;
                app_depth = depth;
            }
            if ((element_name_equals(&editor.pool, chunk, "provider") ||
                 element_name_equals(&editor.pool, chunk, "service")) &&
                (element_has_class(&editor.pool, chunk, chunk_size,
                                   "dev.falconpatch.runtime.FalconPatchProvider") ||
                 element_has_class(&editor.pool, chunk, chunk_size,
                                   "dev.falconpatch.runtime.FalconPatchKotlinProvider") ||
                 element_has_class(&editor.pool, chunk, chunk_size,
                                   "dev.falconpatch.runtime.FalconPatchService") ||
                 element_has_class(&editor.pool, chunk, chunk_size,
                                   "dev.falconpatch.runtime.FalconPatchKotlinService"))) {
                set_error(error, error_size, "FalconPatch bootstrap is already present.");
                return 0;
            }
            depth++;
        } else if (type == RES_XML_END_ELEMENT_TYPE && chunk_size >= 24) {
            depth--;
            if (app_depth >= 0 && depth == app_depth &&
                element_name_equals(&editor.pool, chunk, "application")) {
                app_end_offset = offset;
                break;
            }
        }
        offset += chunk_size;
    }
    if (app_start_offset == SIZE_MAX || app_end_offset == SIZE_MAX) {
        set_error(error, error_size, "AndroidManifest.xml has no patchable application element.");
        return 0;
    }

    mapped_indices[0] = name_attr;
    mapped_indices[1] = enabled_attr;
    mapped_indices[2] = debuggable_attr;
    mapped_indices[3] = exported_attr;
    mapped_indices[4] = authorities_attr;
    mapped_indices[5] = init_order_attr;
    mapped_indices[6] = value_attr;
    if (!build_string_pool(&editor, &new_pool, &new_pool_size) ||
        !build_resource_map(map_offset == SIZE_MAX ? NULL : data + map_offset, map_size,
                            editor.pool.string_count + (uint32_t)editor.added_count,
                            mapped_indices, mapped_ids, 7, &new_map, &new_map_size) ||
        !append_bootstrap_nodes(&nodes, android_ns, provider_tag, service_tag, metadata_tag,
                                name_attr, enabled_attr, exported_attr, authorities_attr,
                                init_order_attr, value_attr, provider_class, service_class,
                                authority, metadata_name, library_name)) {
        set_error(error, error_size, "Out of memory while patching AndroidManifest.xml.");
        goto fail;
    }
    if (make_debuggable) {
        uint32_t app_chunk_size = read_u32(data + app_start_offset + 4);
        if (!build_debuggable_application(data + app_start_offset, app_chunk_size,
                                          &editor.pool,
                                          map_offset == SIZE_MAX ? NULL : data + map_offset,
                                          map_size, android_ns, debuggable_attr,
                                          &new_application, &new_application_size)) {
            set_error(error, error_size, "Cannot add the debuggable application attribute.");
            goto fail;
        }
    }

    if (!append(&result, data, root_header_size)) {
        goto memory_fail;
    }
    offset = root_header_size;
    while (offset + 8 <= root_size) {
        uint16_t type = read_u16(data + offset);
        uint32_t chunk_size = read_u32(data + offset + 4);
        if (offset == app_end_offset && !append(&result, nodes.data, nodes.size)) {
            goto memory_fail;
        }
        if (offset == pool_offset) {
            if (!append(&result, new_pool, new_pool_size) ||
                (map_offset == SIZE_MAX && !append(&result, new_map, new_map_size))) {
                goto memory_fail;
            }
        } else if (offset == map_offset) {
            if (!append(&result, new_map, new_map_size)) {
                goto memory_fail;
            }
        } else if (offset == app_start_offset && new_application) {
            if (!append(&result, new_application, new_application_size)) {
                goto memory_fail;
            }
        } else if (!append(&result, data + offset, chunk_size)) {
            goto memory_fail;
        }
        (void)type;
        offset += chunk_size;
    }
    if (result.size > UINT32_MAX) {
        set_error(error, error_size, "Patched AndroidManifest.xml is too large.");
        goto fail;
    }
    write_u32(result.data + 4, (uint32_t)result.size);
    *output = result.data;
    *output_size = result.size;
    free(new_pool);
    free(new_map);
    free(new_application);
    free(nodes.data);
    return 1;

memory_fail:
    set_error(error, error_size, "Out of memory while rebuilding AndroidManifest.xml.");
fail:
    free(new_pool);
    free(new_map);
    free(new_application);
    free(nodes.data);
    free(result.data);
    return 0;
}
