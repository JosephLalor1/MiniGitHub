/*
 * snapshot.c - a list of files being put together into a new commit
 *
 * Kept sorted by path, like the file list of a commit.
 */

#include "snapshot.h"
#include "git_paths.h"
#include "model.h"

/* ---------- snapshots being put together ----------
   A list of files (kept sorted) that becomes a new commit. */

/* Frees a snapshot's lists and empties it. */
void SnapshotFree(Snapshot *snap) { free(snap->names); free(snap->blobs); free(snap->modes); memset(snap, 0, sizeof *snap); }
/* Makes room for needed files. False if out of memory. */
bool SnapshotReserve(Snapshot *snap, int needed)
{
    if (needed <= snap->capacity) return true;
    int newCapacity = snap->capacity ? snap->capacity : 16;
    while (newCapacity < needed) newCapacity *= 2;
    const char **names = realloc(snap->names, (size_t)newCapacity * sizeof *names);
    if (names) snap->names = names;
    int *blobs = realloc(snap->blobs, (size_t)newCapacity * sizeof *blobs);
    if (blobs) snap->blobs = blobs;
    uint8_t *modes = realloc(snap->modes, (size_t)newCapacity);
    if (modes) snap->modes = modes;
    if (!names || !blobs || !modes) return false;
    snap->capacity = newCapacity;
    return true;
}
/* Starts from a commit's files (or from nothing if commit is NULL). */
bool SnapshotFromCommit(Snapshot *snap, Commit *commit)
{
    memset(snap, 0, sizeof *snap);
    int count = commit ? commit->fileCount : 0;
    if (!SnapshotReserve(snap, count > 0 ? count : 1)) return false;
    if (count) {
        memcpy(snap->names, commit->names, (size_t)count * sizeof *snap->names);
        memcpy(snap->blobs, commit->blobs, (size_t)count * sizeof *snap->blobs);
        memcpy(snap->modes, commit->modes, (size_t)count);
    }
    snap->count = count;
    return true;
}
/* Where name is, or where it would go (as -1 - position). */
int SnapshotFind(Snapshot *snap, const char *name)
{
    int low = 0, high = snap->count - 1;
    while (low <= high) {
        int mid = (low + high) / 2, order = strcmp(snap->names[mid], name);
        if (!order) return mid;
        if (order < 0) low = mid + 1; else high = mid - 1;
    }
    return -1 - low;
}
/* Puts a file in (or replaces it). mode -1 = keep its mode (a normal file if it's new). */
bool SnapshotSet(Snapshot *snap, const char *name, int blob, int mode)
{
    int index = SnapshotFind(snap, name);
    if (index >= 0) { snap->blobs[index] = blob; if (mode >= 0) snap->modes[index] = (uint8_t)mode; return true; }
    if (snap->count >= MAX_FILES) return false;
    const char *interned = InternPath(name);
    if (!interned || !SnapshotReserve(snap, snap->count + 1)) return false;
    index = -1 - index;
    memmove(&snap->names[index + 1], &snap->names[index], (size_t)(snap->count - index) * sizeof *snap->names);
    memmove(&snap->blobs[index + 1], &snap->blobs[index], (size_t)(snap->count - index) * sizeof *snap->blobs);
    memmove(&snap->modes[index + 1], &snap->modes[index], (size_t)(snap->count - index));
    snap->names[index] = interned; snap->blobs[index] = blob; snap->modes[index] = (uint8_t)(mode >= 0 ? mode : MODE_FILE);
    snap->count++;
    return true;
}
/* Takes a file out of the snapshot (if it's there). */
void SnapshotRemove(Snapshot *snap, const char *name)
{
    int index = SnapshotFind(snap, name);
    if (index < 0) return;
    memmove(&snap->names[index], &snap->names[index + 1], (size_t)(snap->count - index - 1) * sizeof *snap->names);
    memmove(&snap->blobs[index], &snap->blobs[index + 1], (size_t)(snap->count - index - 1) * sizeof *snap->blobs);
    memmove(&snap->modes[index], &snap->modes[index + 1], (size_t)(snap->count - index - 1));
    snap->count--;
}
