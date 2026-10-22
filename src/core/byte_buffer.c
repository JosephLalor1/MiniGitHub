/*
 * byte_buffer.c - a buffer of bytes that grows as data is added
 *
 * Used to build git objects, packs, zip files and web replies piece by
 * piece. Running out of memory is remembered (outOfMemory) rather than
 * crashing, so callers can check once at the end.
 */

#include "byte_buffer.h"

/* Makes room for extra more bytes (doubling the capacity), or sets outOfMemory. */
void BufferReserve(ByteBuffer *buffer, size_t extra)
{
    if (buffer->outOfMemory) return;
    if (buffer->length + extra <= buffer->capacity) return;
    size_t newCapacity = buffer->capacity ? buffer->capacity : 256;
    while (newCapacity < buffer->length + extra) newCapacity *= 2;
    uint8_t *grown = realloc(buffer->data, newCapacity);
    if (!grown) { buffer->outOfMemory = true; return; }
    buffer->data = grown; buffer->capacity = newCapacity;
}
/* Adds count bytes to the end of the buffer. */
void BufferAppend(ByteBuffer *buffer, const void *bytes, size_t count) { BufferReserve(buffer, count); if (buffer->outOfMemory) return; memcpy(buffer->data + buffer->length, bytes, count); buffer->length += count; }
/* Adds a string (without its terminating zero). */
void BufferAppendString(ByteBuffer *buffer, const char *text) { BufferAppend(buffer, text, strlen(text)); }
/* Adds one byte. */
void BufferAppendByte(ByteBuffer *buffer, uint8_t byte) { BufferAppend(buffer, &byte, 1); }
/* Frees the bytes and empties the buffer, ready to use again. */
void BufferFree(ByteBuffer *buffer) { free(buffer->data); memset(buffer, 0, sizeof *buffer); }
