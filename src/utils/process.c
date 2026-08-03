#include "utils/process.h"
#include "utils/file_utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

static void set_error(char *error, size_t error_size, const char *format,
                      const char *value) {
    if (error && error_size) {
        snprintf(error, error_size, format, value ? value : "");
    }
}

int fpatch_find_executable(const char *name, char *output, size_t output_size) {
#ifdef _WIN32
    static const char *extensions[] = {".exe", ".com", NULL};
    size_t i;
    for (i = 0; extensions[i]; i++) {
        DWORD length = SearchPathA(NULL, name, extensions[i], (DWORD)output_size, output, NULL);
        if (length > 0 && length < output_size) {
            return 1;
        }
    }
    return 0;
#else
    const char *path = getenv("PATH");
    const char *cursor;
    if (!path) {
        return 0;
    }
    cursor = path;
    while (*cursor) {
        const char *end = strchr(cursor, ':');
        size_t length = end ? (size_t)(end - cursor) : strlen(cursor);
        int written = snprintf(output, output_size, "%.*s/%s", (int)length, cursor, name);
        if (written > 0 && (size_t)written < output_size && access(output, X_OK) == 0) {
            return 1;
        }
        if (!end) {
            break;
        }
        cursor = end + 1;
    }
    return 0;
#endif
}

static int version_compare(const char *left, const char *right) {
    const char *a = left;
    const char *b = right;
    while (*a || *b) {
        unsigned long av = 0;
        unsigned long bv = 0;
        while (*a >= '0' && *a <= '9') {
            av = av * 10 + (unsigned long)(*a++ - '0');
        }
        while (*b >= '0' && *b <= '9') {
            bv = bv * 10 + (unsigned long)(*b++ - '0');
        }
        if (av != bv) {
            return av > bv ? 1 : -1;
        }
        while (*a && *a != '.') {
            a++;
        }
        while (*b && *b != '.') {
            b++;
        }
        if (*a == '.') a++;
        if (*b == '.') b++;
    }
    return 0;
}

static int android_sdk_root(char *output, size_t output_size) {
    const char *root = getenv("ANDROID_SDK_ROOT");
    if (!root || !root[0]) {
        root = getenv("ANDROID_HOME");
    }
#ifdef _WIN32
    if (!root || !root[0]) {
        const char *local = getenv("LOCALAPPDATA");
        if (local && fpatch_path_join(output, output_size, local, "Android/Sdk") &&
            fpatch_directory_exists(output)) {
            return 1;
        }
    }
#endif
    if (root && strlen(root) < output_size && fpatch_directory_exists(root)) {
        snprintf(output, output_size, "%s", root);
        return 1;
    }
    return 0;
}

static int latest_build_tools(char *output, size_t output_size) {
    char sdk[1024];
    char parent[1024];
    char best[128] = "";

    if (!android_sdk_root(sdk, sizeof(sdk)) ||
        !fpatch_path_join(parent, sizeof(parent), sdk, "build-tools")) {
        return 0;
    }
#ifdef _WIN32
    {
        WIN32_FIND_DATAA entry;
        char pattern[1100];
        HANDLE handle;
        if (!fpatch_path_join(pattern, sizeof(pattern), parent, "*")) {
            return 0;
        }
        handle = FindFirstFileA(pattern, &entry);
        if (handle == INVALID_HANDLE_VALUE) {
            return 0;
        }
        do {
            if ((entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
                entry.cFileName[0] != '.' &&
                (!best[0] || version_compare(entry.cFileName, best) > 0)) {
                snprintf(best, sizeof(best), "%s", entry.cFileName);
            }
        } while (FindNextFileA(handle, &entry));
        FindClose(handle);
    }
#else
    {
        DIR *directory = opendir(parent);
        struct dirent *entry;
        if (!directory) {
            return 0;
        }
        while ((entry = readdir(directory)) != NULL) {
            char candidate[1100];
            if (entry->d_name[0] == '.' ||
                !fpatch_path_join(candidate, sizeof(candidate), parent, entry->d_name) ||
                !fpatch_directory_exists(candidate)) {
                continue;
            }
            if (!best[0] || version_compare(entry->d_name, best) > 0) {
                snprintf(best, sizeof(best), "%s", entry->d_name);
            }
        }
        closedir(directory);
    }
#endif
    return best[0] && fpatch_path_join(output, output_size, parent, best);
}

int fpatch_find_android_build_tool(const char *name, char *output, size_t output_size) {
    char directory[1024];
    char filename[256];

    if (!latest_build_tools(directory, sizeof(directory))) {
        return 0;
    }
#ifdef _WIN32
    snprintf(filename, sizeof(filename), "%s.exe", name);
#else
    snprintf(filename, sizeof(filename), "%s", name);
#endif
    return fpatch_path_join(output, output_size, directory, filename) &&
           fpatch_file_exists(output);
}

int fpatch_find_apksigner_jar(char *output, size_t output_size) {
    char directory[1024];
    if (!latest_build_tools(directory, sizeof(directory)) ||
        !fpatch_path_join(output, output_size, directory, "lib/apksigner.jar")) {
        return 0;
    }
    return fpatch_file_exists(output);
}

#ifdef _WIN32
static int append_argument(char *command, size_t command_size, size_t *offset,
                           const char *argument) {
    size_t backslashes = 0;
    const char *cursor;

    if (*offset && *offset + 1 < command_size) {
        command[(*offset)++] = ' ';
    }
    if (*offset + 1 >= command_size) {
        return 0;
    }
    command[(*offset)++] = '"';
    for (cursor = argument; ; cursor++) {
        if (*cursor == '\\') {
            backslashes++;
            continue;
        }
        if (*cursor == '"' || *cursor == '\0') {
            size_t needed = backslashes * 2 + (*cursor == '"' ? 1 : 0);
            if (*offset + needed + 2 >= command_size) {
                return 0;
            }
            while (backslashes > 0) {
                command[(*offset)++] = '\\';
                command[(*offset)++] = '\\';
                backslashes--;
            }
            if (*cursor == '"') {
                command[(*offset)++] = '\\';
                command[(*offset)++] = '"';
                backslashes = 0;
                continue;
            }
            command[(*offset)++] = '"';
            command[*offset] = '\0';
            return 1;
        }
        if (*offset + backslashes + 2 >= command_size) {
            return 0;
        }
        while (backslashes) {
            command[(*offset)++] = '\\';
            backslashes--;
        }
        command[(*offset)++] = *cursor;
    }
}
#endif

int fpatch_run_process(const char *executable, const char *const argv[],
                       char *error, size_t error_size) {
#ifdef _WIN32
    STARTUPINFOA startup;
    PROCESS_INFORMATION process;
    char command[32768] = "";
    size_t offset = 0;
    size_t i;
    DWORD exit_code;

    memset(&startup, 0, sizeof(startup));
    memset(&process, 0, sizeof(process));
    startup.cb = sizeof(startup);
    for (i = 0; argv[i]; i++) {
        if (!append_argument(command, sizeof(command), &offset, argv[i])) {
            set_error(error, error_size, "Process command is too long: %s", executable);
            return 0;
        }
    }
    if (!CreateProcessA(executable, command, NULL, NULL, TRUE, 0, NULL, NULL,
                        &startup, &process)) {
        set_error(error, error_size, "Cannot start process: %s", executable);
        return 0;
    }
    WaitForSingleObject(process.hProcess, INFINITE);
    if (!GetExitCodeProcess(process.hProcess, &exit_code)) {
        exit_code = 1;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    if (exit_code != 0) {
        set_error(error, error_size, "Process failed: %s", executable);
        return 0;
    }
    return 1;
#else
    pid_t child = fork();
    int status;
    if (child < 0) {
        set_error(error, error_size, "Cannot start process: %s", executable);
        return 0;
    }
    if (child == 0) {
        execv(executable, (char *const *)argv);
        _exit(127);
    }
    if (waitpid(child, &status, 0) < 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        set_error(error, error_size, "Process failed: %s", executable);
        return 0;
    }
    return 1;
#endif
}
