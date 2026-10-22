/*
 * git_pack_write.c - making the packs git receives
 *
 * For clone, fetch and pull: every commit, folder and file the other side
 * is missing, with new versions of files sent as small deltas of older
 * ones where that helps.
 */

#include "git_pack_write.h"
#include "byte_buffer.h"
#include "compression.h"
#include "git_commit.h"
#include "git_pack_read.h"
#include "model.h"
#include "repo.h"
#include "sha1.h"
#include "sha_map.h"
#include "util.h"

/* ---------- making a pack, for clone / fetch / pull ---------- */

/* A delta: the instructions that turn file version src into version dst
   ("copy bytes 0-4000 of the old one, add these 12 new bytes, copy..."),
   found by looking up 16-byte pieces of dst in an index of src.
   false if it wouldn't come out smaller than limit. */
#define DELTA_BLOCK 16
static void DeltaWriteSize(ByteBuffer *out, size_t value) { do { uint8_t byte = value & 0x7F; value >>= 7; if (value) byte |= 0x80; BufferAppendByte(out, byte); } while (value); }
static void DeltaWriteInsert(ByteBuffer *out, const uint8_t *bytes, size_t count)
{
    while (count) { size_t chunk = count > 127 ? 127 : count; BufferAppendByte(out, (uint8_t)chunk); BufferAppend(out, bytes, chunk); bytes += chunk; count -= chunk; }
}
static void DeltaWriteCopy(ByteBuffer *out, size_t offset, size_t count)
{
    while (count) {
        size_t chunk = count > 0xFFFFFF ? 0xFFFFFF : count;
        uint8_t command = 0x80, args[8];
        int argCount = 0;
        for (int i = 0; i < 4; i++) if ((offset >> (8 * i)) & 0xFF) { command |= (uint8_t)(1 << i); args[argCount++] = (uint8_t)(offset >> (8 * i)); }
        for (int i = 0; i < 3; i++) if ((chunk >> (8 * i)) & 0xFF) { command |= (uint8_t)(16 << i); args[argCount++] = (uint8_t)(chunk >> (8 * i)); }
        BufferAppendByte(out, command); BufferAppend(out, args, (size_t)argCount);
        offset += chunk; count -= chunk;
    }
}
static uint32_t BlockHash(const uint8_t *block)
{
    uint32_t hash = 0;
    for (int i = 0; i < DELTA_BLOCK; i++) hash = hash * 257u + block[i];
    return hash;
}
static bool MakeDelta(const uint8_t *src, size_t srcLength, const uint8_t *dst, size_t dstLength, ByteBuffer *out, size_t limit)
{
    if (srcLength < DELTA_BLOCK * 4 || dstLength < DELTA_BLOCK * 4 || srcLength > 0x7FFFFFFF) return false;
    size_t blockCount = srcLength / DELTA_BLOCK, tableSize = 64;
    while (tableSize < blockCount * 2) tableSize *= 2;
    int *bucketHead = malloc(tableSize * sizeof(int)), *bucketNext = malloc(blockCount * sizeof(int));
    if (!bucketHead || !bucketNext) { free(bucketHead); free(bucketNext); return false; }
    for (size_t i = 0; i < tableSize; i++) bucketHead[i] = -1;
    int shift = 32;
    for (size_t size = tableSize; size > 1; size >>= 1) shift--;
    for (size_t i = 0; i < blockCount; i++) {
        uint32_t bucket = (BlockHash(src + i * DELTA_BLOCK) * 2654435761u) >> shift;
        bucketNext[i] = bucketHead[bucket]; bucketHead[bucket] = (int)i;
    }
    uint32_t topFactor = 1;                                       /* 257^(DELTA_BLOCK-1), to roll the hash along */
    for (int i = 0; i < DELTA_BLOCK - 1; i++) topFactor *= 257u;
    size_t start = out->length;
    DeltaWriteSize(out, srcLength); DeltaWriteSize(out, dstLength);
    size_t at = 0, insertFrom = 0;
    uint32_t hash = BlockHash(dst);
    bool ok = true;
    while (at + DELTA_BLOCK <= dstLength) {
        size_t bestLen = 0, bestPos = 0;
        int tries = 0;
        for (int candidate = bucketHead[(hash * 2654435761u) >> shift]; candidate >= 0 && tries < 8; candidate = bucketNext[candidate], tries++) {
            size_t candidatePos = (size_t)candidate * DELTA_BLOCK;
            if (memcmp(src + candidatePos, dst + at, DELTA_BLOCK) != 0) continue;
            size_t matchLength = DELTA_BLOCK;
            while (candidatePos + matchLength < srcLength && at + matchLength < dstLength && src[candidatePos + matchLength] == dst[at + matchLength]) matchLength++;
            if (matchLength > bestLen) { bestLen = matchLength; bestPos = candidatePos; }
        }
        if (bestLen >= DELTA_BLOCK) {
            while (bestPos > 0 && at > insertFrom && src[bestPos - 1] == dst[at - 1]) { bestPos--; at--; bestLen++; }
            DeltaWriteInsert(out, dst + insertFrom, at - insertFrom);
            DeltaWriteCopy(out, bestPos, bestLen);
            at += bestLen; insertFrom = at;
            if (out->length - start > limit) { ok = false; break; }
            if (at + DELTA_BLOCK <= dstLength) hash = BlockHash(dst + at);
            continue;
        }
        if (at + DELTA_BLOCK < dstLength) hash = (hash - dst[at] * topFactor) * 257u + dst[at + DELTA_BLOCK];
        at++;
    }
    if (ok) {
        DeltaWriteInsert(out, dst + insertFrom, dstLength - insertFrom);
        ok = out->length - start <= limit && !out->outOfMemory;
    }
    free(bucketHead); free(bucketNext);
    if (!ok) out->length = start;
    return ok;
}

typedef struct { int type; const uint8_t *data; size_t length; size_t storeOffset; bool stored; uint8_t baseSha[20]; } OutgoingObject;
typedef struct { OutgoingObject *objects; int count, capacity; ShaMap present; ByteBuffer store; } OutgoingObjects;

static void OutgoingAdd(OutgoingObjects *set, int type, const uint8_t sha[20], const uint8_t *data, size_t length, bool copy)
{
    if (ShaMapGet(&set->present, sha) >= 0) return;
    set->objects = GrowArray(set->objects, &set->capacity, set->count + 1, sizeof(OutgoingObject));
    if (set->count >= set->capacity) return;
    OutgoingObject *object = &set->objects[set->count];
    object->type = type; object->length = length;
    if (copy) { object->stored = true; object->storeOffset = set->store.length; BufferAppend(&set->store, data, length); object->data = NULL; }
    else { object->stored = false; object->data = data; }
    ShaMapPut(&set->present, sha, set->count++);
}
static void OutgoingAddTree(void *context, const uint8_t sha[20], const uint8_t *data, size_t length) { OutgoingAdd(context, OBJ_TREE, sha, data, length, true); }
static ShaMap knownTrees;                                   /* trees the other side already has */
static void RememberKnownTree(void *context, const uint8_t sha[20], const uint8_t *data, size_t length)
{
    (void)context; (void)data; (void)length;
    ShaMapPut(&knownTrees, sha, 1);
}
static void OutgoingAddNewTree(void *context, const uint8_t sha[20], const uint8_t *data, size_t length)
{
    if (ShaMapGet(&knownTrees, sha) < 0) OutgoingAddTree(context, sha, data, length);
}

/* Marks commit tip and everything it was built on, following the parents written in
   each commit's git data (MarkHistory in branches.c uses the parents[] list instead). */
static void MarkHistoryFromRaw(Repo *repo, int tip, uint8_t *mark, uint8_t bit)
{
    int *stack = NULL, stackSize = 0, stackCapacity = 0;
    stack = GrowArray(stack, &stackCapacity, 1, sizeof(int));
    if (!stackCapacity) return;
    stack[stackSize++] = tip;
    while (stackSize) {
        int commitIndex = stack[--stackSize];
        if (mark[commitIndex] & bit) continue;
        mark[commitIndex] |= bit;
        CommitInfo info;
        if (!ParseCommit(repo->commits[commitIndex].raw, repo->commits[commitIndex].rawLen, &info)) continue;
        for (int parentNo = 0; parentNo < info.parentCount; parentNo++) {
            int parent = CommitBySha(repo, info.parents[parentNo]);
            if (parent < 0 || (mark[parent] & bit)) continue;
            stack = GrowArray(stack, &stackCapacity, stackSize + 1, sizeof(int));
            if (stackSize < stackCapacity) stack[stackSize++] = parent;
        }
    }
    free(stack);
}

/* The pack for "I want these commits; I already have those". */
bool BuildPack(Repo *repo, const int *wants, int wantCount, const int *haves, int haveCount, ByteBuffer *out)
{
    uint8_t *marks = calloc((size_t)(repo->commitCount ? repo->commitCount : 1), 1);
    uint8_t *blobSent = calloc((size_t)(repo->blobCount ? repo->blobCount : 1), 1);
    if (!marks || !blobSent) { free(marks); free(blobSent); return false; }
    for (int i = 0; i < haveCount; i++) MarkHistoryFromRaw(repo, haves[i], marks, 2);
    for (int i = 0; i < wantCount; i++) MarkHistoryFromRaw(repo, wants[i], marks, 1);

    /* what the other side has right next to what it's getting: don't send those files and folders again */
    ShaMapFree(&knownTrees);
    uint8_t treeSha[20];
    for (int j = 0; j < repo->commitCount; j++) {           /* a commit they have, with a child they don't */
        if (marks[j] != 1) continue;
        for (int parentFile = 0; parentFile < repo->commits[j].parentCount; parentFile++) {
            int k = repo->commits[j].parents[parentFile];
            if (marks[k] & 2) marks[k] |= 4;
        }
    }
    for (int k = 0; k < repo->commitCount; k++) {
        if (!(marks[k] & 4)) continue;
        marks[k] &= 3;
        Commit *commit = &repo->commits[k];
        for (int fileIndex = 0; fileIndex < commit->fileCount; fileIndex++) blobSent[commit->blobs[fileIndex]] = 1;
        CommitTree(repo, commit, treeSha, RememberKnownTree, NULL);
    }

    OutgoingObjects set;
    memset(&set, 0, sizeof set);
    /* a file's new version usually goes as a delta of the version before it (if that's in this pack too) */
    uint8_t *blobDepth = calloc((size_t)(repo->blobCount ? repo->blobCount : 1), 1);  /* 0 = not in the pack; else 1 + delta depth */
    ByteBuffer delta = {0};
    for (int k = 0; k < repo->commitCount && blobDepth; k++) {
        if (marks[k] != 1) continue;                        /* wanted, and they don't have it */
        Commit *commit = &repo->commits[k], *parent = ParentOf(repo, k);
        OutgoingAdd(&set, OBJ_COMMIT, commit->sha, (const uint8_t *)commit->raw, (size_t)commit->rawLen, false);
        CommitTree(repo, commit, treeSha, OutgoingAddNewTree, &set);
        for (int fileIndex = 0; fileIndex < commit->fileCount; fileIndex++) {
            int blobIndex = commit->blobs[fileIndex];
            if (blobSent[blobIndex]) continue;
            blobSent[blobIndex] = 1;
            Blob *blob = &repo->blobs[blobIndex];
            int parentFile = parent ? FindInCommit(parent, commit->names[fileIndex]) : -1, parentBlob = parentFile >= 0 ? parent->blobs[parentFile] : -1;
            delta.length = 0;
            if (parentBlob >= 0 && blobDepth[parentBlob] && blobDepth[parentBlob] < 50 &&
                MakeDelta((const uint8_t *)repo->blobs[parentBlob].data, (size_t)repo->blobs[parentBlob].length, (const uint8_t *)blob->data, (size_t)blob->length, &delta, (size_t)blob->length / 2)) {
                int countBefore = set.count;
                OutgoingAdd(&set, OBJ_REF_DELTA, blob->sha, delta.data, delta.length, true);
                if (set.count > countBefore) memcpy(set.objects[countBefore].baseSha, repo->blobs[parentBlob].sha, 20);
                blobDepth[blobIndex] = (uint8_t)(blobDepth[parentBlob] + 1);
            } else {
                OutgoingAdd(&set, OBJ_BLOB, blob->sha, (const uint8_t *)blob->data, (size_t)blob->length, false);
                blobDepth[blobIndex] = 1;
            }
        }
    }
    BufferFree(&delta);
    bool built = blobDepth != NULL;
    free(blobDepth);
    free(marks); free(blobSent);
    ShaMapFree(&knownTrees);
    bool ok = !set.store.outOfMemory && built;

    /* write it: header, each object (kind + size, then zlib), then a SHA-1 of it all */
    size_t start = out->length;
    BufferAppendString(out, "PACK");
    uint8_t header[8] = {0, 0, 0, 2, (uint8_t)(set.count >> 24), (uint8_t)(set.count >> 16), (uint8_t)(set.count >> 8), (uint8_t)set.count};
    BufferAppend(out, header, 8);
    for (int i = 0; i < set.count && ok; i++) {
        OutgoingObject *object = &set.objects[i];
        const uint8_t *data = object->stored ? set.store.data + object->storeOffset : object->data;
        size_t size = object->length;
        uint8_t headerByte = (uint8_t)(object->type << 4 | (size & 15));
        size >>= 4;
        while (size) { BufferAppendByte(out, headerByte | 0x80); headerByte = size & 0x7F; size >>= 7; }
        BufferAppendByte(out, headerByte);
        if (object->type == OBJ_REF_DELTA) BufferAppend(out, object->baseSha, 20);  /* which object it's a delta of */
        ZlibCompress(out, data ? data : (const uint8_t *)"", object->length);
        if (out->outOfMemory) ok = false;
    }
    if (ok) {
        Sha1 sha;
        uint8_t checksum[20];
        Sha1Init(&sha); Sha1Update(&sha, out->data + start, out->length - start); Sha1Final(&sha, checksum);
        BufferAppend(out, checksum, 20);
    }
    free(set.objects); ShaMapFree(&set.present); BufferFree(&set.store);
    return ok && !out->outOfMemory;
}
