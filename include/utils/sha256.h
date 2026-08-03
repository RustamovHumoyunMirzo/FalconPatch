#ifndef FPATCH_SHA256_H
#define FPATCH_SHA256_H

#include <stddef.h>

void fpatch_sha256(const void *data, size_t size, unsigned char digest[32]);
void fpatch_sha256_hex(const void *data, size_t size, char output[65]);

#endif
