#include "cli.h"

#include <stdio.h>
#include <string.h>

#ifndef FPATCH_VERSION
#define FPATCH_VERSION "1.8.0"
#endif

static Command g_commands[MAX_CMDS];
static size_t g_cmd_count = 0;

static Command *add_flag_impl(Command *self, const char *name, bool is_optional,
                              bool requires_input, bool allow_dupes) {
    Flag *flag;

    if (self->flag_count >= MAX_FLAGS) {
        fprintf(stderr, "Error: Maximum flags exceeded for command '%s'.\n", self->name);
        return self;
    }

    flag = &self->flags[self->flag_count++];
    memset(flag, 0, sizeof(*flag));
    flag->name = name;
    flag->is_optional = is_optional;
    flag->requires_input = requires_input;
    flag->allow_dupes = allow_dupes;
    return self;
}

Command *add_cmd(const char *name, void (*handler)(Command *cmd)) {
    Command *cmd;

    if (g_cmd_count >= MAX_CMDS) {
        fprintf(stderr, "Error: Maximum command count exceeded.\n");
        return NULL;
    }

    cmd = &g_commands[g_cmd_count++];
    memset(cmd, 0, sizeof(*cmd));
    cmd->name = name;
    cmd->handler = handler;
    cmd->add_flag = add_flag_impl;
    return cmd;
}

void set_cmd_description(Command *cmd, const char *description) {
    if (cmd) {
        cmd->description = description;
    }
}

static Flag *find_flag(Command *cmd, const char *flag_name) {
    size_t i;

    for (i = 0; i < cmd->flag_count; i++) {
        if (strcmp(cmd->flags[i].name, flag_name) == 0) {
            return &cmd->flags[i];
        }
    }
    return NULL;
}

const char *get_flag_value(Command *cmd, const char *flag_name) {
    Flag *flag = find_flag(cmd, flag_name);
    return flag ? flag->value : NULL;
}

const char *get_flag_value_at(Command *cmd, const char *flag_name, size_t index) {
    Flag *flag = find_flag(cmd, flag_name);
    if (!flag || index >= flag->value_count) {
        return NULL;
    }
    return flag->values[index];
}

size_t get_flag_value_count(Command *cmd, const char *flag_name) {
    Flag *flag = find_flag(cmd, flag_name);
    return flag ? flag->value_count : 0;
}

bool has_flag(Command *cmd, const char *flag_name) {
    Flag *flag = find_flag(cmd, flag_name);
    return flag ? flag->is_present : false;
}

bool is_flag_dupe(Command *cmd, const char *flag_name) {
    Flag *flag = find_flag(cmd, flag_name);
    return flag ? flag->is_dupe : false;
}

static void print_banner(void) {
    puts("FalconPatch " FPATCH_VERSION);
    puts("Android testing and security instrumentation for authorized applications.");
    puts("Copyright (c) 2026 Rustamov Humoyun Mirzo");
}

static void print_root_help(void) {
    size_t i;

    print_banner();
    puts("");
    puts("Usage: fpatch <command> [flags]");
    puts("       fpatch --version");
    puts("");
    puts("Commands:");
    for (i = 0; i < g_cmd_count; i++) {
        printf("  %-15s %s\n", g_commands[i].name,
               g_commands[i].description ? g_commands[i].description : "");
    }
    puts("");
    puts("Run 'fpatch <command> --help' for command-specific help.");
}

static void reset_command_flags(Command *cmd) {
    size_t i;

    cmd->exit_code = 0;
    for (i = 0; i < cmd->flag_count; i++) {
        cmd->flags[i].is_present = false;
        cmd->flags[i].is_dupe = false;
        cmd->flags[i].value = NULL;
        cmd->flags[i].value_count = 0;
    }
}

bool cli_parse(int argc, char **argv) {
    const char *cmd_name;
    Command *matched_cmd = NULL;
    size_t i;

    if (argc < 2) {
        print_banner();
        puts("Run 'fpatch --help' to list commands.");
        return true;
    }

    cmd_name = argv[1];
    if (strcmp(cmd_name, "--version") == 0 || strcmp(cmd_name, "-v") == 0) {
        puts(FPATCH_VERSION);
        return true;
    }
    if (strcmp(cmd_name, "--help") == 0 || strcmp(cmd_name, "-h") == 0 ||
        strcmp(cmd_name, "help") == 0) {
        print_root_help();
        return true;
    }

    for (i = 0; i < g_cmd_count; i++) {
        if (strcmp(g_commands[i].name, cmd_name) == 0) {
            matched_cmd = &g_commands[i];
            break;
        }
    }
    if (!matched_cmd) {
        fprintf(stderr, "Error: Unknown command '%s'. Run 'fpatch --help'.\n", cmd_name);
        return false;
    }

    reset_command_flags(matched_cmd);
    for (i = 2; i < (size_t)argc; i++) {
        Flag *flag = find_flag(matched_cmd, argv[i]);
        if (!flag) {
            fprintf(stderr, "Error: Unrecognized flag '%s' for command '%s'.\n",
                    argv[i], matched_cmd->name);
            return false;
        }
        if (flag->is_present) {
            if (!flag->allow_dupes) {
                fprintf(stderr, "Error: Duplicate flag '%s' is not allowed for command '%s'.\n",
                        flag->name, matched_cmd->name);
                return false;
            }
            flag->is_dupe = true;
        }
        flag->is_present = true;

        if (flag->requires_input) {
            if (i + 1 >= (size_t)argc || argv[i + 1][0] == '-') {
                fprintf(stderr, "Error: Flag '%s' requires a value.\n", flag->name);
                return false;
            }
            if (flag->value_count >= MAX_FLAG_VALUES) {
                fprintf(stderr, "Error: Too many values for flag '%s'.\n", flag->name);
                return false;
            }
            flag->value = argv[++i];
            flag->values[flag->value_count++] = flag->value;
        }
    }

    for (i = 0; i < matched_cmd->flag_count; i++) {
        Flag *flag = &matched_cmd->flags[i];
        if (!flag->is_optional && !flag->is_present) {
            fprintf(stderr, "Error: Missing required flag '%s' for command '%s'.\n",
                    flag->name, matched_cmd->name);
            return false;
        }
    }

    if (matched_cmd->handler) {
        matched_cmd->handler(matched_cmd);
    }
    return matched_cmd->exit_code == 0;
}
