#include "cli.h"
#include "utils/apk_detacher.h"

#include <stdio.h>
#include <string.h>

static void print_detach_help(void) {
    puts("Usage: fpatch detach --target <app.apk> --so <library> --out <app.apk> [options]");
    puts("");
    puts("Removes a native library from an authorized APK and strips old signature entries.");
    puts("");
    puts("Required:");
    puts("  --target <file.apk>         APK to edit.");
    puts("  --so <name>                 Library name, with or without lib prefix and .so suffix.");
    puts("  --out <file.apk>            Output APK path.");
    puts("");
    puts("Options:");
    puts("  --abi <abi|all>             ABI to target; repeatable. Default: all ABIs.");
    puts("  -a                          Target all ABIs.");
    puts("  --sign                      Align, sign, and verify the output APK.");
    puts("  --smart-repair              Repair literal load calls and safe JNI callsites.");
    puts("");
    puts("Signing:");
    puts("  --keystore <file>           Signing keystore; generated debug key by default.");
    puts("  --ks-alias <name>           Key alias (default: androiddebugkey).");
    puts("  --ks-pass <password>        Keystore password (default: android).");
    puts("  --key-pass <password>       Key password (default: android).");
}

static int set_text(char *destination, size_t size, const char *value,
                    char *error, size_t error_size) {
    if (!value) {
        return 1;
    }
    if (strlen(value) >= size) {
        snprintf(error, error_size, "A command-line value is too long.");
        return 0;
    }
    snprintf(destination, size, "%s", value);
    return 1;
}

static int add_abi(FpatchDetachRequest *request, const char *abi,
                   char *error, size_t error_size) {
    if (request->abi_count >= FPATCH_MAX_DETACH_ABIS) {
        snprintf(error, error_size, "Too many --abi values.");
        return 0;
    }
    if (!set_text(request->abis[request->abi_count],
                  sizeof(request->abis[request->abi_count]), abi,
                  error, error_size)) {
        return 0;
    }
    request->abi_count++;
    return 1;
}

static int parse_detach(Command *cmd, FpatchDetachRequest *request,
                        char *error, size_t error_size) {
    size_t i;
    if (!set_text(request->target, sizeof(request->target),
                  get_flag_value(cmd, "--target"), error, error_size) ||
        !set_text(request->library, sizeof(request->library),
                  get_flag_value(cmd, "--so"), error, error_size) ||
        !set_text(request->output, sizeof(request->output),
                  get_flag_value(cmd, "--out"), error, error_size) ||
        !set_text(request->keystore, sizeof(request->keystore),
                  get_flag_value(cmd, "--keystore"), error, error_size) ||
        !set_text(request->key_alias, sizeof(request->key_alias),
                  get_flag_value(cmd, "--ks-alias"), error, error_size) ||
        !set_text(request->store_password, sizeof(request->store_password),
                  get_flag_value(cmd, "--ks-pass"), error, error_size) ||
        !set_text(request->key_password, sizeof(request->key_password),
                  get_flag_value(cmd, "--key-pass"), error, error_size)) {
        return 0;
    }
    for (i = 0; i < get_flag_value_count(cmd, "--abi"); i++) {
        if (!add_abi(request, get_flag_value_at(cmd, "--abi", i),
                     error, error_size)) {
            return 0;
        }
    }
    if (has_flag(cmd, "-a")) {
        request->abi_count = 0;
    }
    request->sign = has_flag(cmd, "--sign");
    request->smart_repair = has_flag(cmd, "--smart-repair");
    return 1;
}

static void handle_detach(Command *cmd) {
    FpatchDetachRequest request;
    FpatchDetachResult result;
    char error[1024] = "";
    size_t i;

    if (has_flag(cmd, "--help")) {
        print_detach_help();
        return;
    }
    fpatch_detach_request_init(&request);
    if (!parse_detach(cmd, &request, error, sizeof(error)) ||
        !fpatch_detach_apk(&request, &result, error, sizeof(error))) {
        fprintf(stderr, "Error: %s\n", error[0] ? error : "Detach failed.");
        cmd->exit_code = 1;
        return;
    }
    puts("FalconPatch detach complete");
    printf("  Library: %s\n", result.library);
    printf("  Signing: %s\n", result.resigned ? "resigned and verified" : "unsigned");
    printf("  Output: %s\n", result.output);
    puts("  Removed:");
    for (i = 0; i < result.removed_count; i++) {
        printf("    %s\n", result.removed_entries[i]);
    }
    if (request.smart_repair) {
        printf("  Smart repair load calls: %zu\n", result.repaired_load_calls);
        printf("  Smart repair JNI exports: %zu\n", result.jni_exports);
        printf("  Smart repair RegisterNatives: %s\n",
               result.detected_registered_jni ? "detected" : "not detected");
        printf("  Smart repair native calls: %zu\n", result.repaired_native_calls);
        if (result.skipped_native_calls) {
            printf("  Smart repair skipped native calls: %zu\n",
                   result.skipped_native_calls);
        }
    }
    if (result.stripped_falconpatch_payload) {
        puts("  Stripped FalconPatch payload: yes");
    }
}

CMD_INIT(register_detach_cmd) {
    Command *detach = add_cmd("detach", handle_detach);
    if (detach) {
        set_cmd_description(detach, "Detach a native library from an APK.");
        detach->add_flag(detach, "--target", true, true, false)
              ->add_flag(detach, "--so", true, true, false)
              ->add_flag(detach, "--out", true, true, false)
              ->add_flag(detach, "--abi", true, true, true)
              ->add_flag(detach, "-a", true, false, false)
              ->add_flag(detach, "--sign", true, false, false)
              ->add_flag(detach, "--smart-repair", true, false, false)
              ->add_flag(detach, "--keystore", true, true, false)
              ->add_flag(detach, "--ks-alias", true, true, false)
              ->add_flag(detach, "--ks-pass", true, true, false)
              ->add_flag(detach, "--key-pass", true, true, false)
              ->add_flag(detach, "--help", true, false, false);
    }
}
