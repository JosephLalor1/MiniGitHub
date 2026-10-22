/*
 * sha1.c - SHA-1, the fingerprint git gives everything
 *
 * The hash itself, the fingerprint of a git object ("blob 12\0..."
 * hashed), and turning fingerprints into their 40-character hex form and
 * back.
 */

#include "sha1.h"

/* ---------- SHA-1 ---------- */

#define ROTATE_LEFT(value, bits) (((value) << (bits)) | ((value) >> (32 - (bits))))

/* Mixes one 64-byte block into the running hash. The short names (w, a-e, f, k, temp)
   are the ones the SHA-1 standard (FIPS 180-4) uses, so the code can be checked against it. */
static void Sha1ProcessBlock(Sha1 *sha, const uint8_t *block)
{
    uint32_t w[80];
    for (int i = 0; i < 16; i++) w[i] = (uint32_t)block[i * 4] << 24 | (uint32_t)block[i * 4 + 1] << 16 | (uint32_t)block[i * 4 + 2] << 8 | block[i * 4 + 3];
    for (int i = 16; i < 80; i++) w[i] = ROTATE_LEFT(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    uint32_t a = sha->state[0], b = sha->state[1], c = sha->state[2], d = sha->state[3], e = sha->state[4];
    for (int i = 0; i < 80; i++) {
        uint32_t f, k;
        if (i < 20)      { f = (b & c) | (~b & d);           k = 0x5A827999; }
        else if (i < 40) { f = b ^ c ^ d;                    k = 0x6ED9EBA1; }
        else if (i < 60) { f = (b & c) | (b & d) | (c & d);  k = 0x8F1BBCDC; }
        else             { f = b ^ c ^ d;                    k = 0xCA62C1D6; }
        uint32_t t = ROTATE_LEFT(a, 5) + f + e + k + w[i];
        e = d; d = c; c = ROTATE_LEFT(b, 30); b = a; a = t;
    }
    sha->state[0] += a; sha->state[1] += b; sha->state[2] += c; sha->state[3] += d; sha->state[4] += e;
}
/* Starts a new SHA-1 calculation. */
void Sha1Init(Sha1 *sha)
{
    sha->state[0] = 0x67452301; sha->state[1] = 0xEFCDAB89; sha->state[2] = 0x98BADCFE; sha->state[3] = 0x10325476; sha->state[4] = 0xC3D2E1F0;
    sha->totalLength = 0; sha->blockUsed = 0;
}
/* Feeds more bytes into a SHA-1 calculation. */
void Sha1Update(Sha1 *sha, const void *data, size_t length)
{
    const uint8_t *bytes = data;
    sha->totalLength += length;
    if (sha->blockUsed) {
        while (length && sha->blockUsed < 64) { sha->block[sha->blockUsed++] = *bytes++; length--; }
        if (sha->blockUsed == 64) { Sha1ProcessBlock(sha, sha->block); sha->blockUsed = 0; }
    }
    while (length >= 64) { Sha1ProcessBlock(sha, bytes); bytes += 64; length -= 64; }
    while (length) { sha->block[sha->blockUsed++] = *bytes++; length--; }
}
/* Finishes a SHA-1 calculation and writes the 20-byte digest. */
void Sha1Final(Sha1 *sha, uint8_t digest[20])
{
    uint64_t bitLength = sha->totalLength * 8;
    uint8_t padByte = 0x80, zeroByte = 0;
    Sha1Update(sha, &padByte, 1);
    while (sha->blockUsed != 56) Sha1Update(sha, &zeroByte, 1);
    uint8_t lengthBytes[8];
    for (int i = 0; i < 8; i++) lengthBytes[i] = (uint8_t)(bitLength >> (56 - 8 * i));
    Sha1Update(sha, lengthBytes, 8);
    for (int i = 0; i < 5; i++) {
        digest[i * 4] = (uint8_t)(sha->state[i] >> 24); digest[i * 4 + 1] = (uint8_t)(sha->state[i] >> 16);
        digest[i * 4 + 2] = (uint8_t)(sha->state[i] >> 8); digest[i * 4 + 3] = (uint8_t)sha->state[i];
    }
}

/* The fingerprint git gives an object: SHA-1 of "<kind> <size>\0" + the contents. */
void GitObjectSha(const char *kind, const void *data, size_t length, uint8_t objectId[20])
{
    char header[48];
    int headerLength = snprintf(header, sizeof header, "%s %llu", kind, (unsigned long long)length);
    Sha1 sha;
    Sha1Init(&sha);
    Sha1Update(&sha, header, (size_t)headerLength + 1);  /* + the zero byte after it */
    Sha1Update(&sha, data, length);
    Sha1Final(&sha, objectId);
}

const uint8_t ZERO_SHA[20];
/* Writes a fingerprint as 40 lowercase hex digits (plus a terminating zero). */
void ShaToHex(const uint8_t objectId[20], char hex[41])
{
    static const char *digits = "0123456789abcdef";
    for (int i = 0; i < 20; i++) { hex[i * 2] = digits[objectId[i] >> 4]; hex[i * 2 + 1] = digits[objectId[i] & 15]; }
    hex[40] = 0;
}
/* Reads 40 hex characters; false if they aren't. */
bool ParseShaHex(const char *hex, uint8_t objectId[20])
{
    for (int i = 0; i < 40; i++) if (!isxdigit((unsigned char)hex[i])) return false;
    for (int i = 0; i < 20; i++) {
        int high = hex[i * 2], low = hex[i * 2 + 1];
        high = high <= '9' ? high - '0' : (high | 32) - 'a' + 10;
        low = low <= '9' ? low - '0' : (low | 32) - 'a' + 10;
        objectId[i] = (uint8_t)(high << 4 | low);
    }
    return true;
}
