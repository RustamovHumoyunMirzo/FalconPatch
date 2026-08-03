#include "cli.h"
#include "utils/apk_inspector.h"

#include <stdio.h>
#include <stdlib.h>

static void print_inspect_help(void) {
    puts("Usage: fpatch inspect --source <app.apk>");
    puts("       fpatch inspect --apk <app.apk>");
    puts("       fpatch inspect --source <app.apk> --ndk");
    puts("");
    puts("Inspects an Android APK structure, manifest metadata, signatures, DEX files,");
    puts("native libraries, and FalconPatch bootstrap readiness.");
    puts("");
    puts("Flags:");
    puts("  --ndk    Advanced native/NDK-only report.");
}

static void handle_inspect(Command *cmd) {
    const char *apk_path;
    ApkInfo *info;

    if (has_flag(cmd, "--help")) {
        print_inspect_help();
        return;
    }

    apk_path = get_flag_value(cmd, "--source");
    if (!apk_path) {
        apk_path = get_flag_value(cmd, "--apk");
    }
    if (!apk_path) {
        print_inspect_help();
        cmd->exit_code = 1;
        return;
    }

    info = (ApkInfo *)calloc(1, sizeof(*info));
    if (!info) {
        fprintf(stderr, "Error: Out of memory while preparing inspect state.\n");
        cmd->exit_code = 1;
        return;
    }

    if (!fpatch_inspect_apk(apk_path, info)) {
        free(info);
        cmd->exit_code = 1;
        return;
    }

    if (has_flag(cmd, "--ndk")) {
        fpatch_print_ndk_report(info);
    } else {
        fpatch_print_inspect_report(info);
    }

    free(info);
}

CMD_INIT(register_inspect_cmd) {
    Command *inspect = add_cmd("inspect", handle_inspect);
    if (inspect) {
        set_cmd_description(inspect, "Inspect APK metadata, signing, DEX, and native code.");
        inspect->add_flag(inspect, "--source", true, true, false)
               ->add_flag(inspect, "--apk", true, true, false)
               ->add_flag(inspect, "--ndk", true, false, false)
               ->add_flag(inspect, "--help", true, false, false);
    }
}
