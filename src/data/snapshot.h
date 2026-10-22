/*
 * snapshot.h - a list of files being put together into a new commit
 *
 * Kept sorted by path, like the file list of a commit.
 */

#ifndef MINIGITHUB_SNAPSHOT_H
#define MINIGITHUB_SNAPSHOT_H

#include "common.h"
#include "model.h"

typedef struct { int count, capacity; const char **names; int *blobs; uint8_t *modes; } Snapshot;

/* Frees a snapshot's lists and empties it. */
void SnapshotFree(Snapshot *snap);

/* Makes room for needed files. False if out of memory. */
bool SnapshotReserve(Snapshot *snap, int needed);

/* Starts from a commit's files (or from nothing if commit is NULL). */
bool SnapshotFromCommit(Snapshot *snap, Commit *commit);

/* Where name is, or where it would go (as -1 - position). */
int SnapshotFind(Snapshot *snap, const char *name);

/* Puts a file in (or replaces it). mode -1 = keep its mode (a normal file if it's new). */
bool SnapshotSet(Snapshot *snap, const char *name, int blob, int mode);

/* Takes a file out of the snapshot (if it's there). */
void SnapshotRemove(Snapshot *snap, const char *name);

#endif
