#ifndef XENITH_SHA256_H
#define XENITH_SHA256_H

#include <stddef.h>

/* SHA-256 of data, written as 64 lowercase hex chars + NUL into hex_out[65]. */
void sha256_hex(const void *data, size_t len, char hex_out[65]);

#endif
