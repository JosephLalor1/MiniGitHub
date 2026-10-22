/*
 * save_file.c - saving and loading minigithub.dat
 *
 * Writes everything to the save file and reads it back - including save
 * files from every older version of Mini GitHub, and histories an older
 * version stored out of order.
 */

#include "save_file.h"
#include "drafts.h"
#include "git_commit.h"
#include "git_paths.h"
#include "model.h"
#include "repo.h"
#include "sha1.h"
#include "sha_map.h"
#include "themes.h"
#include "ui_core.h"
#include "users.h"
#include "util.h"
#include "work_file.h"

/* ------------------------------------------------------------------ */
/* Saving and loading                                                 */
/* Format: numbers on their own line; text as  <length>:<bytes>\n      */
/* ------------------------------------------------------------------ */
static void WriteBytesField(FILE *file, const char *data, int length) { fprintf(file, "%d:", length); fwrite(data, 1, (size_t)length, file); fputc('\n', file); }
static void WriteTextField(FILE *file, const char *text) { WriteBytesField(file, text, (int)strlen(text)); }
static void WriteNumberField(FILE *file, long long value)   { fprintf(file, "%lld\n", value); }

static int  loadError;
bool loadFailed;                /* the save file couldn't be read: nothing is saved until you decide */
static int  loadFailCheck;      /* which check failed (for the message) */
char loadReason[TEXT_LEN];      /* what went wrong, in words            */
char loadContext[TEXT_LEN];     /* e.g. "the repository jo/hello-world"  */
#define LOAD_FAIL (loadError = 1, loadFailCheck = loadFailCheck ? loadFailCheck : __LINE__)
/* Reads one piece of text (or binary data); *outLength gets its length. */
static char *ReadBytesField(FILE *file, int *outLength)
{
    int length;
    /* anything up to 1 GB: as big as anything Mini GitHub can store */
    if (fscanf(file, " %d:", &length) != 1 || length < 0 || length > 1073741824) {
        if (!loadError) snprintf(loadReason, sizeof loadReason, "a piece of text or a file in it couldn't be read");
        LOAD_FAIL; return NULL;
    }
    char *bytes = malloc((size_t)length + 1);
    if (!bytes) { if (!loadError) snprintf(loadReason, sizeof loadReason, "not enough memory to load a %d MB file", length >> 20); LOAD_FAIL; return NULL; }
    if (fread(bytes, 1, (size_t)length, file) != (size_t)length) { free(bytes); if (!loadError) snprintf(loadReason, sizeof loadReason, "the file ends too early (cut short?)"); LOAD_FAIL; return NULL; }
    bytes[length] = 0;
    if (outLength) *outLength = length;
    fgetc(file);
    return bytes;
}
static char *ReadTextField(FILE *file) { return ReadBytesField(file, NULL); }
static void ReadTextInto(FILE *file, char *dest, int capacity)
{
    char *text = ReadTextField(file);
    snprintf(dest, (size_t)capacity, "%s", text ? text : "");
    free(text);
}
static long long ReadNumberField(FILE *file)
{
    long long value = 0;
    if (fscanf(file, " %lld", &value) != 1) { if (!loadError) snprintf(loadReason, sizeof loadReason, "a number in it couldn't be read"); LOAD_FAIL; }
    return value;
}

static uint32_t PathPointerHash(const void *pointer) { uintptr_t bits = (uintptr_t)pointer; bits ^= bits >> 17; bits *= 0x9E3779B1u; return (uint32_t)(bits ^ (bits >> 15)); }
/* The number given to this path, or -1. */
int PathIndexGet(PathIndexMap *map, const char *path)
{
    if (!map->capacity) return -1;
    for (uint32_t slot = PathPointerHash(path) & (uint32_t)(map->capacity - 1);; slot = (slot + 1) & (uint32_t)(map->capacity - 1)) {
        if (!map->paths[slot]) return -1;
        if (map->paths[slot] == path) return map->numbers[slot];
    }
}
/* Gives a path a number. */
void PathIndexPut(PathIndexMap *map, const char *path, int number)
{
    if ((map->count + 1) * 2 > map->capacity) {
        PathIndexMap bigger = {0};
        bigger.capacity = map->capacity ? map->capacity * 2 : 256;
        bigger.paths = calloc((size_t)bigger.capacity, sizeof *bigger.paths);
        bigger.numbers = calloc((size_t)bigger.capacity, sizeof *bigger.numbers);
        if (!bigger.paths || !bigger.numbers) { free(bigger.paths); free(bigger.numbers); return; }
        for (int slot = 0; slot < map->capacity; slot++) if (map->paths[slot]) PathIndexPut(&bigger, map->paths[slot], map->numbers[slot]);
        free(map->paths); free(map->numbers);
        *map = bigger;
    }
    uint32_t slot = PathPointerHash(path) & (uint32_t)(map->capacity - 1);
    while (map->paths[slot] && map->paths[slot] != path) slot = (slot + 1) & (uint32_t)(map->capacity - 1);
    if (!map->paths[slot]) map->count++;
    map->paths[slot] = path; map->numbers[slot] = number;
}
static void PathIndexFree(PathIndexMap *map) { free(map->paths); free(map->numbers); memset(map, 0, sizeof *map); }

/* Earlier versions could store a pushed commit before a commit it was built on
   (with merges that criss-cross), recording the link wrongly. Every commit's
   real parents are in its git data, so they're read again from there, and the
   list is put back in order: each commit after the ones it's built on.
   Returns false only if the history can't be put in order at all. */
static bool RepairHistory(Repo *repo)
{
    int count = repo->commitCount;
    if (!count) return true;
    for (int k = 0; k < count; k++) {
        Commit *commit = &repo->commits[k];
        CommitInfo info;
        if (!ParseCommit(commit->raw, commit->rawLen, &info)) return false;
        commit->parentCount = 0;
        for (int parentNo = 0; parentNo < info.parentCount && commit->parentCount < 2; parentNo++) {
            int parent = CommitBySha(repo, info.parents[parentNo]);
            if (parent >= 0 && parent != k) commit->parents[commit->parentCount++] = parent;  /* a missing one is left out */
        }
        commit->statsDone = false;
    }
    /* put in order: repeatedly take a commit whose parents are all placed */
    int *parentsLeft = calloc((size_t)count, sizeof(int)), *childStart = calloc((size_t)count + 1, sizeof(int)), *children = malloc((size_t)count * 2 * sizeof(int) + 1);
    int *order = malloc((size_t)count * sizeof(int)), *newIndex = malloc((size_t)count * sizeof(int)), *queue = malloc((size_t)count * sizeof(int));
    Commit *moved = malloc((size_t)count * sizeof(Commit));
    bool ok = parentsLeft && childStart && children && order && newIndex && queue && moved;
    if (ok) {
        for (int k = 0; k < count; k++) for (int parentNo = 0; parentNo < repo->commits[k].parentCount; parentNo++) childStart[repo->commits[k].parents[parentNo] + 1]++;
        for (int k = 0; k < count; k++) childStart[k + 1] += childStart[k];
        int *childFill = calloc((size_t)count, sizeof(int));
        if (!childFill) ok = false;
        for (int k = 0; ok && k < count; k++)
            for (int parentNo = 0; parentNo < repo->commits[k].parentCount; parentNo++) { int parent = repo->commits[k].parents[parentNo]; children[childStart[parent] + childFill[parent]++] = k; }
        free(childFill);
        int queueHead = 0, queueTail = 0, orderedCount = 0;
        for (int k = 0; ok && k < count; k++) { parentsLeft[k] = repo->commits[k].parentCount; if (!parentsLeft[k]) queue[queueTail++] = k; }
        while (ok && queueHead < queueTail) {
            int k = queue[queueHead++];
            order[orderedCount++] = k;
            for (int j = childStart[k]; j < childStart[k + 1]; j++) if (--parentsLeft[children[j]] == 0) queue[queueTail++] = children[j];
        }
        if (orderedCount != count) ok = false;               /* a loop: can't happen with real commits */
        if (ok) {
            for (int i = 0; i < count; i++) newIndex[order[i]] = i;
            for (int i = 0; i < count; i++) moved[i] = repo->commits[order[i]];
            for (int i = 0; i < count; i++) {
                for (int parentNo = 0; parentNo < moved[i].parentCount; parentNo++) moved[i].parents[parentNo] = newIndex[moved[i].parents[parentNo]];
                repo->commits[i] = moved[i];
            }
            for (int branch = 0; branch < repo->branchCount; branch++) repo->branches[branch].tip = newIndex[repo->branches[branch].tip];
            ShaMapFree(&repo->commitMap);
            for (int i = 0; i < count; i++) ShaMapPut(&repo->commitMap, repo->commits[i].sha, i);
            repo->generation++;
        }
    }
    free(parentsLeft); free(childStart); free(children); free(order); free(newIndex); free(queue); free(moved);
    return ok;
}

/* Writes everything to minigithub.dat (via a temporary file, so a crash never leaves half a file).
   Never writes over a file that failed to load. */
void Save(void)
{
    if (loadFailed) return;                /* never write over a save file that couldn't be read */
    FILE *file = fopen(SAVE_FILE ".tmp", "wb");
    if (!file) { Toast("Could not save - is the folder read-only?"); return; }
    fprintf(file, "MINIGITHUB10\n");
    WriteNumberField(file, nextUserId);
    WriteTextField(file, lastUserName);
    WriteNumberField(file, userCount);
    for (int i = 0; i < userCount; i++) {
        User *user = &users[i];
        WriteNumberField(file, user->id); WriteTextField(file, user->name); WriteTextField(file, user->salt); WriteTextField(file, user->hash); WriteNumberField(file, user->created);
        WriteTextField(file, user->bio); WriteNumberField(file, user->theme); WriteNumberField(file, user->avatarColor); WriteTextField(file, user->avatar ? user->avatar : "");
        WriteTextField(file, user->recoverySalt); WriteTextField(file, user->recoveryHash);
        WriteTextField(file, user->email);
        WriteNumberField(file, user->claimCount);
        for (int k = 0; k < user->claimCount; k++) WriteTextField(file, user->claims[k]);
    }
    WriteNumberField(file, repoCount);
    for (int i = 0; i < repoCount; i++) {
        Repo *repo = &repos[i];
        WriteNumberField(file, repo->owner); WriteTextField(file, repo->name); WriteTextField(file, repo->description); WriteNumberField(file, repo->created); WriteTextField(file, repo->initialBranch); WriteNumberField(file, repo->isPrivate);
        WriteNumberField(file, repo->starCount);
        for (int k = 0; k < repo->starCount; k++) WriteNumberField(file, repo->starredBy[k]);
        WriteNumberField(file, repo->collaboratorCount);
        for (int k = 0; k < repo->collaboratorCount; k++) WriteNumberField(file, repo->collaborators[k]);
        WriteNumberField(file, repo->inviteCount);
        for (int k = 0; k < repo->inviteCount; k++) { WriteNumberField(file, repo->invites[k]); WriteNumberField(file, repo->inviteTime[k]); }
        WriteNumberField(file, repo->blobCount);
        for (int k = 0; k < repo->blobCount; k++) WriteBytesField(file, repo->blobs[k].data, repo->blobs[k].length);

        /* every path used, once, then each commit refers to them by number */
        PathIndexMap pathNumbers = {0};
        int pathCount = 0;
        for (int k = 0; k < repo->commitCount; k++)
            for (int j = 0; j < repo->commits[k].fileCount; j++)
                if (PathIndexGet(&pathNumbers, repo->commits[k].names[j]) < 0) PathIndexPut(&pathNumbers, repo->commits[k].names[j], pathCount++);
        WriteNumberField(file, pathCount);
        const char **pathList = calloc((size_t)(pathCount ? pathCount : 1), sizeof *pathList);
        for (int slot = 0; pathList && slot < pathNumbers.capacity; slot++) if (pathNumbers.paths[slot]) pathList[pathNumbers.numbers[slot]] = pathNumbers.paths[slot];
        for (int k = 0; k < pathCount; k++) WriteTextField(file, pathList ? pathList[k] : "");
        free(pathList);

        WriteNumberField(file, repo->commitCount);
        for (int k = 0; k < repo->commitCount; k++) {
            Commit *commit = &repo->commits[k];
            WriteBytesField(file, commit->raw, commit->rawLen);
            /* the line counts are kept too, so they needn't be worked out again (-1 = not yet) */
            fprintf(file, "%lld %d %d %d %d %d %d %d %d %d\n", commit->time, commit->author, (int)commit->copied, commit->parentCount,
                    commit->parents[0], commit->parents[1], commit->statsDone ? commit->statFiles : -1, commit->statAdds, commit->statDels, commit->fileCount);
            for (int j = 0; j < commit->fileCount; j++) fprintf(file, "%d %d %d\n", PathIndexGet(&pathNumbers, commit->names[j]), commit->blobs[j], commit->modes[j]);
        }
        PathIndexFree(&pathNumbers);
        WriteNumberField(file, repo->branchCount); WriteNumberField(file, repo->defaultBranch);
        for (int branch = 0; branch < repo->branchCount; branch++) { WriteTextField(file, repo->branches[branch].name); WriteNumberField(file, repo->branches[branch].tip); }

        WriteNumberField(file, repo->issueCount);
        for (int k = 0; k < repo->issueCount; k++) {
            Issue *issue = &repo->issues[k];
            WriteTextField(file, issue->title); WriteNumberField(file, issue->open); WriteNumberField(file, issue->time); WriteNumberField(file, issue->author);
        }
        int nonEmptyDrafts = 0;                           /* skip empty drafts */
        for (int k = 0; k < repo->draftCount; k++) if (repo->drafts[k].fileCount) nonEmptyDrafts++;
        WriteNumberField(file, nonEmptyDrafts);
        for (int k = 0; k < repo->draftCount; k++) {
            Draft *draft = &repo->drafts[k];
            if (!draft->fileCount) continue;
            WriteNumberField(file, draft->userId); WriteNumberField(file, draft->fileCount);
            char mergeShaHex[41];
            ShaToHex(draft->mergeSha, mergeShaHex);
            WriteTextField(file, draft->branch); WriteNumberField(file, draft->merging); WriteTextField(file, mergeShaHex); WriteTextField(file, draft->mergeName);
            for (int j = 0; j < draft->fileCount; j++) {
                WorkFile *draftFile = &draft->files[j];
                WriteTextField(file, draftFile->name); WriteBytesField(file, draftFile->text, draftFile->length); WriteNumberField(file, draftFile->deleted); WriteNumberField(file, draftFile->baseBlob);
            }
        }
        WriteNumberField(file, repo->isFork); WriteNumberField(file, repo->forkOwner); WriteTextField(file, repo->forkName); WriteTextField(file, repo->forkBase);
    }
    bool ok = !ferror(file);
    ok = fclose(file) == 0 && ok;
    if (!ok) { remove(SAVE_FILE ".tmp"); Toast("Could not save - is the disk full?"); return; }
    remove(SAVE_FILE);
    rename(SAVE_FILE ".tmp", SAVE_FILE);
}

/* "MINIGITHUB3" was used by two different versions of this program: one added
   profiles, the other added recovery codes. Both put their new fields right after
   each account's creation time: profiles start with the bio (text, then a number),
   recovery codes with two pieces of text. So peek at the second item to tell. */
static bool V3HasProfiles(FILE *file)
{
    long position = ftell(file);
    int savedError = loadError;
    char *firstText = ReadTextField(file);
    free(firstText);
    int number = 0, nextChar = 0;
    bool isText = fscanf(file, " %d", &number) == 1 && (nextChar = fgetc(file)) == ':';
    fseek(file, position, SEEK_SET);
    loadError = savedError;
    return !isText;
}

/* A blob read from the save file goes exactly where it was (commits refer to it by number). */
static bool LoadBlob(Repo *repo, char *data, int length)
{
    repo->blobs = GrowArray(repo->blobs, &repo->blobCapacity, repo->blobCount + 1, sizeof(Blob));
    if (repo->blobCount >= repo->blobCapacity) { free(data); return false; }
    Blob *blob = &repo->blobs[repo->blobCount];
    blob->data = data; blob->length = length; blob->binary = LooksBinary(data, length); blob->lang = NULL;
    GitObjectSha("blob", data, (size_t)length, blob->sha);
    if (ShaMapGet(&repo->blobMap, blob->sha) < 0) ShaMapPut(&repo->blobMap, blob->sha, repo->blobCount);
    repo->blobCount++;
    return true;
}

typedef struct { const char *name; int blob; } OldEntry;
static int CompareOldEntries(const void *left, const void *right) { return strcmp(((const OldEntry *)left)->name, ((const OldEntry *)right)->name); }

/* Commits from before version 7 weren't git commits yet. Each is turned into
   one now - the same files, author, time and message, on top of the commit
   before it - so it gets a real git fingerprint. */
static bool UpgradeCommits(Repo *repo)
{
    for (int k = 0; k < repo->commitCount; k++) {
        Commit *commit = &repo->commits[k];
        uint8_t treeSha[20];
        const uint8_t *parentShas[1];
        commit->parentCount = k > 0;
        commit->parents[0] = k - 1;
        if (k > 0) parentShas[0] = repo->commits[k - 1].sha;
        if (!CommitTree(repo, commit, treeSha, NULL, NULL)) return false;
        commit->raw = MakeRawCommit(treeSha, parentShas, k > 0, UserName(commit->author), UserEmail(commit->author), commit->time, commit->message, &commit->rawLen);
        if (!commit->raw) return false;
        GitObjectSha("commit", commit->raw, (size_t)commit->rawLen, commit->sha);
        char hex[41];
        ShaToHex(commit->sha, hex);
        snprintf(commit->id, sizeof commit->id, "%.7s", hex);
        ShaMapPut(&repo->commitMap, commit->sha, k);
    }
    repo->generation++;
    return true;
}

/* Reads every version: 7 (git commits, folders), 6 (forks), 5 (collaborators + drafts),
   4 (profiles + recovery codes), 3 (either one), 2 (accounts), 1 (before accounts). */
bool Load(void)
{
    /* the program stopped in the middle of saving: the new copy was written, the old one already removed */
    if (!FileExists(SAVE_FILE) && FileExists(SAVE_FILE ".tmp")) rename(SAVE_FILE ".tmp", SAVE_FILE);
    FILE *file = fopen(SAVE_FILE, "rb");
    if (!file) return false;
    loadError = 0; loadFailCheck = 0; loadReason[0] = 0; loadContext[0] = 0;
    char magic[32] = {0};
    int version = 0;
    if (fscanf(file, "%31s", magic) == 1 && strncmp(magic, "MINIGITHUB", 10) == 0) {
        int number = atoi(magic + 10);
        if (number >= 1 && number <= 10 && magic[10] != '0') version = number;  /* MINIGITHUB1 ... MINIGITHUB10 */
    }
    if (!version) {
        if (strncmp(magic, "MINIGITHUB", 10) == 0) snprintf(loadReason, sizeof loadReason, "it was made by a newer version of Mini GitHub");
        else snprintf(loadReason, sizeof loadReason, "it isn't a Mini GitHub save file");
        LOAD_FAIL;
    }

    if (version >= 2) {
        nextUserId = (int)ReadNumberField(file);
        ReadTextInto(file, lastUserName, NAME_LEN);
        int savedUserCount = (int)ReadNumberField(file);
        if (savedUserCount < 0 || savedUserCount > MAX_USERS) LOAD_FAIL;
        for (int i = 0; i < savedUserCount && !loadError; i++) {
            User *user = &users[userCount++];
            memset(user, 0, sizeof *user);
            user->id = (int)ReadNumberField(file);
            ReadTextInto(file, user->name, NAME_LEN);
            ReadTextInto(file, user->salt, (int)sizeof user->salt);
            ReadTextInto(file, user->hash, (int)sizeof user->hash);
            user->created = ReadNumberField(file);
            bool hasProfiles = version >= 4 || (version == 3 && V3HasProfiles(file));
            bool hasRecoveryCode = version >= 4 || (version == 3 && !hasProfiles);
            if (hasProfiles) {
                ReadTextInto(file, user->bio, TEXT_LEN);
                user->theme       = (int)ReadNumberField(file);
                user->avatarColor = (int)ReadNumberField(file);
                user->avatar      = ReadTextField(file);
                if (user->avatar && !user->avatar[0]) { free(user->avatar); user->avatar = NULL; }
                if (user->theme < 0 || user->theme >= THEME_COUNT) user->theme = 0;
                if (user->avatarColor < 0 || user->avatarColor > 8) user->avatarColor = 0;
            }
            if (hasRecoveryCode) {
                ReadTextInto(file, user->recoverySalt, (int)sizeof user->recoverySalt);
                ReadTextInto(file, user->recoveryHash, (int)sizeof user->recoveryHash);
                bool noCode = !user->recoverySalt[0] && !user->recoveryHash[0];
                if (!noCode && (strlen(user->recoverySalt) != 32 || strlen(user->recoveryHash) != 64)) LOAD_FAIL;
            }
            if (version >= 7) ReadTextInto(file, user->email, TEXT_LEN);
            if (version >= 9) {
                int savedClaimCount = (int)ReadNumberField(file);
                if (savedClaimCount < 0 || savedClaimCount > 8) LOAD_FAIL;
                for (int k = 0; k < savedClaimCount && !loadError; k++) ReadTextInto(file, user->claims[user->claimCount++], TEXT_LEN);
            }
            if (user->id <= 0 || strlen(user->salt) != 32 || strlen(user->hash) != 64) LOAD_FAIL;
            if (user->id >= nextUserId) nextUserId = user->id + 1;
        }
    }

    int savedRepoCount = loadError ? 0 : (int)ReadNumberField(file);
    if (savedRepoCount < 0 || savedRepoCount > MAX_REPOS) LOAD_FAIL;

    static OldEntry oldEntries[64];
    int repairedCount = 0;                                   /* histories put back in order */
    for (int i = 0; i < savedRepoCount && !loadError; i++) {
        bool needRepair = false;
        Repo *repo = &repos[repoCount++];
        memset(repo, 0, sizeof *repo);
        snprintf(repo->initialBranch, NAME_LEN, "main");
        repo->owner = version >= 2 ? (int)ReadNumberField(file) : -1;
        ReadTextInto(file, repo->name, NAME_LEN);
        snprintf(loadContext, sizeof loadContext, "the repository %s", repo->name);
        ReadTextInto(file, repo->description, TEXT_LEN);
        if (version == 1) ReadNumberField(file);                    /* old single-user "starred" flag: dropped */
        repo->created = ReadNumberField(file);
        if (version >= 7) { ReadTextInto(file, repo->initialBranch, NAME_LEN); if (!repo->initialBranch[0]) snprintf(repo->initialBranch, NAME_LEN, "main"); }
        if (version >= 10) repo->isPrivate = ReadNumberField(file) != 0;
        if (version >= 2) {
            int savedStarCount = (int)ReadNumberField(file);
            if (savedStarCount < 0 || savedStarCount > MAX_USERS) { LOAD_FAIL; break; }
            for (int k = 0; k < savedStarCount; k++) repo->starredBy[repo->starCount++] = (int)ReadNumberField(file);
        }

        /* before version 5 there was one working copy per repository (the owner's);
           it's read here and turned into the owner's draft once the commits are in */
        static WorkFile legacyFiles[64];
        int legacyFileCount = 0;
        if (version >= 5) {
            int savedCollaboratorCount = (int)ReadNumberField(file);
            if (savedCollaboratorCount < 0 || savedCollaboratorCount > MAX_USERS) { LOAD_FAIL; break; }
            for (int k = 0; k < savedCollaboratorCount; k++) repo->collaborators[repo->collaboratorCount++] = (int)ReadNumberField(file);
            int savedInviteCount = (int)ReadNumberField(file);
            if (savedInviteCount < 0 || savedInviteCount > MAX_USERS) { LOAD_FAIL; break; }
            for (int k = 0; k < savedInviteCount; k++) { repo->invites[repo->inviteCount] = (int)ReadNumberField(file); repo->inviteTime[repo->inviteCount++] = ReadNumberField(file); }
        } else {
            int savedFileCount = (int)ReadNumberField(file);
            if (savedFileCount < 0 || savedFileCount > 64) { LOAD_FAIL; break; }
            for (int k = 0; k < savedFileCount && !loadError; k++) {
                WorkFile *legacyFile = &legacyFiles[legacyFileCount];
                memset(legacyFile, 0, sizeof *legacyFile);
                ReadTextInto(file, legacyFile->name, PATH_LEN);
                int textLength = 0;
                char *text = ReadBytesField(file, &textLength);
                if (text) { WorkFileSetBytes(legacyFile, text, textLength); free(text); legacyFileCount++; }
            }
        }

        int savedBlobCount = (int)ReadNumberField(file);
        if (savedBlobCount < 0 || savedBlobCount > MAX_BLOBS) { LOAD_FAIL; break; }
        for (int k = 0; k < savedBlobCount && !loadError; k++) {
            int blobLength = 0;
            char *blobData = ReadBytesField(file, &blobLength);
            if (!blobData || !LoadBlob(repo, blobData, blobLength)) { LOAD_FAIL; break; }
        }
        if (loadError) break;

        int savedCommitCount = (int)ReadNumberField(file);
        if (version >= 7) {
            int pathCount = savedCommitCount;              /* first the table of paths */
            if (pathCount < 0 || pathCount > 10000000) { LOAD_FAIL; break; }
            const char **paths = malloc((size_t)(pathCount ? pathCount : 1) * sizeof *paths);
            if (!paths) { LOAD_FAIL; break; }
            for (int k = 0; k < pathCount && !loadError; k++) {
                char *pathText = ReadTextField(file);
                paths[k] = pathText ? InternPath(pathText) : NULL;
                if (!paths[k]) LOAD_FAIL;
                free(pathText);
            }
            savedCommitCount = loadError ? 0 : (int)ReadNumberField(file);
            if (savedCommitCount < 0 || savedCommitCount > MAX_COMMITS) LOAD_FAIL;
            for (int k = 0; k < savedCommitCount && !loadError; k++) {
                Commit commit;
                memset(&commit, 0, sizeof commit);
                commit.raw = ReadBytesField(file, &commit.rawLen);
                commit.time = ReadNumberField(file); commit.author = (int)ReadNumberField(file); commit.copied = ReadNumberField(file) != 0;
                commit.parentCount = (int)ReadNumberField(file); commit.parents[0] = (int)ReadNumberField(file); commit.parents[1] = (int)ReadNumberField(file);
                commit.statFiles = (int)ReadNumberField(file); commit.statAdds = (int)ReadNumberField(file); commit.statDels = (int)ReadNumberField(file);
                bool hasStats = commit.statFiles >= 0 && commit.statAdds >= 0 && commit.statDels >= 0;
                int fileCount = (int)ReadNumberField(file);
                if (!commit.raw || fileCount < 0 || fileCount > MAX_FILES || commit.parentCount < 0 || commit.parentCount > 2) { free(commit.raw); LOAD_FAIL; break; }
                for (int parentNo = 0; parentNo < commit.parentCount; parentNo++)
                    if (commit.parents[parentNo] < 0 || commit.parents[parentNo] >= k) { needRepair = true; commit.parents[parentNo] = 0; }  /* fixed below */
                commit.fileCount = fileCount;
                commit.names = malloc((size_t)(fileCount ? fileCount : 1) * sizeof *commit.names);
                commit.blobs = malloc((size_t)(fileCount ? fileCount : 1) * sizeof *commit.blobs);
                commit.modes = malloc((size_t)(fileCount ? fileCount : 1));
                if (!commit.names || !commit.blobs || !commit.modes) { FreeCommit(&commit); LOAD_FAIL; break; }
                for (int j = 0; j < fileCount && !loadError; j++) {
                    int pathIndex = (int)ReadNumberField(file), blobIndex = (int)ReadNumberField(file), mode = (int)ReadNumberField(file);
                    if (pathIndex < 0 || pathIndex >= pathCount || blobIndex < 0 || blobIndex >= repo->blobCount || mode < 0 || mode > 2) { LOAD_FAIL; break; }
                    commit.names[j] = paths[pathIndex]; commit.blobs[j] = blobIndex; commit.modes[j] = (uint8_t)mode;
                }
                CommitInfo info;
                if (!loadError && !ParseCommit(commit.raw, commit.rawLen, &info)) LOAD_FAIL;
                if (loadError) { FreeCommit(&commit); break; }
                snprintf(commit.message, TEXT_LEN, "%s", info.summary);
                if (!AppendCommit(repo, &commit)) { FreeCommit(&commit); LOAD_FAIL; break; }
                repo->commits[repo->commitCount - 1].statsDone = hasStats;
            }
            free(paths);
            if (loadError) break;
        } else {
            if (savedCommitCount < 0 || savedCommitCount > MAX_COMMITS) { LOAD_FAIL; break; }
            repo->commits = GrowArray(repo->commits, &repo->commitCapacity, savedCommitCount + 1, sizeof(Commit));
            if (repo->commitCapacity < savedCommitCount) { LOAD_FAIL; break; }
            for (int k = 0; k < savedCommitCount && !loadError; k++) {
                Commit *commit = &repo->commits[k];
                memset(commit, 0, sizeof *commit);
                ReadTextInto(file, commit->id, (int)sizeof commit->id);  /* the old short id: only used to match up forks below */
                ReadTextInto(file, commit->message, TEXT_LEN);
                commit->time   = ReadNumberField(file);
                commit->author = version >= 2 ? (int)ReadNumberField(file) : -1;
                int fileCount = (int)ReadNumberField(file);
                if (fileCount < 0 || fileCount > 64) { LOAD_FAIL; break; }
                commit->fileCount = fileCount;
                for (int j = 0; j < fileCount; j++) {
                    char name[PATH_LEN];
                    ReadTextInto(file, name, PATH_LEN);
                    int blobIndex = (int)ReadNumberField(file);
                    if (blobIndex < 0 || blobIndex >= repo->blobCount || !name[0]) { LOAD_FAIL; break; }
                    oldEntries[j].name = InternPath(name);
                    oldEntries[j].blob = blobIndex;
                    if (!oldEntries[j].name) LOAD_FAIL;
                }
                if (version >= 6) commit->copied = ReadNumberField(file) != 0;
                if (loadError) break;
                qsort(oldEntries, (size_t)fileCount, sizeof oldEntries[0], CompareOldEntries);
                commit->names = malloc((size_t)(fileCount ? fileCount : 1) * sizeof *commit->names);
                commit->blobs = malloc((size_t)(fileCount ? fileCount : 1) * sizeof *commit->blobs);
                commit->modes = calloc((size_t)(fileCount ? fileCount : 1), 1);
                repo->commitCount++;
                if (!commit->names || !commit->blobs || !commit->modes) { LOAD_FAIL; break; }
                for (int j = 0; j < fileCount; j++) { commit->names[j] = oldEntries[j].name; commit->blobs[j] = oldEntries[j].blob; }
            }
            if (loadError) break;
        }

        /* branches (before version 8 there was just one, at the newest commit) */
        if (version >= 8) {
            int savedBranchCount = (int)ReadNumberField(file), defaultBranch = (int)ReadNumberField(file);
            if (savedBranchCount < 0 || savedBranchCount > MAX_BRANCHES || (savedBranchCount && (defaultBranch < 0 || defaultBranch >= savedBranchCount))) { LOAD_FAIL; break; }
            for (int branch = 0; branch < savedBranchCount && !loadError; branch++) {
                char branchName[NAME_LEN];
                ReadTextInto(file, branchName, NAME_LEN);
                int tip = (int)ReadNumberField(file);
                if (tip < 0 || tip >= repo->commitCount || !branchName[0]) LOAD_FAIL;
                else AddBranch(repo, branchName, tip);
            }
            repo->defaultBranch = repo->currentBranch = savedBranchCount ? defaultBranch : 0;
            if (loadError) break;
        } else if (repo->commitCount) { AddBranch(repo, repo->initialBranch, repo->commitCount - 1); repo->defaultBranch = repo->currentBranch = 0; }
        if (needRepair) {                                    /* a history stored out of order: put it right */
            if (!RepairHistory(repo)) { snprintf(loadReason, sizeof loadReason, "its history couldn't be put back in order"); LOAD_FAIL; break; }
            repairedCount++;
            needRepair = false;
        }

        int savedIssueCount = (int)ReadNumberField(file);
        if (savedIssueCount < 0 || savedIssueCount > MAX_ISSUES) { LOAD_FAIL; break; }
        for (int k = 0; k < savedIssueCount && !loadError; k++) {
            Issue *issue = &repo->issues[repo->issueCount++];
            ReadTextInto(file, issue->title, TEXT_LEN);
            issue->open   = ReadNumberField(file) != 0;
            issue->time   = ReadNumberField(file);
            issue->author = version >= 2 ? (int)ReadNumberField(file) : -1;
        }

        if (version >= 5) {
            int savedDraftCount = (int)ReadNumberField(file);
            if (savedDraftCount < 0 || savedDraftCount > MAX_DRAFTS) { LOAD_FAIL; break; }
            for (int k = 0; k < savedDraftCount && !loadError; k++) {
                int userId = (int)ReadNumberField(file), draftFileCount = (int)ReadNumberField(file);
                if (draftFileCount < 0 || draftFileCount > MAX_DRAFT) { LOAD_FAIL; break; }
                char draftBranchName[NAME_LEN] = "", mergeShaHex[48] = "", mergeBranchName[NAME_LEN] = "";
                bool merging = false;
                if (version >= 8) { ReadTextInto(file, draftBranchName, NAME_LEN); merging = ReadNumberField(file) != 0; ReadTextInto(file, mergeShaHex, sizeof mergeShaHex); ReadTextInto(file, mergeBranchName, NAME_LEN); }
                int keptBranch = repo->currentBranch, branchIndex = draftBranchName[0] ? FindBranch(repo, draftBranchName) : -1;
                if (branchIndex >= 0) repo->currentBranch = branchIndex;  /* GetDraft makes it for the branch you're on */
                Draft *draft = (branchIndex >= 0 || !draftBranchName[0]) ? GetDraft(repo, userId, true) : NULL;  /* a draft for a branch that's gone is dropped */
                repo->currentBranch = keptBranch;
                if (draft && merging && ParseShaHex(mergeShaHex, draft->mergeSha)) { draft->merging = true; snprintf(draft->mergeName, NAME_LEN, "%s", mergeBranchName); }
                for (int j = 0; j < draftFileCount && !loadError; j++) {
                    char name[PATH_LEN];
                    ReadTextInto(file, name, PATH_LEN);
                    int textLength = 0;
                    char *text = ReadBytesField(file, &textLength);
                    bool deleted = ReadNumberField(file) != 0;
                    int baseBlob = (int)ReadNumberField(file);
                    if (!text) break;
                    if (baseBlob < -1 || baseBlob >= repo->blobCount) LOAD_FAIL;
                    if (draft && !loadError && draft->fileCount < MAX_DRAFT) {
                        WorkFile *draftFile = &draft->files[draft->fileCount++];
                        memset(draftFile, 0, sizeof *draftFile);
                        snprintf(draftFile->name, PATH_LEN, "%s", name);
                        WorkFileSetBytes(draftFile, text, textLength);
                        draftFile->deleted = deleted;
                        draftFile->baseBlob = baseBlob;
                    }
                    free(text);
                }
            }
        } else {
            /* the old working copy becomes the owner's draft: only the real changes */
            for (int k = 0; k < legacyFileCount; k++) {
                WorkFile *legacyFile = &legacyFiles[k];
                if (!SameAsBlob(repo, legacyFile, HeadBlobOf(repo, legacyFile->name))) {
                    WorkFile *draftFile = DraftEdit(repo, repo->owner, legacyFile->name);
                    if (draftFile) WorkFileSetBytes(draftFile, legacyFile->text, legacyFile->length);
                }
            }
            Commit *head = HeadCommit(repo);
            for (int k = 0; head && k < head->fileCount; k++) {
                bool stillInHead = false;
                for (int j = 0; j < legacyFileCount; j++) if (strcmp(legacyFiles[j].name, head->names[k]) == 0) stillInHead = true;
                if (!stillInHead) { WorkFile *draftFile = DraftEdit(repo, repo->owner, head->names[k]); if (draftFile) draftFile->deleted = true; }
            }
            for (int k = 0; k < legacyFileCount; k++) WorkFileFree(&legacyFiles[k]);
        }

        if (version >= 6 && !loadError) {
            repo->isFork    = ReadNumberField(file) != 0;
            repo->forkOwner = (int)ReadNumberField(file);
            ReadTextInto(file, repo->forkName, NAME_LEN);
            ReadTextInto(file, repo->forkBase, (int)sizeof repo->forkBase);
        }
    }
    fclose(file);

    /* older saves: turn every commit into a git commit. Forks remember where they
       started by the original's old short id, so that's matched up first. */
    if (!loadError && version < 7) {
        static int forkBaseAt[MAX_REPOS];
        for (int i = 0; i < repoCount; i++) {
            Repo *repo = &repos[i];
            forkBaseAt[i] = -1;
            if (!repo->isFork || !repo->forkBase[0]) continue;
            for (int j = 0; j < repoCount && forkBaseAt[i] < 0; j++)
                if (repos[j].owner == repo->forkOwner && EqualsIgnoreCase(repos[j].name, repo->forkName))
                    for (int k = 0; k < repos[j].commitCount; k++)
                        if (strcmp(repos[j].commits[k].id, repo->forkBase) == 0) { forkBaseAt[i] = j * 1000000 + k; break; }
        }
        for (int i = 0; i < repoCount && !loadError; i++) if (!UpgradeCommits(&repos[i])) LOAD_FAIL;
        for (int i = 0; i < repoCount && !loadError; i++) {
            Repo *repo = &repos[i];
            if (!repo->isFork || !repo->forkBase[0]) continue;
            if (forkBaseAt[i] < 0) { snprintf(repo->forkBase, sizeof repo->forkBase, "%040d", 0); continue; }  /* no longer matches */
            ShaToHex(repos[forkBaseAt[i] / 1000000].commits[forkBaseAt[i] % 1000000].sha, repo->forkBase);
        }
        if (!loadError) {
            /* keep the old file, just in case */
            char backupName[64];
            snprintf(backupName, sizeof backupName, "%s.v%d-backup", SAVE_FILE, version);
            if (!FileExists(backupName)) {
                int size = 0;
                unsigned char *fileBytes = LoadFileData(SAVE_FILE, &size);
                if (fileBytes) { SaveFileData(backupName, fileBytes, size); UnloadFileData(fileBytes); }
            }
            MarkDirty();
        }
    }

    if (loadError) {
        /* Don't touch the file: it stays exactly as it is, and nothing is saved
           over it until you choose what to do (see ScreenLoadFail). */
        for (int i = 0; i < repoCount; i++) FreeRepo(&repos[i]);
        for (int i = 0; i < userCount; i++) free(users[i].avatar);
        memset(repos, 0, sizeof repos);
        memset(users, 0, sizeof users);
        repoCount = userCount = 0;
        nextUserId = 1;
        lastUserName[0] = 0;
        if (!loadReason[0]) snprintf(loadReason, sizeof loadReason, "part of it didn't make sense (check %d)", loadFailCheck);
        loadFailed = true;
        return false;
    }
    if (repairedCount) {                                     /* save the mended version (the old file is kept, below) */
        char backupName[64];
        snprintf(backupName, sizeof backupName, "%s.before-repair", SAVE_FILE);
        if (!FileExists(backupName)) { int size = 0; unsigned char *fileBytes = LoadFileData(SAVE_FILE, &size); if (fileBytes) { SaveFileData(backupName, fileBytes, size); UnloadFileData(fileBytes); } }
        MarkDirty();
        Toast(TextFormat("Mended the history of %d repositor%s stored out of order by an older version", repairedCount, repairedCount == 1 ? "y" : "ies"));
        return true;
    }
    /* it loaded: keep a copy of this good file, in case anything goes wrong later */
    if (version >= 7) {
        int size = 0;
        unsigned char *fileBytes = LoadFileData(SAVE_FILE, &size);
        if (fileBytes) { SaveFileData(SAVE_FILE ".last-good", fileBytes, size); UnloadFileData(fileBytes); }
    }
    return true;
}
