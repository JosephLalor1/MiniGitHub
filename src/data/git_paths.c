/*
 * git_paths.c - file paths as git sees them
 *
 * One shared copy of each path (so commits can compare paths by pointer),
 * the rules for a usable path, and git's file modes.
 */

#include "git_paths.h"
#include "util.h"

/* ---------- file paths ----------
   Every path ("src/main.c") is kept once and shared; commits just point at
   it. That saves memory and lets paths be compared by pointer. */
static char **internedPaths;
static int    internedCount, internedCapacity, *internTable, internTableSize;
/* The one shared copy of this path (made the first time), or NULL if out of memory. */
const char *InternPath(const char *path)
{
    uint32_t hash = HashFnv1a(path, (int)strlen(path), FNV_OFFSET_BASIS);
    if ((internedCount + 1) * 2 > internTableSize) {
        int newSize = internTableSize ? internTableSize * 2 : 1024;
        int *table = malloc((size_t)newSize * sizeof(int));
        if (!table) return NULL;
        for (int i = 0; i < newSize; i++) table[i] = -1;
        for (int k = 0; k < internedCount; k++) {
            uint32_t slot = HashFnv1a(internedPaths[k], (int)strlen(internedPaths[k]), FNV_OFFSET_BASIS) & (uint32_t)(newSize - 1);
            while (table[slot] >= 0) slot = (slot + 1) & (uint32_t)(newSize - 1);
            table[slot] = k;
        }
        free(internTable);
        internTable = table; internTableSize = newSize;
    }
    uint32_t slot = hash & (uint32_t)(internTableSize - 1);
    for (; internTable[slot] >= 0; slot = (slot + 1) & (uint32_t)(internTableSize - 1))
        if (strcmp(internedPaths[internTable[slot]], path) == 0) return internedPaths[internTable[slot]];
    internedPaths = GrowArray(internedPaths, &internedCapacity, internedCount + 1, sizeof(char *));
    if (internedCount >= internedCapacity) return NULL;
    size_t length = strlen(path);
    char *copy = malloc(length + 1);
    if (!copy) return NULL;
    memcpy(copy, path, length + 1);
    internedPaths[internedCount] = copy;
    internTable[slot] = internedCount++;
    return copy;
}

/* Is this a sensible path for a file: "a/b.txt" yes; "", "/a", "a//b", "a/../b", "a/" no. */
bool IsValidPath(const char *path)
{
    size_t length = strlen(path);
    if (!length || length >= PATH_LEN || path[0] == '/' || path[length - 1] == '/') return false;
    const char *part = path;
    for (;;) {
        const char *slash = strchr(part, '/');
        size_t partLength = slash ? (size_t)(slash - part) : strlen(part);
        if (partLength == 0) return false;
        if ((partLength == 1 && part[0] == '.') || (partLength == 2 && part[0] == '.' && part[1] == '.')) return false;
        if (partLength == 4 && strncmp(part, ".git", 4) == 0) return false;
        if (!slash) break;
        part = slash + 1;
    }
    for (size_t i = 0; i < length; i++) if ((unsigned char)path[i] < 32 || path[i] == '\\') return false;
    return true;
}

/* ---------- file modes ----------
   Git remembers whether a file is a normal file, a program you can run,
   or a "symbolic link" (a pointer to another path). */

/* The mode git writes for a file: "100644", "100755" or "120000". */
const char *GitModeText(int mode) { return mode == MODE_EXEC ? "100755" : mode == MODE_LINK ? "120000" : "100644"; }
