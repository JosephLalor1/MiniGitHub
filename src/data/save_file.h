/*
 * save_file.h - saving and loading minigithub.dat
 *
 * Writes everything to the save file and reads it back - including save
 * files from every older version of Mini GitHub, and histories an older
 * version stored out of order.
 */

#ifndef MINIGITHUB_SAVE_FILE_H
#define MINIGITHUB_SAVE_FILE_H

#include "common.h"

extern bool loadFailed;             /* the save file couldn't be read: nothing is saved until you decide */
extern char loadReason[TEXT_LEN];   /* what went wrong, in words */
extern char loadContext[TEXT_LEN];  /* e.g. "the repository jo/hello-world" */

/* Which number each path has in the save file (paths are written once per repository). */
typedef struct { const char **paths; int *numbers; int capacity, count; } PathIndexMap;

/* The number given to this path, or -1. */
int PathIndexGet(PathIndexMap *map, const char *path);

/* Gives a path a number. */
void PathIndexPut(PathIndexMap *map, const char *path, int number);

/* Writes everything to minigithub.dat (via a temporary file, so a crash never leaves half a file),
   and returns once it's written. Never writes over a file that failed to load. */
void Save(void);

/* The same, but the writing is done on another thread so the window doesn't freeze: what to
   write is gathered now, and written while the program carries on. False if the save before is
   still being written (try again later). */
bool SaveInBackground(void);

/* Called once a frame: tidies up after a background save that's finished (and says if it failed). */
void CheckOnSave(void);

/* Waits for a background save to finish. Anything that frees a file's stored contents
   (deleting a repository...) waits first, since the save may still be writing them. */
void WaitForSave(void);

/* Reads every version: 11 (following), 10 (private repositories), 9 (other emails), 8 (branches),
   7 (git commits, folders), 6 (forks), 5 (collaborators + drafts), 4
   (profiles + recovery codes), 3 (either one), 2 (accounts), 1 (before accounts). */
bool Load(void);

/* Copies a file a piece at a time, under a temporary name until it's complete (so a
   copy cut short never replaces a good file). False if it couldn't be made. */
bool CopyFileBytes(const char *fromPath, const char *toPath);

#endif
