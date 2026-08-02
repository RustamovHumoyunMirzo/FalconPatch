/*
 * ----------------------------------------------------------------------------
 * HOW TO ADD A NEW COMMAND:
 * ----------------------------------------------------------------------------
 *   1. Create a new `.c` file in `src/commands/` (e.g., `src/commands/hello.c`).
 *   2. Include `"cli.h"`.
 *   3. Write the command handler function.
 *   4. Use the `CMD_INIT` macro to self-register the command before `main()`.
 *
 *   EXAMPLE (`src/commands/hello.c`):
 *
 *     #include <stdio.h>
 *     #include "cli.h"
 *
 *     static void handle_hello(Command *cmd) {
 *         if (has_flag(cmd, "--world")) {
 *             printf("Hello, World!\n");
 *         }
 *
 *         if (has_flag(cmd, "--echo")) {
 *             const char *val = get_flag_value(cmd, "--echo");
 *             printf("Echo: %s\n", val ? val : "(none)");
 *         }
 *     }
 *
 *     CMD_INIT(register_hello_cmd) {
 *         Command *cmd = add_cmd("hello", handle_hello);
 *         if (cmd) {
 *             // add_flag signature:
 *             // (self, flag_name, is_optional, requires_input, allow_dupes)
 *             cmd->add_flag(cmd, "--world", true,  false, false)
 *                ->add_flag(cmd, "--echo",  true,  true,  false);
 *         }
 *     }
 *
 * ----------------------------------------------------------------------------
 * CLI USAGE EXAMPLES:
 * ----------------------------------------------------------------------------
 *   $ fpatch hello --world
 *   Hello, World!
 *
 *   $ fpatch hello --echo "Hello World"
 *   Echo: Hello World
 *
 *   $ fpatch math --add 10 --add 20
 *   Error: Duplicate flag '--add' is not allowed for command 'math'
 *
 *   $ fpatch math
 *   Error: Missing required flag '--add' for command 'math'
 */

#include "cli.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static Command g_commands[MAX_CMDS];
static size_t g_cmd_count = 0;

static Command* add_flag_impl(Command *self, const char *name, bool is_optional, bool requires_input, bool allow_dupes) {
    if (self->flag_count >= MAX_FLAGS) {
        fprintf(stderr, "Error: Max flags exceeded for command '%s'\n", self->name);
        return self;
    }
    
    Flag *f = &self->flags[self->flag_count++];
    f->name = name;
    f->is_optional = is_optional;
    f->requires_input = requires_input;
    f->allow_dupes = allow_dupes;
    f->is_present = false;
    f->is_dupe = false;
    f->value = NULL;
    
    return self;
}

Command* add_cmd(const char *name, void (*handler)(Command *cmd)) {
    if (g_cmd_count >= MAX_CMDS) {
        fprintf(stderr, "Error: Max commands exceeded\n");
        return NULL;
    }
    
    Command *cmd = &g_commands[g_cmd_count++];
    cmd->name = name;
    cmd->flag_count = 0;
    cmd->exit_code = 0;
    cmd->handler = handler;
    cmd->add_flag = add_flag_impl;
    
    return cmd;
}

const char* get_flag_value(Command *cmd, const char *flag_name) {
    for (size_t i = 0; i < cmd->flag_count; i++) {
        if (strcmp(cmd->flags[i].name, flag_name) == 0) {
            return cmd->flags[i].value;
        }
    }
    return NULL;
}

bool has_flag(Command *cmd, const char *flag_name) {
    for (size_t i = 0; i < cmd->flag_count; i++) {
        if (strcmp(cmd->flags[i].name, flag_name) == 0) {
            return cmd->flags[i].is_present;
        }
    }
    return false;
}

bool is_flag_dupe(Command *cmd, const char *flag_name) {
    for (size_t i = 0; i < cmd->flag_count; i++) {
        if (strcmp(cmd->flags[i].name, flag_name) == 0) {
            return cmd->flags[i].is_dupe;
        }
    }
    return false;
}

bool cli_parse(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <command> [flags]\n", argv[0]);
        return false;
    }

    const char *cmd_name = argv[1];
    Command *matched_cmd = NULL;

    for (size_t i = 0; i < g_cmd_count; i++) {
        if (strcmp(g_commands[i].name, cmd_name) == 0) {
            matched_cmd = &g_commands[i];
            break;
        }
    }

    if (!matched_cmd) {
        fprintf(stderr, "Error: Unknown command '%s'\n", cmd_name);
        return false;
    }

    // Parse flags
    for (int i = 2; i < argc; i++) {
        const char *arg = argv[i];
        bool flag_found = false;

        for (size_t j = 0; j < matched_cmd->flag_count; j++) {
            Flag *f = &matched_cmd->flags[j];
            if (strcmp(f->name, arg) == 0) {
                flag_found = true;

                // Check for duplicate flag
                if (f->is_present) {
                    if (!f->allow_dupes) {
                        fprintf(stderr, "Error: Duplicate flag '%s' is not allowed for command '%s'\n", f->name, matched_cmd->name);
                        return false;
                    }
                    f->is_dupe = true;
                }

                f->is_present = true;

                if (f->requires_input) {
                    if (i + 1 < argc && argv[i + 1][0] != '-') {
                        f->value = argv[++i]; // Consume next argument as input value
                    } else {
                        fprintf(stderr, "Error: Flag '%s' requires a value.\n", f->name);
                        return false;
                    }
                }
                break;
            }
        }

        if (!flag_found) {
            fprintf(stderr, "Error: Unrecognized flag '%s' for command '%s'\n", arg, matched_cmd->name);
            return false;
        }
    }

    // Validate required flags
    for (size_t j = 0; j < matched_cmd->flag_count; j++) {
        Flag *f = &matched_cmd->flags[j];
        if (!f->is_optional && !f->is_present) {
            fprintf(stderr, "Error: Missing required flag '%s' for command '%s'\n", f->name, matched_cmd->name);
            return false;
        }
    }

    // Execute callback handler
    if (matched_cmd->handler) {
        matched_cmd->handler(matched_cmd);
    }

    return matched_cmd->exit_code == 0;
}
