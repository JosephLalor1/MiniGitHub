/*
 * work_file.h - a file being edited
 *
 * Its bytes, plus a version number that goes up with every change so
 * anything worked out from it (its language, a picture preview) knows when
 * to look again.
 */

#ifndef MINIGITHUB_WORK_FILE_H
#define MINIGITHUB_WORK_FILE_H

#include "common.h"
#include "model.h"

/* The same rule real git uses: a file is "binary" (not text) if there's a zero byte anywhere in
   its first 8000 bytes. Text never contains one. */
bool LooksBinary(const char *data, int length);
extern int fileContentsGeneration;

/* Sets a working file to any bytes (text or binary). */
void WorkFileSetBytes(WorkFile *file, const char *data, int length);

/* Sets a working file to a string. */
void WorkFileSetText(WorkFile *file, const char *text);

/* Inserts count bytes at position at. */
void WorkFileInsert(WorkFile *file, int at, const char *bytes, int count);

/* Removes count bytes starting at position at. */
void WorkFileDelete(WorkFile *file, int at, int count);

/* Frees a working file's text and language clues. */
void WorkFileFree(WorkFile *file);

#endif
