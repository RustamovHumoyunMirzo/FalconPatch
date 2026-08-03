#include "fp_internal.h"

#include <android/log.h>
#include <lauxlib.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char *script;
    size_t size;
} FpWorkerJob;

static void *worker_main(void *argument) {
    FpWorkerJob *job = (FpWorkerJob *)argument;
    lua_State *state = luaL_newstate();

    if (state) {
        fp_lua_open_restricted(state);
        fp_lua_register_all_modules(state);
        if (luaL_loadbuffer(state, job->script, job->size, "background-worker") != LUA_OK ||
            lua_pcall(state, 0, 0, 0) != LUA_OK) {
            fp_logf(ANDROID_LOG_ERROR, "FalconPatch/Worker", "%s",
                    lua_tostring(state, -1));
        }
        lua_close(state);
    }
    if (g_fp_runtime.vm) {
        (*g_fp_runtime.vm)->DetachCurrentThread(g_fp_runtime.vm);
    }
    free(job->script);
    free(job);
    return NULL;
}

static int lua_submit(lua_State *state) {
    size_t size;
    const char *script = luaL_checklstring(state, 1, &size);
    FpWorkerJob *job = (FpWorkerJob *)calloc(1, sizeof(*job));
    pthread_t thread;

    if (!job) {
        lua_pushboolean(state, 0);
        return 1;
    }
    job->script = (char *)malloc(size + 1);
    if (!job->script) {
        free(job);
        lua_pushboolean(state, 0);
        return 1;
    }
    memcpy(job->script, script, size);
    job->script[size] = '\0';
    job->size = size;
    if (pthread_create(&thread, NULL, worker_main, job) != 0) {
        free(job->script);
        free(job);
        lua_pushboolean(state, 0);
        return 1;
    }
    pthread_detach(thread);
    lua_pushboolean(state, 1);
    return 1;
}

int fp_open_background_worker_module(lua_State *state) {
    static const luaL_Reg functions[] = {
        {"submit", lua_submit},
        {NULL, NULL}
    };
    luaL_newlib(state, functions);
    return 1;
}
