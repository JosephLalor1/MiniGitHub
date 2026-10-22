/*
 * byte_buffer.h - a buffer of bytes that grows as data is added
 *
 * Used to build git objects, packs, zip files and web replies piece by
 * piece. Running out of memory is remembered (outOfMemory) rather than
 * crashing, so callers can check once at the end.
 */

#ifndef MINIGITHUB_BYTE_BUFFER_H
#define MINIGITHUB_BYTE_BUFFER_H

#include "common.h"

/* A growing buffer of bytes. */
typedef struct { uint8_t *data; size_t length, capacity; bool outOfMemory; } ByteBuffer;

/* Makes room for extra more bytes (doubling the capacity), or sets outOfMemory. */
void BufferReserve(ByteBuffer *buffer, size_t extra);

/* Adds count bytes to the end of the buffer. */
void BufferAppend(ByteBuffer *buffer, const void *bytes, size_t count);

/* Adds a string (without its terminating zero). */
void BufferAppendString(ByteBuffer *buffer, const char *text);

/* Adds one byte. */
void BufferAppendByte(ByteBuffer *buffer, uint8_t byte);

/* Frees the bytes and empties the buffer, ready to use again. */
void BufferFree(ByteBuffer *buffer);

#endif
