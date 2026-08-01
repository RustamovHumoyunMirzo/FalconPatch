#include "fpatch_api.h"

int fpatch_module_init(const fpatch_host_api *api) {
    if (api && api->log) {
        api->log(1, "hello_module", "native module loaded");
    }

    return 0;
}

void fpatch_module_shutdown(void) {
}
