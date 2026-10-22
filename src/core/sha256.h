/*
 * sha256.h - SHA-256, HMAC and PBKDF2
 *
 * The building blocks of password hashing (see data/passwords.c). The git
 * server also uses SHA-256 to remember logins without keeping passwords.
 */

#ifndef MINIGITHUB_SHA256_H
#define MINIGITHUB_SHA256_H

#include "common.h"

typedef struct { uint32_t state[8]; uint8_t block[64]; uint64_t totalLength; int blockUsed; } Sha256;

/* Starts a new SHA-256 calculation. */
void Sha256Init(Sha256 *sha);

/* Feeds more bytes into a SHA-256 calculation. */
void Sha256Update(Sha256 *sha, const void *data, size_t length);

/* Finishes a SHA-256 calculation and writes the 32-byte digest. */
void Sha256Final(Sha256 *sha, uint8_t digest[32]);

/* Standard PBKDF2-HMAC-SHA256, producing one 32-byte block. */
void Pbkdf2(const char *password, const uint8_t *salt, int saltLength, int iterations, uint8_t out[32]);

#endif
