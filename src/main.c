#include "cli.h"

int main(int argc, char **argv) {
    if (!cli_parse(argc, argv)) {
        return 1;
    }
    return 0;
}