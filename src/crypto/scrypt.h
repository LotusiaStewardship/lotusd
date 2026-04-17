/*
 * Copyright 2009 Colin Percival, 2011 ArtForz, 2012-2013 pooler
 * All rights reserved.
 */

#ifndef BITCOIN_CRYPTO_SCRYPT_H
#define BITCOIN_CRYPTO_SCRYPT_H

#include <stdint.h>
#include <stdlib.h>

static const int SCRYPT_SCRATCHPAD_SIZE = 131072 + 63;

void scrypt_1024_1_1_256(const uint8_t *input, uint8_t *output);
void scrypt_1024_1_1_256_sp_generic(const uint8_t *input, uint8_t *output,
                                    uint8_t *scratchpad);

#define scrypt_1024_1_1_256_sp(input, output, scratchpad)                      \
    scrypt_1024_1_1_256_sp_generic((input), (output), (scratchpad))

void PBKDF2_SHA256(const uint8_t *passwd, size_t passwdlen, const uint8_t *salt,
                   size_t saltlen, uint64_t c, uint8_t *buf, size_t dkLen);

#endif // BITCOIN_CRYPTO_SCRYPT_H
