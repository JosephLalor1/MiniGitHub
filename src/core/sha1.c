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

/* Mixes one 64-byte block into the running hash. The short names (w, a-e, f, k)
   are the ones the SHA-1 standard (FIPS 180-4) uses, so the code can be checked against it.

   It is written out round by round (SHA1_FIVE_ROUNDS below) because that is what
   every reasonably fast SHA-1 does - over three times faster than a loop here, which
   matters: every blob is hashed each time the save file is loaded. Two tricks:
     - In the standard, each round ends by shifting the working values along
       (e = d; d = c; ...). Written out, the shifting is done by naming the values
       in a different order in each round instead, so nothing actually moves.
     - The message schedule w[] is only ever needed 16 words back, so it is kept in
       a ring of 16 words and each new word is worked out when its round needs it. */
static void Sha1ProcessBlock(Sha1 *sha, const uint8_t *block)
{
    uint32_t w[16];
    for (int i = 0; i < 16; i++) w[i] = (uint32_t)block[i * 4] << 24 | (uint32_t)block[i * 4 + 1] << 16 | (uint32_t)block[i * 4 + 2] << 8 | block[i * 4 + 3];
    uint32_t a = sha->state[0], b = sha->state[1], c = sha->state[2], d = sha->state[3], e = sha->state[4];

    /* word i of the schedule (rounds 0-15 use the block itself) */
#define SHA1_W(i) ((i) < 16 ? w[i] : (w[(i) & 15] = ROTATE_LEFT(w[((i) - 3) & 15] ^ w[((i) - 8) & 15] ^ w[((i) - 14) & 15] ^ w[(i) & 15], 1)))
    /* the mixing function of each group of 20 rounds (written the cheapest equivalent way) */
#define SHA1_CHOOSE(b, c, d)   ((d) ^ ((b) & ((c) ^ (d))))          /* (b & c) | (~b & d)          */
#define SHA1_PARITY(b, c, d)   ((b) ^ (c) ^ (d))
#define SHA1_MAJORITY(b, c, d) (((b) & (c)) | ((d) & ((b) | (c))))  /* (b & c) | (b & d) | (c & d) */
#define SHA1_ROUND(a, b, c, d, e, f, k, i) do { e += ROTATE_LEFT(a, 5) + f(b, c, d) + (k) + SHA1_W(i); b = ROTATE_LEFT(b, 30); } while (0)
#define SHA1_FIVE_ROUNDS(f, k, i) do { SHA1_ROUND(a, b, c, d, e, f, k, i);     SHA1_ROUND(e, a, b, c, d, f, k, i + 1); \
                                       SHA1_ROUND(d, e, a, b, c, f, k, i + 2); SHA1_ROUND(c, d, e, a, b, f, k, i + 3); \
                                       SHA1_ROUND(b, c, d, e, a, f, k, i + 4); } while (0)
    SHA1_FIVE_ROUNDS(SHA1_CHOOSE,   0x5A827999, 0);  SHA1_FIVE_ROUNDS(SHA1_CHOOSE,   0x5A827999, 5);
    SHA1_FIVE_ROUNDS(SHA1_CHOOSE,   0x5A827999, 10); SHA1_FIVE_ROUNDS(SHA1_CHOOSE,   0x5A827999, 15);
    SHA1_FIVE_ROUNDS(SHA1_PARITY,   0x6ED9EBA1, 20); SHA1_FIVE_ROUNDS(SHA1_PARITY,   0x6ED9EBA1, 25);
    SHA1_FIVE_ROUNDS(SHA1_PARITY,   0x6ED9EBA1, 30); SHA1_FIVE_ROUNDS(SHA1_PARITY,   0x6ED9EBA1, 35);
    SHA1_FIVE_ROUNDS(SHA1_MAJORITY, 0x8F1BBCDC, 40); SHA1_FIVE_ROUNDS(SHA1_MAJORITY, 0x8F1BBCDC, 45);
    SHA1_FIVE_ROUNDS(SHA1_MAJORITY, 0x8F1BBCDC, 50); SHA1_FIVE_ROUNDS(SHA1_MAJORITY, 0x8F1BBCDC, 55);
    SHA1_FIVE_ROUNDS(SHA1_PARITY,   0xCA62C1D6, 60); SHA1_FIVE_ROUNDS(SHA1_PARITY,   0xCA62C1D6, 65);
    SHA1_FIVE_ROUNDS(SHA1_PARITY,   0xCA62C1D6, 70); SHA1_FIVE_ROUNDS(SHA1_PARITY,   0xCA62C1D6, 75);
#undef SHA1_W
#undef SHA1_CHOOSE
#undef SHA1_PARITY
#undef SHA1_MAJORITY
#undef SHA1_ROUND
#undef SHA1_FIVE_ROUNDS
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
