/*
 * git_pack_read.h - reading the packs git sends
 *
 * When you push, git sends a pack: every new object squeezed with zlib,
 * some as deltas of others. This unpacks them all and turns every delta
 * back into the object it describes.
 */

#ifndef MINIGITHUB_GIT_PACK_READ_H
#define MINIGITHUB_GIT_PACK_READ_H

#include "common.h"
#include "model.h"
#include "sha_map.h"

enum { OBJ_COMMIT = 1, OBJ_TREE = 2, OBJ_BLOB = 3, OBJ_TAG = 4, OBJ_OFS_DELTA = 6, OBJ_REF_DELTA = 7 };
#define MAX_PUSH   (512u * 1024 * 1024)       /* biggest request git can send: 512 MB */

typedef struct {
    size_t   offset;               /* where it starts in the pack           */
    int      type;                 /* OBJ_..., for deltas the final type    */
    uint8_t *data; size_t length;  /* the object (or, until resolved, the delta) */
    bool     isDelta, resolved, resolving;
    size_t   baseOffset;         /* OBJ_OFS_DELTA: where its base starts  */
    uint8_t  baseSha[20];        /* OBJ_REF_DELTA: its base's fingerprint */
    int      baseType;
    uint8_t  sha[20];
} PackEntry;

typedef struct { PackEntry *objects; int count, capacity; ShaMap map; } PackContents;

/* Frees every object of an unpacked pack. */
void PackFree(PackContents *pack);

/* Unpacks a whole pack. Returns NULL if it worked, or what was wrong. */
const char *ReadPack(const uint8_t *data, size_t length, PackContents *pack, Repo *repo);

#endif
