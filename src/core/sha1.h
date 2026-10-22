/*
 * sha1.h - SHA-1, the fingerprint git gives everything
 *
 * The hash itself, the fingerprint of a git object ("blob 12\0..."
 * hashed), and turning fingerprints into their 40-character hex form and
 * back.
 */

#ifndef MINIGITHUB_SHA1_H
#define MINIGITHUB_SHA1_H

#include "common.h"

typedef struct { uint32_t state[5]; uint8_t block[64]; uint64_t totalLength; int blockUsed; } Sha1;

/* Starts a new SHA-1 calculation. */
void Sha1Init(Sha1 *sha);

/* Feeds more bytes into a SHA-1 calculation. */
void Sha1Update(Sha1 *sha, const void *data, size_t length);

/* Finishes a SHA-1 calculation and writes the 20-byte digest. */
void Sha1Final(Sha1 *sha, uint8_t digest[20]);

/* The fingerprint git gives an object: SHA-1 of "<kind> <size>\0" + the contents. */
void GitObjectSha(const char *kind, const void *data, size_t length, uint8_t objectId[20]);
extern const uint8_t ZERO_SHA[20];

/* Writes a fingerprint as 40 lowercase hex digits (plus a terminating zero). */
void ShaToHex(const uint8_t objectId[20], char hex[41]);

/* Reads 40 hex characters; false if they aren't. */
bool ParseShaHex(const char *hex, uint8_t objectId[20]);

#endif
