/*
 * git_receive.c - taking in a push
 *
 * Finds the new commits in a pack, checks they build on the branch,
 * unfolds their folders and stores everything.
 */

#include "git_receive.h"
#include "branches.h"
#include "byte_buffer.h"
#include "forks.h"
#include "git_commit.h"
#include "git_pack_read.h"
#include "git_paths.h"
#include "model.h"
#include "repo.h"
#include "sha1.h"
#include "sha_map.h"
#include "users.h"
#include "util.h"

/* ---------- every folder (tree) the repository has ----------
   A push only sends what's new, so a commit's unchanged folders are
   looked up here. Kept between pushes and topped up with new commits. */
static struct {
    const Repo *repo; int commitsDone; uint8_t lastCommitSha[20];
    ShaMap map; ByteBuffer trees; size_t *offsets; int *lengths; int count, capacity;
} treeIndex;
static void TreeIndexReset(void)
{
    ShaMapFree(&treeIndex.map); BufferFree(&treeIndex.trees); free(treeIndex.offsets); free(treeIndex.lengths);
    memset(&treeIndex, 0, sizeof treeIndex);
}
static void TreeIndexAdd(void *context, const uint8_t sha[20], const uint8_t *data, size_t length)
{
    (void)context;
    if (ShaMapGet(&treeIndex.map, sha) >= 0) return;
    if (treeIndex.count >= treeIndex.capacity) {
        int newCapacity = treeIndex.capacity ? treeIndex.capacity * 2 : 256;
        size_t *offsets = realloc(treeIndex.offsets, (size_t)newCapacity * sizeof *offsets);
        if (offsets) treeIndex.offsets = offsets;
        int *lengths = realloc(treeIndex.lengths, (size_t)newCapacity * sizeof *lengths);
        if (lengths) treeIndex.lengths = lengths;
        if (!offsets || !lengths) return;
        treeIndex.capacity = newCapacity;
    }
    treeIndex.offsets[treeIndex.count] = treeIndex.trees.length;
    treeIndex.lengths[treeIndex.count] = (int)length;
    BufferAppend(&treeIndex.trees, data, length);
    ShaMapPut(&treeIndex.map, sha, treeIndex.count++);
}
static void TreeIndexUpdate(Repo *repo)
{
    if (treeIndex.repo != repo || treeIndex.commitsDone > repo->commitCount || (treeIndex.commitsDone && memcmp(repo->commits[treeIndex.commitsDone - 1].sha, treeIndex.lastCommitSha, 20) != 0)) {
        TreeIndexReset();
        treeIndex.repo = repo;
    }
    uint8_t treeSha[20];
    for (; treeIndex.commitsDone < repo->commitCount; treeIndex.commitsDone++) CommitTree(repo, &repo->commits[treeIndex.commitsDone], treeSha, TreeIndexAdd, NULL);
    if (treeIndex.commitsDone) memcpy(treeIndex.lastCommitSha, repo->commits[treeIndex.commitsDone - 1].sha, 20);
}
/* Something the repository already has, for a delta built on it ("thin" packs). */
bool FindRepoObject(Repo *repo, const uint8_t *sha, const uint8_t **data, size_t *length, int *type)
{
    int blobIndex = ShaMapGet(&repo->blobMap, sha);
    if (blobIndex >= 0) { *data = (const uint8_t *)repo->blobs[blobIndex].data; *length = (size_t)repo->blobs[blobIndex].length; *type = OBJ_BLOB; return true; }
    int commitIndex = CommitBySha(repo, sha);
    if (commitIndex >= 0) { *data = (const uint8_t *)repo->commits[commitIndex].raw; *length = (size_t)repo->commits[commitIndex].rawLen; *type = OBJ_COMMIT; return true; }
    TreeIndexUpdate(repo);
    int treeSlot = ShaMapGet(&treeIndex.map, sha);
    if (treeSlot >= 0) { *data = treeIndex.trees.data + treeIndex.offsets[treeSlot]; *length = (size_t)treeIndex.lengths[treeSlot]; *type = OBJ_TREE; return true; }
    return false;
}

/* ---------- taking in a push ---------- */

/* One file of a pushed commit, before it's stored. */
typedef struct { const char *path; uint8_t sha[20]; uint8_t mode; } PushedFile;
typedef struct { PushedFile *entries; int count, capacity; } PushedFiles;

static int ComparePushedFiles(const void *left, const void *right) { return strcmp(((const PushedFile *)left)->path, ((const PushedFile *)right)->path); }

/* Unfolds a tree (and the trees inside it) into a flat list of paths. */
static const char *ExpandTree(PackContents *pack, Repo *repo, const uint8_t *sha, const char *prefix, PushedFiles *out, int depth)
{
    if (depth > 60) return "folders nested too deeply";
    const uint8_t *data = NULL;
    size_t length = 0;
    int type = 0;
    int objIndex = ShaMapGet(&pack->map, sha);
    if (objIndex >= 0) { data = pack->objects[objIndex].data; length = pack->objects[objIndex].length; type = pack->objects[objIndex].type; }
    else if (!FindRepoObject(repo, sha, &data, &length, &type)) return "a folder (tree) is missing from the push";
    if (type != OBJ_TREE) return "something that should be a folder isn't";
    size_t pos = 0;
    char path[PATH_LEN + 8];
    while (pos < length) {
        const uint8_t *space = memchr(data + pos, ' ', length - pos);
        if (!space) return "a folder is damaged";
        int modeLength = (int)(space - (data + pos));
        char mode[8];
        if (modeLength <= 0 || modeLength > 6) return "a folder is damaged";
        memcpy(mode, data + pos, (size_t)modeLength); mode[modeLength] = 0;
        pos += (size_t)modeLength + 1;
        const uint8_t *nameEnd = memchr(data + pos, 0, length - pos);
        if (!nameEnd || (size_t)(nameEnd - data) + 21 > length) return "a folder is damaged";
        const char *name = (const char *)(data + pos);
        const uint8_t *entrySha = nameEnd + 1;
        pos = (size_t)(nameEnd - data) + 21;
        int pathLength = snprintf(path, sizeof path, "%s%s", prefix, name);
        if (pathLength >= PATH_LEN) return TextFormat("a file path is too long (Mini GitHub allows %d characters)", PATH_LEN - 1);
        if (!IsValidPath(path)) return TextFormat("\"%.80s\" isn't a file name Mini GitHub can store", path);
        if (strcmp(mode, "40000") == 0) {
            char subPrefix[PATH_LEN + 16];
            snprintf(subPrefix, sizeof subPrefix, "%s/", path);
            const char *error = ExpandTree(pack, repo, entrySha, subPrefix, out, depth + 1);
            if (error) return error;
            continue;
        }
        int modeKind = strcmp(mode, "100644") == 0 ? MODE_FILE : strcmp(mode, "100755") == 0 ? MODE_EXEC : strcmp(mode, "120000") == 0 ? MODE_LINK : -1;
        if (strcmp(mode, "160000") == 0) return "submodules aren't supported";
        if (modeKind < 0) return "a file has an unusual mode";
        if (out->count >= MAX_FILES) return TextFormat("a commit has more than %d files", MAX_FILES);
        out->entries = GrowArray(out->entries, &out->capacity, out->count + 1, sizeof(PushedFile));
        if (out->count >= out->capacity) return "out of memory";
        PushedFile *entry = &out->entries[out->count++];
        entry->path = InternPath(path);
        if (!entry->path) return "out of memory";
        memcpy(entry->sha, entrySha, 20);
        entry->mode = (uint8_t)modeKind;
    }
    return NULL;
}

/* A new commit from the push, checked and ready to store. */
typedef struct { int objectIndex; CommitInfo info; PushedFiles files; int state; } PushedCommit;

/* Updates repo's branch from oldSha to newSha using the objects in pack.
   Returns NULL if it worked, or the reason to give git. *addedCount gets how many commits came in. */
const char *ApplyPush(Repo *repo, int pusherId, const uint8_t oldSha[20], const uint8_t newSha[20], const char *refName, PackContents *pack, int *addedCount)
{
    *addedCount = 0;
    (void)pusherId;                        /* who pushed doesn't decide whose commits they are */
    if (strncmp(refName, "refs/tags/", 10) == 0) return "Mini GitHub doesn't keep tags";
    if (strncmp(refName, "refs/heads/", 11) != 0) return "only branches can be pushed";
    const char *branchName = refName + 11;
    const char *problem = BranchNameProblem(branchName);
    if (problem) return "Mini GitHub can't use that branch name (letters, numbers and - _ . / only)";
    int branchIndex = FindBranch(repo, branchName);
    Commit *branchTip = branchIndex >= 0 ? &repo->commits[repo->branches[branchIndex].tip] : NULL;  /* the branch's tip now (NULL = a new branch) */
    if (memcmp(oldSha, branchTip ? branchTip->sha : ZERO_SHA, 20) != 0) return "fetch first - someone else pushed or committed in the meantime";
    if (memcmp(newSha, ZERO_SHA, 20) == 0) {                 /* git push --delete */
        if (branchIndex < 0) return NULL;
        if (branchIndex == repo->defaultBranch) return "the default branch can't be deleted (choose another default in Mini GitHub first)";
        const char *deleteError = DeleteBranch(repo, branchIndex);
        if (!deleteError) *addedCount = -1;                                 /* "something changed" */
        return deleteError;
    }
    if (branchTip && memcmp(newSha, branchTip->sha, 20) == 0) return NULL;  /* nothing new */
    if (branchIndex < 0) {
        if (repo->branchCount >= MAX_BRANCHES) return "this repository has as many branches as it can hold";
        for (int branchIndex2 = 0; branchIndex2 < repo->branchCount; branchIndex2++) {           /* git can't have "a" and "a/b" both */
            size_t existingLength = strlen(repo->branches[branchIndex2].name), nameLength = strlen(branchName);
            if ((existingLength < nameLength && strncmp(branchName, repo->branches[branchIndex2].name, existingLength) == 0 && branchName[existingLength] == '/') ||
                (nameLength < existingLength && strncmp(repo->branches[branchIndex2].name, branchName, nameLength) == 0 && repo->branches[branchIndex2].name[nameLength] == '/'))
                return "that name clashes with an existing branch";
        }
    }
    {   /* pushing a commit Mini GitHub already has (e.g. a new branch at an existing commit) */
        int knownCommit = CommitBySha(repo, newSha);
        if (knownCommit >= 0) {
            if (branchTip && !IsAncestor(repo, (int)(branchTip - repo->commits), knownCommit))
                return "the push doesn't build on this branch's latest commit - pull first (git pull), then push again";
            if (branchIndex >= 0) repo->branches[branchIndex].tip = knownCommit;
            else { branchIndex = AddBranch(repo, branchName, knownCommit); if (repo->branchCount == 1) repo->defaultBranch = repo->currentBranch = 0; }
            repo->generation++;
            *addedCount = -1;
            return NULL;
        }
    }

    /* 1) find every new commit, oldest first (each one after the commits it's built on) */
    PushedCommit *pushed = NULL;
    int pushedCount = 0, pushedCapacity = 0;
    ShaMap seen = {0};
    int *stack = NULL, stackSize = 0, stackCapacity = 0;
    bool reachedTip = false;
    const char *error = NULL;
    int *order = NULL, orderCount = 0, orderCapacity = 0;

    #define PUSH_ON_STACK(item) do { stack = GrowArray(stack, &stackCapacity, stackSize + 1, sizeof(int)); if (stackSize >= stackCapacity) { error = "out of memory"; goto cleanup; } stack[stackSize++] = (item); } while (0)
    {
        int objIndex = ShaMapGet(&pack->map, newSha);
        if (objIndex < 0 || pack->objects[objIndex].type != OBJ_COMMIT) { error = "the pushed commit is missing from the pack"; goto cleanup; }
        pushed = GrowArray(pushed, &pushedCapacity, 1, sizeof(PushedCommit));
        if (!pushedCapacity) { error = "out of memory"; goto cleanup; }
        memset(&pushed[0], 0, sizeof pushed[0]);
        pushed[0].objectIndex = objIndex;
        if (!ParseCommit((const char *)pack->objects[objIndex].data, (int)pack->objects[objIndex].length, &pushed[0].info)) { error = "a commit is damaged"; goto cleanup; }
        pushedCount = 1;
        ShaMapPut(&seen, newSha, 0);
        PUSH_ON_STACK(0);
    }
    while (stackSize) {
        int i = stack[stackSize - 1];
        if (pushed[i].state == 0) {
            pushed[i].state = 1;
            for (int parentNo = 0; parentNo < pushed[i].info.parentCount; parentNo++) {
                uint8_t parentSha[20];                        /* a copy: pushed[] may move as it grows */
                memcpy(parentSha, pushed[i].info.parents[parentNo], 20);
                if (branchTip && memcmp(parentSha, branchTip->sha, 20) == 0) reachedTip = true;
                int knownIndex = CommitBySha(repo, parentSha);
                if (knownIndex >= 0) { if (branchTip && !reachedTip && IsAncestor(repo, (int)(branchTip - repo->commits), knownIndex)) reachedTip = true; continue; }
                int seenIndex = ShaMapGet(&seen, parentSha);
                if (seenIndex >= 0) {
                    /* already found, but not placed yet: it must be placed before this one, so
                       it goes on top of the pile again (the copy further down is skipped later) */
                    if (pushed[seenIndex].state == 0) PUSH_ON_STACK(seenIndex);
                    continue;
                }
                int objIndex = ShaMapGet(&pack->map, parentSha);
                if (objIndex < 0 || pack->objects[objIndex].type != OBJ_COMMIT) {
                    error = branchTip ? "the push doesn't build on Mini GitHub's latest commit - pull first (git pull), then push again"
                            : "a commit is missing from the pack";
                    goto cleanup;
                }
                if (pushedCount >= MAX_COMMITS) { error = "too many commits"; goto cleanup; }
                pushed = GrowArray(pushed, &pushedCapacity, pushedCount + 1, sizeof(PushedCommit));
                if (pushedCount >= pushedCapacity) { error = "out of memory"; goto cleanup; }
                memset(&pushed[pushedCount], 0, sizeof pushed[pushedCount]);
                pushed[pushedCount].objectIndex = objIndex;
                if (!ParseCommit((const char *)pack->objects[objIndex].data, (int)pack->objects[objIndex].length, &pushed[pushedCount].info)) { error = "a commit is damaged"; goto cleanup; }
                ShaMapPut(&seen, parentSha, pushedCount);
                PUSH_ON_STACK(pushedCount);
                pushedCount++;
            }
        } else {
            stackSize--;
            if (pushed[i].state == 1) {
                pushed[i].state = 2;
                order = GrowArray(order, &orderCapacity, orderCount + 1, sizeof(int));
                if (orderCount >= orderCapacity) { error = "out of memory"; goto cleanup; }
                order[orderCount++] = i;
            }
        }
    }
    #undef PUSH_ON_STACK
    if (branchTip && !reachedTip) {
        error = "the push doesn't build on Mini GitHub's latest commit - pull first (git pull), then push again";
        goto cleanup;
    }
    if (repo->commitCount + orderCount > MAX_COMMITS) { error = "the repository would have too many commits"; goto cleanup; }

    /* every commit must come after the ones it's built on - check before storing anything */
    {
        int *position = malloc(((size_t)pushedCount + 1) * sizeof(int));
        if (!position) { error = "out of memory"; goto cleanup; }
        for (int k = 0; k < orderCount; k++) position[order[k]] = k;
        for (int k = 0; k < orderCount && !error; k++)
            for (int parentNo = 0; parentNo < pushed[order[k]].info.parentCount; parentNo++) {
                int seenIndex = ShaMapGet(&seen, pushed[order[k]].info.parents[parentNo]);
                if (seenIndex >= 0 && position[seenIndex] >= k) { error = "Mini GitHub put the commits in the wrong order (a bug) - nothing was stored"; break; }
            }
        free(position);
        if (error) goto cleanup;
    }

    /* 2) unfold each commit's folders, and check every file is there */
    for (int k = 0; k < orderCount; k++) {
        PushedCommit *pending = &pushed[order[k]];
        error = ExpandTree(pack, repo, pending->info.tree, "", &pending->files, 0);
        if (error) goto cleanup;
        qsort(pending->files.entries, (size_t)pending->files.count, sizeof(PushedFile), ComparePushedFiles);
        const char **names = malloc((size_t)(pending->files.count + 1) * sizeof *names);
        uint8_t (*shas)[20] = malloc((size_t)(pending->files.count + 1) * 20);
        uint8_t *modes = malloc((size_t)pending->files.count + 1);
        if (!names || !shas || !modes) { free(names); free(shas); free(modes); error = "out of memory"; goto cleanup; }
        for (int j = 0; j < pending->files.count; j++) {
            PushedFile *entry = &pending->files.entries[j];
            if (j && strcmp(entry->path, pending->files.entries[j - 1].path) == 0) { error = "a commit has the same file twice"; break; }
            int objIndex = ShaMapGet(&pack->map, entry->sha);
            if ((objIndex < 0 || pack->objects[objIndex].type != OBJ_BLOB) && ShaMapGet(&repo->blobMap, entry->sha) < 0) { error = TextFormat("the contents of %.80s are missing from the push", entry->path); break; }
            if (objIndex >= 0 && pack->objects[objIndex].length > 0x7FFFFFF0u) { error = "a file is too big"; break; }
            names[j] = entry->path; memcpy(shas[j], entry->sha, 20); modes[j] = entry->mode;
        }
        /* Mini GitHub rebuilds the folders itself, so make sure it gets exactly git's fingerprint */
        uint8_t treeSha[20];
        if (!error && (!BuildTree(names, (const uint8_t (*)[20])shas, modes, 0, pending->files.count, 0, treeSha, NULL, NULL) || memcmp(treeSha, pending->info.tree, 20) != 0))
            error = "a commit has a folder Mini GitHub can't store exactly (empty or oddly sorted folders?)";
        free(names); free(shas); free(modes);
        if (error) goto cleanup;
    }

    /* 3) all good: store the commits */
    {
        int upstream = UpstreamIndex(repo);
        for (int k = 0; k < orderCount; k++) {
            PushedCommit *pushedCommit = &pushed[order[k]];
            PackEntry *object = &pack->objects[pushedCommit->objectIndex];
            Commit commit;
            memset(&commit, 0, sizeof commit);
            int fileCount = pushedCommit->files.count;
            commit.fileCount = fileCount;
            commit.names = malloc((size_t)(fileCount ? fileCount : 1) * sizeof *commit.names);
            commit.blobs = malloc((size_t)(fileCount ? fileCount : 1) * sizeof *commit.blobs);
            commit.modes = malloc((size_t)(fileCount ? fileCount : 1));
            commit.raw = malloc(object->length + 1);
            if (!commit.names || !commit.blobs || !commit.modes || !commit.raw) { FreeCommit(&commit); error = "out of memory"; break; }
            memcpy(commit.raw, object->data, object->length + 1);
            commit.rawLen = (int)object->length;
            for (int j = 0; j < fileCount && !error; j++) {
                PushedFile *entry = &pushedCommit->files.entries[j];
                int blobIndex = ShaMapGet(&repo->blobMap, entry->sha);
                if (blobIndex < 0) {
                    int packIndex = ShaMapGet(&pack->map, entry->sha);
                    blobIndex = StoreBlobWithSha(repo, (const char *)pack->objects[packIndex].data, (int)pack->objects[packIndex].length, entry->sha);
                }
                if (blobIndex < 0) error = "the repository is out of storage space";
                commit.names[j] = entry->path; commit.blobs[j] = blobIndex; commit.modes[j] = entry->mode;
            }
            if (error) { FreeCommit(&commit); break; }
            for (int parentNo = 0; parentNo < pushedCommit->info.parentCount && parentNo < 2; parentNo++) {
                commit.parents[parentNo] = CommitBySha(repo, pushedCommit->info.parents[parentNo]);
                if (commit.parents[parentNo] < 0) error = "Mini GitHub put the commits in the wrong order (a bug) - nothing was stored";
            }
            if (error) { FreeCommit(&commit); break; }
            commit.parentCount = pushedCommit->info.parentCount < 2 ? pushedCommit->info.parentCount : 2;
            commit.time = pushedCommit->info.authorTime;
            snprintf(commit.message, TEXT_LEN, "%s", pushedCommit->info.summary);
            int inUpstream = upstream >= 0 ? CommitBySha(&repos[upstream], object->sha) : -1;
            if (inUpstream >= 0) commit.copied = true;       /* the original's work, not new work */
            commit.author = AuthorOfCommit(&commit);         /* a Mini GitHub user, or someone from outside */
            if (!AppendCommit(repo, &commit)) { FreeCommit(&commit); error = "the repository has too many commits"; break; }
            (*addedCount)++;
        }
        if (!error && *addedCount) {                           /* the branch moves to the pushed commit */
            int tip = CommitBySha(repo, newSha);
            if (branchIndex >= 0) repo->branches[branchIndex].tip = tip;
            else {
                branchIndex = AddBranch(repo, branchName, tip);
                if (repo->branchCount == 1) { repo->defaultBranch = repo->currentBranch = 0; snprintf(repo->initialBranch, NAME_LEN, "%s", branchName); }
            }
            repo->generation++;
        }
    }

cleanup:
    for (int i = 0; i < pushedCount; i++) free(pushed[i].files.entries);
    free(pushed); free(stack); free(order);
    ShaMapFree(&seen);
    return error;
}
