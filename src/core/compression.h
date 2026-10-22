/*
 * compression.h - zlib / DEFLATE in both directions
 *
 * Unpacking everything git sends (and gzip-compressed requests), and
 * squeezing the objects in the packs Mini GitHub sends and the files in
 * zip downloads.
 */

#ifndef MINIGITHUB_COMPRESSION_H
#define MINIGITHUB_COMPRESSION_H

#include "common.h"
#include "byte_buffer.h"

/* zlib = 2-byte header + DEFLATE + Adler-32 checksum. */
uint8_t *InflateZlib(const uint8_t *input, size_t inputLength, size_t *bytesUsed, size_t *outputLength, size_t maxOutput);

/* gzip (used by git for big requests) = a header + DEFLATE + a CRC and the size. */
uint8_t *InflateGzip(const uint8_t *input, size_t inputLength, size_t *outputLength, size_t maxOutput);

/* Squeezes data with DEFLATE (fixed Huffman codes) onto the end of out. False if the result would
   be bigger than limit. */
bool DeflateRaw(const uint8_t *data, size_t length, ByteBuffer *out, size_t limit);

/* Squeezes data into zlib format: DEFLATE when that makes it smaller, otherwise "stored" blocks
   (no squeezing, still valid). */
void ZlibCompress(ByteBuffer *out, const uint8_t *data, size_t length);

#endif
