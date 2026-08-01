#ifndef FPATCH_API_H
#define FPATCH_API_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * FalconPatch host API contract.
 *
 * Android apps that opt in to FalconPatch should expose these calls from a
 * debug-only or internal QA build. Modules are loaded by the host app, not by
 * attaching to unrelated processes.
 */

typedef struct fpatch_host_api {
    int version;
    void (*log)(int level, const char *tag, const char *message);
    const char *(*get_app_id)(void);
    const char *(*get_files_dir)(void);
} fpatch_host_api;

typedef int (*fpatch_module_init_fn)(const fpatch_host_api *api);
typedef void (*fpatch_module_shutdown_fn)(void);

#define FPATCH_API_VERSION 1
#define FPATCH_MODULE_INIT "fpatch_module_init"
#define FPATCH_MODULE_SHUTDOWN "fpatch_module_shutdown"

#ifdef __cplusplus
}
#endif

#endif
