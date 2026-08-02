#ifndef CLI_H
#define CLI_H

#include <stdbool.h>
#include <stddef.h>

// Cross-platform constructor attribute fixed for MSVC C compiler
#if defined(_MSC_VER)
    #pragma section(".CRT$XCU", read)
    #define CMD_INIT(func) \
        static void func(void); \
        __declspec(allocate(".CRT$XCU")) static void (*func##_)(void) = func; \
        static void func(void)
#else
    #define CMD_INIT(func) \
        __attribute__((constructor)) static void func(void)
#endif

#define MAX_FLAGS 16
#define MAX_CMDS 16

typedef struct {
    const char *name;
    bool is_optional;
    bool requires_input;
    bool allow_dupes;      // If false, parsing fails when flag is repeated
    bool is_present;
    bool is_dupe;         // Tracks whether the flag was passed more than once
    const char *value;    // Holds the last value passed
} Flag;

typedef struct Command {
    const char *name;
    Flag flags[MAX_FLAGS];
    size_t flag_count;
    int exit_code;
    void (*handler)(struct Command *cmd);
    
    struct Command* (*add_flag)(struct Command *self, const char *name, bool is_optional, bool requires_input, bool allow_dupes);
} Command;

Command* add_cmd(const char *name, void (*handler)(Command *cmd));
bool cli_parse(int argc, char **argv);
const char* get_flag_value(Command *cmd, const char *flag_name);
bool has_flag(Command *cmd, const char *flag_name);
bool is_flag_dupe(Command *cmd, const char *flag_name);

#endif // CLI_H
