/*
 * sha256.c - SHA-256, HMAC and PBKDF2
 *
 * The building blocks of password hashing (see data/passwords.c). The git
 * server also uses SHA-256 to remember logins without keeping passwords.
 */

#include "sha256.h"

/* ------------------------------------------------------------------ */
/* Password hashing                                                   */
/*                                                                    */
/* SHA-256 turns any input into a 32-byte "fingerprint" that can't be */
/* reversed. PBKDF2 repeats it tens of thousands of times with a      */
/* random salt, so even a stolen save file is slow to crack and two   */
/* people with the same password get different hashes.               */
/* ------------------------------------------------------------------ */

static const uint32_t SHA256_ROUND_CONSTANTS[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};
#define ROTATE_RIGHT(value, bits) (((value) >> (bits)) | ((value) << (32 - (bits))))

/* Mixes one 64-byte block into the running hash. The short names (w, s0, s1, a-h, t1, t2)
   are the ones the SHA-256 standard (FIPS 180-4) uses, so the code can be checked against it. */
static void Sha256ProcessBlock(Sha256 *sha, const uint8_t *block)
{
    uint32_t w[64];
    for (int i = 0; i < 16; i++)
        w[i] = (uint32_t)block[4*i] << 24 | (uint32_t)block[4*i+1] << 16 | (uint32_t)block[4*i+2] << 8 | block[4*i+3];
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = ROTATE_RIGHT(w[i-15], 7) ^ ROTATE_RIGHT(w[i-15], 18) ^ (w[i-15] >> 3);
        uint32_t s1 = ROTATE_RIGHT(w[i-2], 17) ^ ROTATE_RIGHT(w[i-2], 19) ^ (w[i-2] >> 10);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    uint32_t a = sha->state[0], b = sha->state[1], c = sha->state[2], d = sha->state[3];
    uint32_t e = sha->state[4], f = sha->state[5], g = sha->state[6], h = sha->state[7];
    for (int i = 0; i < 64; i++) {
        uint32_t t1 = h + (ROTATE_RIGHT(e, 6) ^ ROTATE_RIGHT(e, 11) ^ ROTATE_RIGHT(e, 25)) + ((e & f) ^ (~e & g)) + SHA256_ROUND_CONSTANTS[i] + w[i];
        uint32_t t2 = (ROTATE_RIGHT(a, 2) ^ ROTATE_RIGHT(a, 13) ^ ROTATE_RIGHT(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    sha->state[0] += a; sha->state[1] += b; sha->state[2] += c; sha->state[3] += d;
    sha->state[4] += e; sha->state[5] += f; sha->state[6] += g; sha->state[7] += h;
}
/* Starts a new SHA-256 calculation. */
void Sha256Init(Sha256 *sha)
{
    static const uint32_t initialState[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    memcpy(sha->state, initialState, sizeof initialState);
    sha->totalLength = 0;
    sha->blockUsed = 0;
}
/* Feeds more bytes into a SHA-256 calculation. */
void Sha256Update(Sha256 *sha, const void *data, size_t length)
{
    const uint8_t *bytes = (const uint8_t *)data;
    sha->totalLength += length;
    /* top up a part-filled block first, then hash whole blocks straight from the input */
    if (sha->blockUsed) {
        while (length && sha->blockUsed < 64) { sha->block[sha->blockUsed++] = *bytes++; length--; }
        if (sha->blockUsed == 64) { Sha256ProcessBlock(sha, sha->block); sha->blockUsed = 0; }
    }
    while (length >= 64) { Sha256ProcessBlock(sha, bytes); bytes += 64; length -= 64; }
    while (length) { sha->block[sha->blockUsed++] = *bytes++; length--; }
}
/* Finishes a SHA-256 calculation and writes the 32-byte digest. */
void Sha256Final(Sha256 *sha, uint8_t digest[32])
{
    uint64_t bitLength = sha->totalLength * 8;
    uint8_t padByte = 0x80, zeroByte = 0, lenBytes[8];
    Sha256Update(sha, &padByte, 1);
    while (sha->blockUsed != 56) Sha256Update(sha, &zeroByte, 1);
    for (int i = 0; i < 8; i++) lenBytes[i] = (uint8_t)(bitLength >> (56 - 8 * i));
    Sha256Update(sha, lenBytes, 8);
    for (int i = 0; i < 8; i++) {
        digest[4*i]   = (uint8_t)(sha->state[i] >> 24); digest[4*i+1] = (uint8_t)(sha->state[i] >> 16);
        digest[4*i+2] = (uint8_t)(sha->state[i] >> 8);  digest[4*i+3] = (uint8_t)(sha->state[i]);
    }
}

/* An HMAC-SHA256 key, ready to use: HMAC hashes the key (padded to a block and
   mixed with 0x36 / 0x5c) before every message, so with the same key used tens of
   thousands of times - as PBKDF2 does - those two blocks are hashed once and kept. */
typedef struct { Sha256 inner, outer; } HmacKey;

static void HmacKeyInit(HmacKey *hmac, const uint8_t *key, size_t keyLength)
{
    uint8_t paddedKey[64] = {0}, innerPad[64], outerPad[64];
    if (keyLength > 64) { Sha256 sha; Sha256Init(&sha); Sha256Update(&sha, key, keyLength); Sha256Final(&sha, paddedKey); }
    else memcpy(paddedKey, key, keyLength);
    for (int i = 0; i < 64; i++) { innerPad[i] = paddedKey[i] ^ 0x36; outerPad[i] = paddedKey[i] ^ 0x5c; }
    Sha256Init(&hmac->inner); Sha256Update(&hmac->inner, innerPad, 64);
    Sha256Init(&hmac->outer); Sha256Update(&hmac->outer, outerPad, 64);
    memset(paddedKey, 0, sizeof paddedKey);               /* don't leave the password lying around */
    memset(innerPad, 0, sizeof innerPad);
    memset(outerPad, 0, sizeof outerPad);
}

/* HMAC-SHA256 of a message with a key prepared by HmacKeyInit. */
static void HmacSha256(const HmacKey *hmac, const uint8_t *message, size_t messageLength, uint8_t out[32])
{
    uint8_t innerHash[32];
    Sha256 sha = hmac->inner;
    Sha256Update(&sha, message, messageLength); Sha256Final(&sha, innerHash);
    sha = hmac->outer;
    Sha256Update(&sha, innerHash, 32); Sha256Final(&sha, out);
}

/* Standard PBKDF2-HMAC-SHA256, producing one 32-byte block. */
void Pbkdf2(const char *password, const uint8_t *salt, int saltLength, int iterations, uint8_t out[32])
{
    HmacKey hmac;
    HmacKeyInit(&hmac, (const uint8_t *)password, strlen(password));
    uint8_t firstMessage[64 + 4], block[32], nextBlock[32];
    memcpy(firstMessage, salt, (size_t)saltLength);
    firstMessage[saltLength] = 0; firstMessage[saltLength + 1] = 0; firstMessage[saltLength + 2] = 0; firstMessage[saltLength + 3] = 1;
    HmacSha256(&hmac, firstMessage, (size_t)saltLength + 4, block);
    memcpy(out, block, 32);
    for (int i = 1; i < iterations; i++) {
        HmacSha256(&hmac, block, 32, nextBlock);
        memcpy(block, nextBlock, 32);
        for (int j = 0; j < 32; j++) out[j] ^= block[j];
    }
    memset(&hmac, 0, sizeof hmac);
}
