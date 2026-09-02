#include "cli.h"
#include "utils/apk_inspector.h"
#include "utils/file_utils.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zip.h>

#define MAX_MODULE_FINDINGS 128

typedef struct {
    char value[256];
} ModuleString;

typedef struct {
    ModuleString strings[MAX_MODULE_FINDINGS];
    ModuleString jni_exports[MAX_MODULE_FINDINGS];
    ModuleString native_callers[MAX_MODULE_FINDINGS];
    ModuleString outbound_libs[MAX_MODULE_FINDINGS];
    size_t string_count;
    size_t jni_export_count;
    size_t native_caller_count;
    size_t outbound_lib_count;
    int register_natives;
} ModuleEvidence;

static void print_inspect_module_help(void) {
    puts("Usage: fpatch inspect-module --source <app.apk> --target <library> [options]");
    puts("       fpatch inspect-module --apk <app.apk> --target <library> [options]");
    puts("");
    puts("Inspects one native module inside an APK and reports DEX loads, JNI links,");
    puts("native module callers, and outbound native references.");
    puts("");
    puts("Required:");
    puts("  --source, --apk <file.apk>  APK to inspect.");
    puts("  --target <name>             Library name, with or without lib prefix and .so suffix.");
    puts("");
    puts("Options:");
    puts("  --abi <abi|all>             ABI to inspect; repeatable. Default: all ABIs.");
}

static int ends_with_case(const char *value, const char *suffix) {
    size_t value_size = strlen(value);
    size_t suffix_size = strlen(suffix);
    size_t i;
    if (value_size < suffix_size) {
        return 0;
    }
    value += value_size - suffix_size;
    for (i = 0; i < suffix_size; i++) {
        if (tolower((unsigned char)value[i]) !=
            tolower((unsigned char)suffix[i])) {
            return 0;
        }
    }
    return 1;
}

static int normalize_library_name(const char *input, char *output, size_t size) {
    const char *base;
    size_t length;
    if (!input || !input[0]) {
        return 0;
    }
    base = fpatch_path_basename(input);
    if (strncmp(base, "lib", 3) == 0) {
        base += 3;
    }
    length = strlen(base);
    if (length > 3 && ends_with_case(base, ".so")) {
        length -= 3;
    }
    if (length == 0 || length + 7 >= size) {
        return 0;
    }
    snprintf(output, size, "lib%.*s.so", (int)length, base);
    return 1;
}

static int module_name_from_library(const char *library, char *output, size_t size) {
    const char *start = library;
    size_t length = strlen(library);
    if (strncmp(start, "lib", 3) == 0) {
        start += 3;
        length -= 3;
    }
    if (length > 3 && ends_with_case(start, ".so")) {
        length -= 3;
    }
    if (length == 0 || length >= size) {
        return 0;
    }
    memcpy(output, start, length);
    output[length] = '\0';
    return 1;
}

static int abi_allowed(Command *cmd, const char *abi) {
    size_t count = get_flag_value_count(cmd, "--abi");
    size_t i;
    if (count == 0) {
        return 1;
    }
    for (i = 0; i < count; i++) {
        const char *candidate = get_flag_value_at(cmd, "--abi", i);
        if (candidate && (strcmp(candidate, "all") == 0 ||
                          strcmp(candidate, "-a") == 0 ||
                          strcmp(candidate, abi) == 0)) {
            return 1;
        }
    }
    return 0;
}

static int parse_native_entry(const char *name, char *abi, size_t abi_size,
                              const char **filename) {
    const char *start;
    const char *slash;
    size_t length;
    if (strncmp(name, "lib/", 4) != 0) {
        return 0;
    }
    start = name + 4;
    slash = strchr(start, '/');
    if (!slash || !slash[1]) {
        return 0;
    }
    length = (size_t)(slash - start);
    if (length == 0 || length >= abi_size) {
        return 0;
    }
    memcpy(abi, start, length);
    abi[length] = '\0';
    *filename = slash + 1;
    return 1;
}

static int add_unique(ModuleString *items, size_t *count,
                      const char *value) {
    size_t i;
    if (!value || !value[0]) {
        return 0;
    }
    for (i = 0; i < *count; i++) {
        if (strcmp(items[i].value, value) == 0) {
            return 0;
        }
    }
    if (*count >= MAX_MODULE_FINDINGS) {
        return 0;
    }
    snprintf(items[*count].value, sizeof(items[*count].value), "%s", value);
    (*count)++;
    return 1;
}

static int printable_string_char(unsigned char value) {
    return value >= 32 && value <= 126;
}

static int jni_symbol_char(unsigned char value) {
    return isalnum(value) || value == '_';
}

static void collect_native_strings(ModuleEvidence *evidence,
                                   const unsigned char *data, size_t size,
                                   const char *target_library,
                                   const char *target_module,
                                   int target_file,
                                   const char *entry_name) {
    size_t i = 0;
    while (i < size) {
        char value[256];
        size_t start = i;
        size_t length;
        while (i < size && printable_string_char(data[i])) {
            i++;
        }
        length = i - start;
        if (length >= 2 && length < sizeof(value)) {
            memcpy(value, data + start, length);
            value[length] = '\0';
            if (target_file) {
                add_unique(evidence->strings, &evidence->string_count, value);
                if (strncmp(value, "Java_", 5) == 0) {
                    size_t j = 5;
                    while (value[j] && jni_symbol_char((unsigned char)value[j])) {
                        j++;
                    }
                    value[j] = '\0';
                    add_unique(evidence->jni_exports, &evidence->jni_export_count, value);
                }
                if (strcmp(value, "RegisterNatives") == 0) {
                    evidence->register_natives = 1;
                }
                if (strncmp(value, "lib", 3) == 0 && ends_with_case(value, ".so") &&
                    strcmp(value, target_library) != 0) {
                    add_unique(evidence->outbound_libs, &evidence->outbound_lib_count, value);
                }
            } else if (strstr(value, target_library) ||
                       strstr(value, target_module)) {
                add_unique(evidence->native_callers, &evidence->native_caller_count,
                           entry_name);
            }
        }
        i++;
    }
}

static int read_zip_entry(zip_t *archive, const char *name,
                          unsigned char **data, size_t *size) {
    zip_stat_t stat;
    zip_file_t *file;
    zip_int64_t read_count;
    *data = NULL;
    *size = 0;
    zip_stat_init(&stat);
    if (zip_stat(archive, name, 0, &stat) != 0 || stat.size > SIZE_MAX) {
        return 0;
    }
    file = zip_fopen(archive, name, 0);
    if (!file) {
        return 0;
    }
    *data = (unsigned char *)malloc((size_t)stat.size ? (size_t)stat.size : 1);
    if (!*data) {
        zip_fclose(file);
        return 0;
    }
    read_count = zip_fread(file, *data, stat.size);
    zip_fclose(file);
    if (read_count < 0 || (zip_uint64_t)read_count != stat.size) {
        free(*data);
        *data = NULL;
        return 0;
    }
    *size = (size_t)stat.size;
    return 1;
}

static void scan_native_entries(Command *cmd, const char *apk_path,
                                const char *target_library,
                                const char *target_module,
                                ModuleEvidence *evidence) {
    zip_t *apk;
    int zip_error = 0;
    zip_int64_t count;
    zip_int64_t i;
    apk = zip_open(apk_path, ZIP_RDONLY, &zip_error);
    if (!apk) {
        return;
    }
    count = zip_get_num_entries(apk, 0);
    for (i = 0; i < count; i++) {
        const char *name = zip_get_name(apk, (zip_uint64_t)i, ZIP_FL_UNCHANGED);
        const char *filename = NULL;
        char abi[32];
        unsigned char *data;
        size_t size;
        int target_file;
        if (!name || !parse_native_entry(name, abi, sizeof(abi), &filename) ||
            !abi_allowed(cmd, abi)) {
            continue;
        }
        if (!read_zip_entry(apk, name, &data, &size)) {
            continue;
        }
        target_file = strcmp(filename, target_library) == 0;
        collect_native_strings(evidence, data, size, target_library,
                               target_module, target_file, name);
        free(data);
    }
    zip_discard(apk);
}

static int load_call_matches(const NativeLoadCall *call,
                             const char *target_library,
                             const char *target_module) {
    const char *base;
    if (!call->argument[0] || strcmp(call->argument, "dynamic/unknown") == 0) {
        return 0;
    }
    base = fpatch_path_basename(call->argument);
    return strcmp(call->argument, target_module) == 0 ||
           strcmp(call->argument, target_library) == 0 ||
           strcmp(base, target_library) == 0 ||
           ends_with_case(call->argument, target_library);
}

static int method_has_static_jni_export(const ModuleEvidence *evidence,
                                        const NativeMethod *method) {
    char class_name[256];
    char symbol[384];
    size_t used = 5;
    size_t i;
    snprintf(symbol, sizeof(symbol), "Java_");
    snprintf(class_name, sizeof(class_name), "%s", method->class_name);
    for (i = 0; class_name[i]; i++) {
        if (class_name[i] == '.') {
            class_name[i] = '_';
        } else if (class_name[i] == '_') {
            if (used + 2 >= sizeof(symbol)) {
                return 0;
            }
            symbol[used++] = '_';
            symbol[used++] = '1';
            continue;
        }
        if (used + 2 >= sizeof(symbol)) {
            return 0;
        }
        symbol[used++] = class_name[i];
    }
    if (used + strlen(method->method_name) + 2 >= sizeof(symbol)) {
        return 0;
    }
    symbol[used++] = '_';
    symbol[used] = '\0';
    strcat(symbol, method->method_name);
    for (i = 0; i < evidence->jni_export_count; i++) {
        if (strncmp(evidence->jni_exports[i].value, symbol, strlen(symbol)) == 0) {
            return 1;
        }
    }
    return 0;
}

static int evidence_has_string(const ModuleEvidence *evidence,
                               const char *value) {
    size_t i;
    for (i = 0; i < evidence->string_count; i++) {
        if (strcmp(evidence->strings[i].value, value) == 0) {
            return 1;
        }
    }
    return 0;
}

static int append_text(char *out, size_t out_size, const char *value) {
    size_t used = strlen(out);
    size_t length = strlen(value);
    if (used + length + 1 >= out_size) {
        return 0;
    }
    memcpy(out + used, value, length + 1);
    return 1;
}

static int append_jni_type(char *out, size_t out_size, const char *type) {
    char base[128];
    size_t length;
    int arrays = 0;
    if (!type || !type[0] || strcmp(type, "unknown") == 0) {
        return 0;
    }
    snprintf(base, sizeof(base), "%s", type);
    length = strlen(base);
    while (length >= 2 && strcmp(base + length - 2, "[]") == 0) {
        arrays++;
        base[length - 2] = '\0';
        length -= 2;
    }
    while (arrays-- > 0) {
        if (!append_text(out, out_size, "[")) {
            return 0;
        }
    }
    if (strcmp(base, "void") == 0) {
        return append_text(out, out_size, "V");
    }
    if (strcmp(base, "boolean") == 0) {
        return append_text(out, out_size, "Z");
    }
    if (strcmp(base, "byte") == 0) {
        return append_text(out, out_size, "B");
    }
    if (strcmp(base, "char") == 0) {
        return append_text(out, out_size, "C");
    }
    if (strcmp(base, "short") == 0) {
        return append_text(out, out_size, "S");
    }
    if (strcmp(base, "int") == 0) {
        return append_text(out, out_size, "I");
    }
    if (strcmp(base, "long") == 0) {
        return append_text(out, out_size, "J");
    }
    if (strcmp(base, "float") == 0) {
        return append_text(out, out_size, "F");
    }
    if (strcmp(base, "double") == 0) {
        return append_text(out, out_size, "D");
    }
    {
        size_t i;
        if (!append_text(out, out_size, "L")) {
            return 0;
        }
        for (i = 0; base[i]; i++) {
            char c[2];
            c[0] = base[i] == '.' ? '/' : base[i];
            c[1] = '\0';
            if (!append_text(out, out_size, c)) {
                return 0;
            }
        }
        return append_text(out, out_size, ";");
    }
}

static int method_jni_signature(const NativeMethod *method,
                                char *out, size_t out_size) {
    char params[512];
    char *cursor;
    if (!out_size) {
        return 0;
    }
    out[0] = '\0';
    if (!append_text(out, out_size, "(")) {
        return 0;
    }
    snprintf(params, sizeof(params), "%s", method->params);
    if (strcmp(params, "none") != 0) {
        cursor = params;
        while (cursor && *cursor) {
            char *comma = strstr(cursor, ", ");
            if (comma) {
                *comma = '\0';
            }
            if (!append_jni_type(out, out_size, cursor)) {
                return 0;
            }
            cursor = comma ? comma + 2 : NULL;
        }
    }
    return append_text(out, out_size, ")") &&
           append_jni_type(out, out_size, method->return_type);
}

static int method_has_registered_jni_evidence(const ModuleEvidence *evidence,
                                              const NativeMethod *method) {
    char slash_class[256];
    char signature[256];
    size_t i;
    if (!evidence->register_natives) {
        return 0;
    }
    snprintf(slash_class, sizeof(slash_class), "%s", method->class_name);
    for (i = 0; slash_class[i]; i++) {
        if (slash_class[i] == '.') {
            slash_class[i] = '/';
        }
    }
    if (!method_jni_signature(method, signature, sizeof(signature))) {
        return 0;
    }
    return (evidence_has_string(evidence, slash_class) ||
            evidence_has_string(evidence, method->class_name)) &&
           evidence_has_string(evidence, method->method_name) &&
           evidence_has_string(evidence, signature);
}

static void print_module_report(Command *cmd, const ApkInfo *info,
                                const char *target_library,
                                const char *target_module,
                                const ModuleEvidence *evidence) {
    int matched_entries = 0;
    int matched_loads = 0;
    int matched_methods = 0;
    int i;
    size_t j;

    puts("FalconPatch module inspection");
    printf("  Source: %s\n", info->path);
    printf("  Target: %s (%s)\n", target_library, target_module);
    printf("  ABI filter: ");
    if (get_flag_value_count(cmd, "--abi") == 0) {
        puts("all");
    } else {
        size_t count = get_flag_value_count(cmd, "--abi");
        for (j = 0; j < count; j++) {
            printf("%s%s", j ? ", " : "", get_flag_value_at(cmd, "--abi", j));
        }
        puts("");
    }

    puts("");
    puts("Module Entries");
    for (i = 0; i < info->native_lib_count && i < MAX_NATIVE_LIBS; i++) {
        const NativeLib *lib = &info->native_libs[i];
        const char *name = fpatch_path_basename(lib->path);
        if (strcmp(name, target_library) == 0 && abi_allowed(cmd, lib->abi)) {
            printf("  %s\n", lib->path);
            printf("    ABI: %s\n", lib->abi);
            printf("    Size: %llu bytes\n", lib->size);
            printf("    ELF: %s\n", lib->elf_valid ? "valid" : "unknown/invalid");
            matched_entries++;
        }
    }
    if (!matched_entries) {
        puts("  none");
    }

    puts("");
    puts("Java/Kotlin Loads");
    for (i = 0; i < info->load_call_count && i < MAX_LOAD_CALLS; i++) {
        const NativeLoadCall *call = &info->load_calls[i];
        if (load_call_matches(call, target_library, target_module)) {
            printf("  %s.%s -> %s(\"%s\") [%s]\n",
                   call->class_name, call->method_name, call->api,
                   call->argument, call->dex_file);
            matched_loads++;
        }
    }
    if (!matched_loads) {
        puts("  none found");
    }

    puts("");
    puts("JNI / Native Methods");
    printf("  Static JNI exports: %zu\n", evidence->jni_export_count);
    printf("  RegisterNatives: %s\n", evidence->register_natives ? "detected" : "not detected");
    for (i = 0; i < info->native_method_count && i < MAX_NATIVE_METHODS; i++) {
        const NativeMethod *method = &info->native_methods[i];
        int static_match = method_has_static_jni_export(evidence, method);
        int registered_match = method_has_registered_jni_evidence(evidence, method);
        if (static_match || registered_match) {
            printf("  %s.%s(%s): %s [%s, %s]\n",
                   method->class_name, method->method_name, method->params,
                   method->return_type, method->dex_file,
                   static_match ? "static export" : "registered JNI evidence");
            matched_methods++;
        }
    }
    if (!matched_methods) {
        puts("  no matching DEX native declarations found");
    }

    puts("");
    puts("Native Callers");
    for (j = 0; j < evidence->native_caller_count; j++) {
        printf("  %s\n", evidence->native_callers[j].value);
    }
    if (evidence->native_caller_count == 0) {
        puts("  none found by string scan");
    }

    puts("");
    puts("Target Native References");
    if (evidence->outbound_lib_count) {
        puts("  Referenced libraries:");
        for (j = 0; j < evidence->outbound_lib_count; j++) {
            printf("    %s\n", evidence->outbound_libs[j].value);
        }
    } else {
        puts("  Referenced libraries: none found");
    }
    if (evidence->jni_export_count) {
        puts("  JNI exports:");
        for (j = 0; j < evidence->jni_export_count; j++) {
            printf("    %s\n", evidence->jni_exports[j].value);
        }
    }
}

static void handle_inspect_module(Command *cmd) {
    const char *apk_path;
    const char *target;
    char target_library[128];
    char target_module[128];
    ApkInfo *info;
    ModuleEvidence evidence;

    if (has_flag(cmd, "--help")) {
        print_inspect_module_help();
        return;
    }
    apk_path = get_flag_value(cmd, "--source");
    if (!apk_path) {
        apk_path = get_flag_value(cmd, "--apk");
    }
    target = get_flag_value(cmd, "--target");
    if (!apk_path || !target) {
        print_inspect_module_help();
        cmd->exit_code = 1;
        return;
    }
    if (!normalize_library_name(target, target_library, sizeof(target_library)) ||
        !module_name_from_library(target_library, target_module, sizeof(target_module))) {
        fprintf(stderr, "Error: Invalid --target library name.\n");
        cmd->exit_code = 1;
        return;
    }
    info = (ApkInfo *)calloc(1, sizeof(*info));
    if (!info) {
        fprintf(stderr, "Error: Out of memory while preparing inspect-module state.\n");
        cmd->exit_code = 1;
        return;
    }
    if (!fpatch_inspect_apk(apk_path, info)) {
        free(info);
        cmd->exit_code = 1;
        return;
    }
    memset(&evidence, 0, sizeof(evidence));
    scan_native_entries(cmd, apk_path, target_library, target_module, &evidence);
    print_module_report(cmd, info, target_library, target_module, &evidence);
    free(info);
}

CMD_INIT(register_inspect_module_cmd) {
    Command *inspect = add_cmd("inspect-module", handle_inspect_module);
    if (inspect) {
        set_cmd_description(inspect, "Inspect one native module in an APK.");
        inspect->add_flag(inspect, "--source", true, true, false)
               ->add_flag(inspect, "--apk", true, true, false)
               ->add_flag(inspect, "--target", true, true, false)
               ->add_flag(inspect, "--abi", true, true, true)
               ->add_flag(inspect, "--help", true, false, false);
    }
}
