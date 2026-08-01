#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#define FPATCH_MKDIR(path) _mkdir(path)
#define FPATCH_PATH_SEP "\\"
#else
#include <sys/stat.h>
#include <sys/types.h>
#define FPATCH_MKDIR(path) mkdir(path, 0755)
#define FPATCH_PATH_SEP "/"
#endif

#define FPATCH_VERSION "0.1.0"
#define MANIFEST_NAME "fpatch.json"

typedef struct module_entry {
    char type[8];
    char name[128];
    char path[512];
    char abi[32];
} module_entry;

static void print_usage(void) {
    puts("FalconPatch fpatch " FPATCH_VERSION);
    puts("");
    puts("Developer-owned Android module workflow helper.");
    puts("");
    puts("Usage:");
    puts("  fpatch init [project-dir]");
    puts("  fpatch add-so <name> <path-to-so> [abi]");
    puts("  fpatch add-lua <name> <path-to-lua>");
    puts("  fpatch list");
    puts("  fpatch validate");
    puts("  fpatch example");
    puts("");
    puts("Safety:");
    puts("  Use only with apps you own or are authorized to test.");
    puts("  Keep FalconPatch disabled in production builds.");
}

static int path_exists(const char *path) {
    FILE *file = fopen(path, "rb");
    if (!file) {
        return 0;
    }
    fclose(file);
    return 1;
}

static int mkdir_if_needed(const char *path) {
    if (FPATCH_MKDIR(path) == 0 || errno == EEXIST) {
        return 0;
    }

    fprintf(stderr, "error: could not create directory '%s'\n", path);
    return 1;
}

static int valid_name(const char *name) {
    size_t i;
    size_t len;

    if (!name) {
        return 0;
    }

    len = strlen(name);
    if (len == 0 || len >= 64) {
        return 0;
    }

    for (i = 0; i < len; i++) {
        char c = name[i];
        if (!((c >= 'a' && c <= 'z') ||
              (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') ||
              c == '_' || c == '-' || c == '.')) {
            return 0;
        }
    }

    return 1;
}

static int has_suffix(const char *value, const char *suffix) {
    size_t value_len;
    size_t suffix_len;

    if (!value || !suffix) {
        return 0;
    }

    value_len = strlen(value);
    suffix_len = strlen(suffix);
    if (value_len < suffix_len) {
        return 0;
    }

    return strcmp(value + value_len - suffix_len, suffix) == 0;
}

static void copy_json_string(char *dest, size_t dest_size, const char *start, size_t len) {
    size_t i;
    size_t j = 0;

    if (dest_size == 0) {
        return;
    }

    for (i = 0; i < len && j + 1 < dest_size; i++) {
        if (start[i] == '\\' && i + 1 < len) {
            i++;
        }
        dest[j++] = start[i];
    }
    dest[j] = '\0';
}

static void json_escape(FILE *file, const char *value) {
    while (*value) {
        if (*value == '\\' || *value == '"') {
            fputc('\\', file);
        }
        fputc(*value, file);
        value++;
    }
}

static int write_manifest_header(FILE *file) {
    return fprintf(file,
        "{\n"
        "  \"schema\": \"falconpatch.modules.v1\",\n"
        "  \"description\": \"Modules for a developer-owned FalconPatch Android host app.\",\n"
        "  \"modules\": [\n") < 0;
}

static int write_manifest_footer(FILE *file) {
    return fprintf(file, "  ]\n}\n") < 0;
}

static int read_modules(module_entry *entries, size_t capacity, size_t *count) {
    FILE *file = fopen(MANIFEST_NAME, "rb");
    char line[1024];
    module_entry current;
    int in_module = 0;

    *count = 0;
    if (!file) {
        return 1;
    }

    memset(&current, 0, sizeof(current));
    while (fgets(line, sizeof(line), file)) {
        char *value_start;
        char *value_end;
        char key[16] = {0};
        char value[512] = {0};

        if (in_module && strstr(line, "}")) {
            if (*count < capacity) {
                entries[*count] = current;
                (*count)++;
            }
            in_module = 0;
            continue;
        }

        value_start = strchr(line, '"');
        if (!value_start) {
            continue;
        }
        value_end = strchr(value_start + 1, '"');
        if (!value_end) {
            continue;
        }
        copy_json_string(key, sizeof(key), value_start + 1, (size_t)(value_end - value_start - 1));

        value_start = strchr(value_end + 1, '"');
        if (!value_start) {
            continue;
        }
        value_end = strchr(value_start + 1, '"');
        if (!value_end) {
            continue;
        }
        copy_json_string(value, sizeof(value), value_start + 1, (size_t)(value_end - value_start - 1));

        if (strcmp(key, "type") == 0 &&
            (strcmp(value, "so") == 0 || strcmp(value, "lua") == 0)) {
            if (!in_module) {
                in_module = 1;
                memset(&current, 0, sizeof(current));
            }
            strncpy(current.type, value, sizeof(current.type) - 1);
        } else if (strcmp(key, "name") == 0) {
            strncpy(current.name, value, sizeof(current.name) - 1);
        } else if (strcmp(key, "path") == 0) {
            strncpy(current.path, value, sizeof(current.path) - 1);
        } else if (strcmp(key, "abi") == 0) {
            strncpy(current.abi, value, sizeof(current.abi) - 1);
        }

    }

    fclose(file);
    return 0;
}

static int rewrite_manifest(const module_entry *entries, size_t count) {
    FILE *file = fopen(MANIFEST_NAME, "wb");
    size_t i;

    if (!file) {
        fprintf(stderr, "error: could not write %s\n", MANIFEST_NAME);
        return 1;
    }

    if (write_manifest_header(file)) {
        fclose(file);
        return 1;
    }

    for (i = 0; i < count; i++) {
        fprintf(file, "    {\n      \"type\": \"");
        json_escape(file, entries[i].type);
        fprintf(file, "\",\n      \"name\": \"");
        json_escape(file, entries[i].name);
        fprintf(file, "\",\n      \"path\": \"");
        json_escape(file, entries[i].path);
        fprintf(file, "\"");
        if (strcmp(entries[i].type, "so") == 0) {
            fprintf(file, ",\n      \"abi\": \"");
            json_escape(file, entries[i].abi[0] ? entries[i].abi : "arm64-v8a");
            fprintf(file, "\"");
        }
        fprintf(file, "\n    }%s\n", i + 1 == count ? "" : ",");
    }

    if (write_manifest_footer(file)) {
        fclose(file);
        return 1;
    }

    fclose(file);
    return 0;
}

static int cmd_init(int argc, char **argv) {
    const char *dir = argc >= 3 ? argv[2] : ".";
    char modules_dir[512];
    char native_dir[512];
    char lua_dir[512];
    char manifest_path[512];
    FILE *file;

    snprintf(modules_dir, sizeof(modules_dir), "%s%smodules", dir, FPATCH_PATH_SEP);
    snprintf(native_dir, sizeof(native_dir), "%s%sso", modules_dir, FPATCH_PATH_SEP);
    snprintf(lua_dir, sizeof(lua_dir), "%s%slua", modules_dir, FPATCH_PATH_SEP);
    snprintf(manifest_path, sizeof(manifest_path), "%s%s%s", dir, FPATCH_PATH_SEP, MANIFEST_NAME);

    if (mkdir_if_needed(dir) || mkdir_if_needed(modules_dir) ||
        mkdir_if_needed(native_dir) || mkdir_if_needed(lua_dir)) {
        return 1;
    }

    if (path_exists(manifest_path)) {
        printf("exists: %s\n", manifest_path);
        return 0;
    }

    file = fopen(manifest_path, "wb");
    if (!file) {
        fprintf(stderr, "error: could not create %s\n", manifest_path);
        return 1;
    }

    if (write_manifest_header(file) || write_manifest_footer(file)) {
        fclose(file);
        return 1;
    }

    fclose(file);
    printf("created FalconPatch workspace at %s\n", dir);
    return 0;
}

static int cmd_add(const char *type, int argc, char **argv) {
    module_entry entries[128];
    module_entry next;
    size_t count;
    size_t i;
    const char *name;
    const char *path;
    const char *abi = "arm64-v8a";

    if ((strcmp(type, "so") == 0 && argc < 4) ||
        (strcmp(type, "lua") == 0 && argc < 4)) {
        print_usage();
        return 1;
    }

    name = argv[2];
    path = argv[3];
    if (strcmp(type, "so") == 0 && argc >= 5) {
        abi = argv[4];
    }

    if (!valid_name(name)) {
        fprintf(stderr, "error: module name must be 1-63 chars: letters, numbers, _, -, .\n");
        return 1;
    }
    if (!path_exists(path)) {
        fprintf(stderr, "error: module file does not exist: %s\n", path);
        return 1;
    }
    if (strcmp(type, "so") == 0 && !has_suffix(path, ".so")) {
        fprintf(stderr, "error: native modules must end in .so\n");
        return 1;
    }
    if (strcmp(type, "lua") == 0 && !has_suffix(path, ".lua")) {
        fprintf(stderr, "error: lua modules must end in .lua\n");
        return 1;
    }
    if (read_modules(entries, 128, &count)) {
        fprintf(stderr, "error: run 'fpatch init' first in this directory\n");
        return 1;
    }
    if (count >= 128) {
        fprintf(stderr, "error: manifest module limit reached\n");
        return 1;
    }

    for (i = 0; i < count; i++) {
        if (strcmp(entries[i].name, name) == 0) {
            fprintf(stderr, "error: module already exists: %s\n", name);
            return 1;
        }
    }

    memset(&next, 0, sizeof(next));
    strncpy(next.type, type, sizeof(next.type) - 1);
    strncpy(next.name, name, sizeof(next.name) - 1);
    strncpy(next.path, path, sizeof(next.path) - 1);
    strncpy(next.abi, abi, sizeof(next.abi) - 1);
    entries[count++] = next;

    if (rewrite_manifest(entries, count)) {
        return 1;
    }

    printf("added %s module '%s'\n", type, name);
    return 0;
}

static int cmd_list(void) {
    module_entry entries[128];
    size_t count;
    size_t i;

    if (read_modules(entries, 128, &count)) {
        fprintf(stderr, "error: no %s found; run 'fpatch init'\n", MANIFEST_NAME);
        return 1;
    }

    if (count == 0) {
        puts("no modules registered");
        return 0;
    }

    for (i = 0; i < count; i++) {
        if (strcmp(entries[i].type, "so") == 0) {
            printf("%s  %s  %s  abi=%s\n",
                   entries[i].type, entries[i].name, entries[i].path, entries[i].abi);
        } else {
            printf("%s  %s  %s\n", entries[i].type, entries[i].name, entries[i].path);
        }
    }

    return 0;
}

static int cmd_validate(void) {
    module_entry entries[128];
    size_t count;
    size_t i;
    int failed = 0;

    if (read_modules(entries, 128, &count)) {
        fprintf(stderr, "error: no %s found; run 'fpatch init'\n", MANIFEST_NAME);
        return 1;
    }

    for (i = 0; i < count; i++) {
        if (!valid_name(entries[i].name)) {
            fprintf(stderr, "invalid name: %s\n", entries[i].name);
            failed = 1;
        }
        if (!path_exists(entries[i].path)) {
            fprintf(stderr, "missing file: %s\n", entries[i].path);
            failed = 1;
        }
        if (strcmp(entries[i].type, "so") == 0 && !has_suffix(entries[i].path, ".so")) {
            fprintf(stderr, "invalid .so path: %s\n", entries[i].path);
            failed = 1;
        }
        if (strcmp(entries[i].type, "lua") == 0 && !has_suffix(entries[i].path, ".lua")) {
            fprintf(stderr, "invalid .lua path: %s\n", entries[i].path);
            failed = 1;
        }
    }

    if (failed) {
        return 1;
    }

    printf("ok: %zu module(s) validated\n", count);
    return 0;
}

static int cmd_example(void) {
    puts("Example fpatch CLI workflow:");
    puts("  fpatch init demo-workspace");
    puts("  cd demo-workspace");
    puts("  fpatch add-so hello ../examples/native/libhello_module.so arm64-v8a");
    puts("  fpatch add-lua script ../examples/lua/hello.lua");
    puts("  fpatch list");
    puts("  fpatch validate");
    puts("");
    puts("Build native examples with the Android NDK, then install modules into");
    puts("your app-private debug FalconPatch directory using your normal dev tooling.");
    return 0;
}

int main(int argc, char **argv) {
    const char *command;

    if (argc < 2) {
        print_usage();
        return 0;
    }

    command = argv[1];
    if (strcmp(command, "-h") == 0 || strcmp(command, "--help") == 0) {
        print_usage();
        return 0;
    }
    if (strcmp(command, "--version") == 0) {
        puts(FPATCH_VERSION);
        return 0;
    }
    if (strcmp(command, "init") == 0) {
        return cmd_init(argc, argv);
    }
    if (strcmp(command, "add-so") == 0) {
        return cmd_add("so", argc, argv);
    }
    if (strcmp(command, "add-lua") == 0) {
        return cmd_add("lua", argc, argv);
    }
    if (strcmp(command, "list") == 0) {
        return cmd_list();
    }
    if (strcmp(command, "validate") == 0) {
        return cmd_validate();
    }
    if (strcmp(command, "example") == 0) {
        return cmd_example();
    }

    fprintf(stderr, "error: unknown command '%s'\n", command);
    print_usage();
    return 1;
}
