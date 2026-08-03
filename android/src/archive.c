#include "fp_internal.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

uint16_t fp_read_u16(const unsigned char *data) {
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

uint32_t fp_read_u32(const unsigned char *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

uint64_t fp_read_u64(const unsigned char *data) {
    return (uint64_t)fp_read_u32(data) | ((uint64_t)fp_read_u32(data + 4) << 32);
}

static int take_bytes(const unsigned char **cursor, const unsigned char *end,
                      size_t count, const unsigned char **value) {
    if ((size_t)(end - *cursor) < count) {
        return 0;
    }
    *value = *cursor;
    *cursor += count;
    return 1;
}

static char *copy_string(const unsigned char *data, size_t size) {
    char *result;

    if (size == SIZE_MAX) {
        return NULL;
    }
    result = (char *)malloc(size + 1);
    if (!result) {
        return NULL;
    }
    memcpy(result, data, size);
    result[size] = '\0';
    return result;
}

int fp_archive_parse(unsigned char *storage, size_t size, FpArchive *archive) {
    const unsigned char *cursor = storage;
    const unsigned char *end = storage + size;
    const unsigned char *bytes;
    uint32_t version;
    uint32_t count;
    size_t i;

    memset(archive, 0, sizeof(*archive));
    if (size < 12 || memcmp(storage, FPATCH_ARCHIVE_MAGIC, 4) != 0) {
        return 0;
    }
    cursor += 4;
    version = fp_read_u32(cursor);
    cursor += 4;
    count = fp_read_u32(cursor);
    cursor += 4;
    if (version != FPATCH_ARCHIVE_VERSION || count > 4096u ||
        count > SIZE_MAX / sizeof(FpArchiveRecord)) {
        return 0;
    }
    archive->records = (FpArchiveRecord *)calloc(count, sizeof(FpArchiveRecord));
    if (count && !archive->records) {
        return 0;
    }
    archive->storage = storage;
    archive->storage_size = size;
    archive->record_count = count;

    for (i = 0; i < count; i++) {
        FpArchiveRecord *record = &archive->records[i];
        uint16_t name_size;
        uint32_t aux_size;
        uint64_t data_size;

        if ((size_t)(end - cursor) < 16) {
            goto fail;
        }
        record->type = cursor[0];
        record->flags = cursor[1];
        name_size = fp_read_u16(cursor + 2);
        aux_size = fp_read_u32(cursor + 4);
        data_size = fp_read_u64(cursor + 8);
        cursor += 16;
        if (data_size > SIZE_MAX) {
            goto fail;
        }
        if (!take_bytes(&cursor, end, name_size, &bytes)) {
            goto fail;
        }
        record->name = copy_string(bytes, name_size);
        if (!record->name || !take_bytes(&cursor, end, aux_size, &bytes)) {
            goto fail;
        }
        record->aux = copy_string(bytes, aux_size);
        if (!record->aux || !take_bytes(&cursor, end, (size_t)data_size, &bytes)) {
            goto fail;
        }
        record->data = (unsigned char *)bytes;
        record->size = (size_t)data_size;
    }
    if (cursor != end) {
        goto fail;
    }
    return 1;

fail:
    archive->storage = NULL;
    fp_archive_free(archive);
    return 0;
}

void fp_archive_free(FpArchive *archive) {
    size_t i;

    if (!archive) {
        return;
    }
    for (i = 0; i < archive->record_count; i++) {
        free(archive->records[i].name);
        free(archive->records[i].aux);
    }
    free(archive->records);
    free(archive->storage);
    memset(archive, 0, sizeof(*archive));
}
