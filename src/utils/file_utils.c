#include "utils/file_utils.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#define FPATCH_MKDIR(path) _mkdir(path)
#else
#include <unistd.h>
#define FPATCH_MKDIR(path) mkdir(path, 0755)
#endif

static void set_error(char *error, size_t error_size, const char *format,
                      const char *path) {
    if (error && error_size) {
        snprintf(error, error_size, format, path ? path : "");
    }
}

int fpatch_file_exists(const char *path) {
    struct stat info;
    if (!path || stat(path, &info) != 0) {
        return 0;
    }
#ifdef _WIN32
    return (info.st_mode & _S_IFMT) == _S_IFREG;
#else
    return S_ISREG(info.st_mode);
#endif
}

int fpatch_directory_exists(const char *path) {
    struct stat info;
    if (!path || stat(path, &info) != 0) {
        return 0;
    }
#ifdef _WIN32
    return (info.st_mode & _S_IFMT) == _S_IFDIR;
#else
    return S_ISDIR(info.st_mode);
#endif
}

int fpatch_make_directories(const char *path) {
    char buffer[1024];
    size_t i;
    size_t length;

    if (!path || !path[0]) {
        return 0;
    }
    length = strlen(path);
    if (length >= sizeof(buffer)) {
        return 0;
    }
    memcpy(buffer, path, length + 1);
    for (i = 1; i <= length; i++) {
        if (buffer[i] == '/' || buffer[i] == '\\' || buffer[i] == '\0') {
            char saved = buffer[i];
            if (i == 2 && buffer[1] == ':') {
                continue;
            }
            buffer[i] = '\0';
            if (buffer[0] && !fpatch_directory_exists(buffer) &&
                FPATCH_MKDIR(buffer) != 0 && errno != EEXIST) {
                return 0;
            }
            buffer[i] = saved;
        }
    }
    return fpatch_directory_exists(path);
}

int fpatch_read_file(const char *path, unsigned char **data, size_t *size,
                     char *error, size_t error_size) {
    FILE *file;
    long length;

    *data = NULL;
    *size = 0;
    file = fopen(path, "rb");
    if (!file) {
        set_error(error, error_size, "Cannot open file: %s", path);
        return 0;
    }
    if (fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) < 0 ||
        fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        set_error(error, error_size, "Cannot measure file: %s", path);
        return 0;
    }
    if ((unsigned long long)length > (unsigned long long)SIZE_MAX) {
        fclose(file);
        set_error(error, error_size, "File is too large: %s", path);
        return 0;
    }
    *data = (unsigned char *)malloc((size_t)length ? (size_t)length : 1);
    if (!*data) {
        fclose(file);
        set_error(error, error_size, "Out of memory while reading: %s", path);
        return 0;
    }
    if ((size_t)length && fread(*data, 1, (size_t)length, file) != (size_t)length) {
        free(*data);
        *data = NULL;
        fclose(file);
        set_error(error, error_size, "Cannot read file: %s", path);
        return 0;
    }
    fclose(file);
    *size = (size_t)length;
    return 1;
}

int fpatch_write_file(const char *path, const void *data, size_t size,
                      char *error, size_t error_size) {
    FILE *file = fopen(path, "wb");
    if (!file) {
        set_error(error, error_size, "Cannot create file: %s", path);
        return 0;
    }
    if (size && fwrite(data, 1, size, file) != size) {
        fclose(file);
        set_error(error, error_size, "Cannot write file: %s", path);
        return 0;
    }
    if (fclose(file) != 0) {
        set_error(error, error_size, "Cannot finish file: %s", path);
        return 0;
    }
    return 1;
}

int fpatch_copy_file(const char *source, const char *target,
                     char *error, size_t error_size) {
    unsigned char buffer[64 * 1024];
    FILE *input = fopen(source, "rb");
    FILE *output;
    size_t count;

    if (!input) {
        set_error(error, error_size, "Cannot open source file: %s", source);
        return 0;
    }
    output = fopen(target, "wb");
    if (!output) {
        fclose(input);
        set_error(error, error_size, "Cannot create target file: %s", target);
        return 0;
    }
    while ((count = fread(buffer, 1, sizeof(buffer), input)) != 0) {
        if (fwrite(buffer, 1, count, output) != count) {
            fclose(input);
            fclose(output);
            set_error(error, error_size, "Cannot write target file: %s", target);
            return 0;
        }
    }
    if (ferror(input)) {
        fclose(input);
        fclose(output);
        set_error(error, error_size, "Cannot complete file copy: %s", target);
        return 0;
    }
    fclose(input);
    if (fclose(output) != 0) {
        set_error(error, error_size, "Cannot complete file copy: %s", target);
        return 0;
    }
    return 1;
}

int fpatch_replace_file(const char *source, const char *target,
                        char *error, size_t error_size) {
#ifdef _WIN32
    if (!MoveFileExA(source, target, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        set_error(error, error_size, "Cannot replace file: %s", target);
        return 0;
    }
#else
    if (rename(source, target) != 0) {
        set_error(error, error_size, "Cannot replace file: %s", target);
        return 0;
    }
#endif
    return 1;
}

const char *fpatch_path_basename(const char *path) {
    const char *slash = strrchr(path, '/');
    const char *backslash = strrchr(path, '\\');
    const char *result = slash;
    if (!result || (backslash && backslash > result)) {
        result = backslash;
    }
    return result ? result + 1 : path;
}

void fpatch_path_dirname(const char *path, char *output, size_t output_size) {
    const char *base = fpatch_path_basename(path);
    size_t length = (size_t)(base - path);

    while (length > 0 && (path[length - 1] == '/' || path[length - 1] == '\\')) {
        length--;
    }
    if (length == 0) {
        snprintf(output, output_size, ".");
    } else if (length < output_size) {
        memcpy(output, path, length);
        output[length] = '\0';
    } else if (output_size) {
        output[0] = '\0';
    }
}

int fpatch_path_join(char *output, size_t output_size,
                     const char *left, const char *right) {
    int written;
    size_t length = strlen(left);
    const char *separator = length && (left[length - 1] == '/' || left[length - 1] == '\\') ? "" : "/";
    written = snprintf(output, output_size, "%s%s%s", left, separator, right);
    return written >= 0 && (size_t)written < output_size;
}

int fpatch_default_output_path(const char *source, char *output, size_t output_size) {
    const char *extension = strrchr(source, '.');
    size_t prefix = extension && strcmp(extension, ".apk") == 0
        ? (size_t)(extension - source) : strlen(source);
    int written;

    if (prefix >= output_size) {
        return 0;
    }
    written = snprintf(output, output_size, "%.*s-fpatch.apk", (int)prefix, source);
    return written >= 0 && (size_t)written < output_size;
}

int fpatch_random_alpha(char *output, size_t length) {
    static const char alphabet[] = "abcdefghijklmnopqrstuvwxyz";
    static int seeded;
    size_t i;

    if (!output || length == 0) {
        return 0;
    }
    if (!seeded) {
        srand((unsigned int)time(NULL) ^ (unsigned int)(uintptr_t)output);
        seeded = 1;
    }
    for (i = 0; i < length; i++) {
        output[i] = alphabet[rand() % (sizeof(alphabet) - 1)];
    }
    output[length] = '\0';
    return 1;
}

int fpatch_elf_abi(const char *path, char *abi, size_t abi_size,
                   char *error, size_t error_size) {
    unsigned char header[20];
    FILE *file = fopen(path, "rb");
    unsigned int machine;
    const char *name = NULL;

    if (!file || fread(header, 1, sizeof(header), file) != sizeof(header)) {
        if (file) {
            fclose(file);
        }
        set_error(error, error_size, "Cannot read ELF header: %s", path);
        return 0;
    }
    fclose(file);
    if (memcmp(header, "\x7f" "ELF", 4) != 0 || (header[5] != 1 && header[5] != 2)) {
        set_error(error, error_size, "Not a supported ELF library: %s", path);
        return 0;
    }
    machine = header[5] == 1
        ? (unsigned int)header[18] | ((unsigned int)header[19] << 8)
        : ((unsigned int)header[18] << 8) | (unsigned int)header[19];
    switch (machine) {
        case 3: name = "x86"; break;
        case 40: name = "armeabi-v7a"; break;
        case 62: name = "x86_64"; break;
        case 183: name = "arm64-v8a"; break;
        default: break;
    }
    if (!name || strlen(name) >= abi_size) {
        set_error(error, error_size, "Unsupported Android ELF architecture: %s", path);
        return 0;
    }
    snprintf(abi, abi_size, "%s", name);
    return 1;
}
