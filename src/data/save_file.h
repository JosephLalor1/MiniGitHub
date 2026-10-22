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

/* Writes everything to minigithub.dat (via a temporary file, so a crash never leaves half a file).
   Never writes over a file that failed to load. */
void Save(void);

/* Reads every version: 7 (git commits, folders), 6 (forks), 5 (collaborators + drafts), 4
   (profiles + recovery codes), 3 (either one), 2 (accounts), 1 (before accounts). */
bool Load(void);

#endif
