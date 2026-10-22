/*
 * work_file.c - a file being edited
 *
 * Its bytes, plus a version number that goes up with every change so
 * anything worked out from it (its language, a picture preview) knows when
 * to look again.
 */

#include "work_file.h"
#include "model.h"

/* ------------------------------------------------------------------ */
/* Working files                                                      */
/* ------------------------------------------------------------------ */

/* The same rule real git uses: a file is "binary" (not text) if there's a
   zero byte anywhere in its first 8000 bytes. Text never contains one. */
bool LooksBinary(const char *data, int length)
{
    int checkLength = length < 8000 ? length : 8000;
    return memchr(data, 0, (size_t)checkLength) != NULL;
}

/* Goes up by one whenever file contents are replaced or freed, so a picture
   preview made earlier is never mistaken for the new contents. */
int fileContentsGeneration;

/* Sets a working file to any bytes (text or binary). */
void WorkFileSetBytes(WorkFile *file, const char *data, int length)
{
    fileContentsGeneration++;
    file->version++;
    if (file->capacity < length + 1) { file->capacity = length + 64; file->text = realloc(file->text, (size_t)file->capacity); }
    memcpy(file->text, data, (size_t)length);
    file->text[length] = 0;                          /* always ends in a zero, handy for text */
    file->length = length;
    file->binary = LooksBinary(data, length);
}
/* Sets a working file to a string. */
void WorkFileSetText(WorkFile *file, const char *text) { WorkFileSetBytes(file, text, (int)strlen(text)); }
/* Inserts count bytes at position at. */
void WorkFileInsert(WorkFile *file, int at, const char *bytes, int count)
{
    if (file->length + count + 1 > file->capacity) { file->capacity = (file->length + count + 1) * 2; file->text = realloc(file->text, (size_t)file->capacity); }
    memmove(file->text + at + count, file->text + at, (size_t)(file->length - at + 1));
    memcpy(file->text + at, bytes, (size_t)count);
    file->length += count;
    file->version++;
}
/* Removes count bytes starting at position at. */
void WorkFileDelete(WorkFile *file, int at, int count)
{
    memmove(file->text + at, file->text + at + count, (size_t)(file->length - at - count + 1));
    file->length -= count;
    file->version++;
}
/* Frees a working file's text and language clues. */
void WorkFileFree(WorkFile *file) { free(file->text); file->text = NULL; file->length = file->capacity = 0; free(file->lang); file->lang = NULL; fileContentsGeneration++; }
