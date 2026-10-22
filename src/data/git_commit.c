/*
 * git_commit.c - real git commits
 *
 * Working out tree (folder) fingerprints, writing and reading git's commit
 * text, and adding finished commits to a repository.
 */

#include "git_commit.h"
#include "byte_buffer.h"
#include "git_paths.h"
#include "model.h"
#include "repo.h"
#include "sha1.h"
#include "sha_map.h"
#include "snapshot.h"
#include "users.h"
#include "util.h"

/* ------------------------------------------------------------------ */
/* Git's building blocks                                              */
/*                                                                    */
/* Real git names every piece of a repository by a SHA-1 fingerprint  */
/* of its contents. There are three kinds of piece ("object"):        */
/*   blob   - one version of one file's bytes                         */
/*   tree   - one folder: a list of names, each pointing at a blob    */
/*            (a file) or another tree (a folder inside it)           */
/*   commit - a tree (the snapshot), the commit(s) before it, who     */
/*            made it, when, and the message                          */
/* Mini GitHub stores things the same way, with the same fingerprints, */
/* so real git can push to it and clone from it.                      */
/* ------------------------------------------------------------------ */

/* ---------- trees (folders) ----------
   Git stores each folder as a "tree": for every entry, its mode, name and
   fingerprint. Because the paths are sorted, each folder's files sit next
   to each other in the list, in exactly the order git wants. */

/* Works out the fingerprint of the folder holding entries first..end-1 (paths all
   starting with the same prefixLength characters, e.g. "src/"). shas[i] is each
   file's blob fingerprint. emit, if given, is handed every tree made. */
bool BuildTree(const char *const *names, const uint8_t (*shas)[20], const uint8_t *modes,
                      int first, int end, int prefixLength, uint8_t treeSha[20], TreeEmit emit, void *context)
{
    ByteBuffer tree = {0};
    int i = first;
    while (i < end) {
        const char *rest = names[i] + prefixLength;
        const char *slash = strchr(rest, '/');
        if (!slash) {
            BufferAppendString(&tree, GitModeText(modes[i])); BufferAppendByte(&tree, ' ');
            BufferAppend(&tree, rest, strlen(rest) + 1);
            BufferAppend(&tree, shas[i], 20);
            i++;
            continue;
        }
        size_t folderNameLength = (size_t)(slash - rest);
        int folderEnd = i + 1;
        while (folderEnd < end && strncmp(names[folderEnd] + prefixLength, rest, folderNameLength + 1) == 0) folderEnd++;
        uint8_t subtreeSha[20];
        if (!BuildTree(names, shas, modes, i, folderEnd, prefixLength + (int)folderNameLength + 1, subtreeSha, emit, context)) { BufferFree(&tree); return false; }
        BufferAppendString(&tree, "40000 ");
        BufferAppend(&tree, rest, folderNameLength); BufferAppendByte(&tree, 0);
        BufferAppend(&tree, subtreeSha, 20);
        i = folderEnd;
    }
    if (tree.outOfMemory) { BufferFree(&tree); return false; }
    GitObjectSha("tree", tree.data ? tree.data : (const uint8_t *)"", tree.length, treeSha);
    if (emit) emit(context, treeSha, tree.data ? tree.data : (const uint8_t *)"", tree.length);
    BufferFree(&tree);
    return true;
}
/* The fingerprint of a commit's top folder; emit, if given, is handed every tree made on the way. */
bool CommitTree(Repo *repo, Commit *commit, uint8_t treeSha[20], TreeEmit emit, void *context)
{
    uint8_t (*blobShas)[20] = malloc((size_t)(commit->fileCount ? commit->fileCount : 1) * 20);
    if (!blobShas) return false;
    for (int i = 0; i < commit->fileCount; i++) memcpy(blobShas[i], repo->blobs[commit->blobs[i]].sha, 20);
    bool ok = BuildTree(commit->names, (const uint8_t (*)[20])blobShas, commit->modes, 0, commit->fileCount, 0, treeSha, emit, context);
    free(blobShas);
    return ok;
}

/* ---------- the git commit object ---------- */

/* Minutes ahead of UTC for local time at moment when (e.g. +60 for Paris in winter). */
static int TimezoneOffset(long long when)
{
    time_t whenTime = (time_t)when;
    struct tm local = *localtime(&whenTime), utc = *gmtime(&whenTime);
    int minutes = (local.tm_hour - utc.tm_hour) * 60 + (local.tm_min - utc.tm_min);
    int dayDifference = local.tm_year != utc.tm_year ? (local.tm_year > utc.tm_year ? 1 : -1) : local.tm_yday - utc.tm_yday;
    return minutes + dayDifference * 1440;
}

/* What git calls the author: "name <email> time zone". */
static void AuthorLine(ByteBuffer *buffer, const char *who, const char *email, long long when)
{
    int offset = TimezoneOffset(when), absOffset = offset < 0 ? -offset : offset;
    BufferAppendString(buffer, who); BufferAppendString(buffer, " <"); BufferAppendString(buffer, email); BufferAppendString(buffer, "> ");
    char timestamp[48];
    snprintf(timestamp, sizeof timestamp, "%lld %c%02d%02d", when, offset < 0 ? '-' : '+', absOffset / 60, absOffset % 60);
    BufferAppendString(buffer, timestamp);
}

/* The text git stores for a commit (tree, parents, author, committer, message). Returns a malloc'd
   string; *length gets its length. */
char *MakeRawCommit(const uint8_t treeSha[20], const uint8_t *const *parents, int parentCount,
                           const char *who, const char *email, long long when, const char *message, int *length)
{
    ByteBuffer buffer = {0};
    char hex[41];
    ShaToHex(treeSha, hex);
    BufferAppendString(&buffer, "tree "); BufferAppendString(&buffer, hex); BufferAppendByte(&buffer, '\n');
    for (int i = 0; i < parentCount; i++) { ShaToHex(parents[i], hex); BufferAppendString(&buffer, "parent "); BufferAppendString(&buffer, hex); BufferAppendByte(&buffer, '\n'); }
    BufferAppendString(&buffer, "author ");    AuthorLine(&buffer, who, email, when); BufferAppendByte(&buffer, '\n');
    BufferAppendString(&buffer, "committer "); AuthorLine(&buffer, who, email, when); BufferAppendByte(&buffer, '\n');
    BufferAppendByte(&buffer, '\n');
    BufferAppendString(&buffer, message);
    BufferAppendByte(&buffer, '\n');
    if (buffer.outOfMemory) { BufferFree(&buffer); return NULL; }
    *length = (int)buffer.length;
    BufferAppendByte(&buffer, 0);
    return (char *)buffer.data;
}

/* Reads the parts of a git commit Mini GitHub uses into *info. False if it has no tree. */
bool ParseCommit(const char *raw, int length, CommitInfo *info)
{
    memset(info, 0, sizeof *info);
    int pos = 0;
    bool sawTree = false;
    while (pos < length) {
        int lineEnd = pos;
        while (lineEnd < length && raw[lineEnd] != '\n') lineEnd++;
        if (lineEnd == pos) { pos = lineEnd + 1; break; }  /* blank line: the message follows */
        const char *line = raw + pos;
        int lineLength = lineEnd - pos;
        if (lineLength == 45 && strncmp(line, "tree ", 5) == 0) sawTree = ParseShaHex(line + 5, info->tree);
        else if (lineLength == 47 && strncmp(line, "parent ", 7) == 0) {
            if (info->parentCount >= MAX_PARENTS || !ParseShaHex(line + 7, info->parents[info->parentCount])) return false;
            info->parentCount++;
        } else if (lineLength > 7 && strncmp(line, "author ", 7) == 0) {
            const char *emailStart = memchr(line, '<', (size_t)lineLength), *emailEnd = emailStart ? memchr(emailStart, '>', (size_t)(lineLength - (emailStart - line))) : NULL;
            if (emailStart && emailEnd) {
                int nameLength = (int)(emailStart - (line + 7));
                while (nameLength > 0 && line[7 + nameLength - 1] == ' ') nameLength--;
                snprintf(info->authorName, sizeof info->authorName, "%.*s", nameLength, line + 7);
                snprintf(info->authorEmail, sizeof info->authorEmail, "%.*s", (int)(emailEnd - emailStart - 1), emailStart + 1);
                info->authorTime = strtoll(emailEnd + 1, NULL, 10);
            }
        }
        pos = lineEnd + 1;
    }
    /* the summary: the message's first line that isn't blank */
    while (pos < length && (raw[pos] == '\n' || raw[pos] == ' ' || raw[pos] == '\r')) pos++;
    int lineEnd = pos;
    while (lineEnd < length && raw[lineEnd] != '\n') lineEnd++;
    while (lineEnd > pos && (raw[lineEnd - 1] == '\r' || raw[lineEnd - 1] == ' ')) lineEnd--;
    snprintf(info->summary, sizeof info->summary, "%.*s", lineEnd - pos, raw + pos);
    if (!info->summary[0]) snprintf(info->summary, sizeof info->summary, "(no message)");
    return sawTree;
}
/* Who to show as a commit's author: the Mini GitHub user, plus the name
   git has if that's different (e.g. "Jo Lalor (jo)" for a pushed commit). */
const char *CommitAuthorName(Commit *commit)
{
    if (IsOutsider(commit->author)) return AuthorLabel(commit->author);
    const char *userName = UserName(commit->author);
    const char *authorLine = commit->raw ? strstr(commit->raw, "\nauthor ") : NULL;
    if (!authorLine) return userName;
    authorLine += 8;
    const char *emailStart = strchr(authorLine, '<'), *lineEnd = strchr(authorLine, '\n');
    if (!emailStart || (lineEnd && emailStart > lineEnd)) return userName;
    int nameLength = (int)(emailStart - authorLine);
    while (nameLength > 0 && authorLine[nameLength - 1] == ' ') nameLength--;
    if (nameLength <= 0 || nameLength > 60 || ((int)strlen(userName) == nameLength && SameTextIgnoreCase(authorLine, userName, nameLength))) return userName;
    return TextFormat("%.*s (%s)", nameLength, authorLine, userName);
}

/* Adds a finished commit to the end of the history (the repository owns its lists from now on). */
bool AppendCommit(Repo *repo, Commit *commit)
{
    if (repo->commitCount >= MAX_COMMITS) return false;
    repo->commits = GrowArray(repo->commits, &repo->commitCapacity, repo->commitCount + 1, sizeof(Commit));
    if (repo->commitCount >= repo->commitCapacity) return false;
    GitObjectSha("commit", commit->raw, (size_t)commit->rawLen, commit->sha);
    char hex[41];
    ShaToHex(commit->sha, hex);
    snprintf(commit->id, sizeof commit->id, "%.7s", hex);
    commit->statsDone = false;
    ShaMapPut(&repo->commitMap, commit->sha, repo->commitCount);
    repo->commits[repo->commitCount++] = *commit;
    repo->generation++;
    return true;
}

/* A file can't have the same name as a folder ("docs" and "docs/guide.md"):
   git couldn't store that. Returns the clashing name, or NULL. */
const char *PathClash(const char *const *names, int count)
{
    char prefix[PATH_LEN + 2];
    for (int i = 0; i < count; i++) {
        snprintf(prefix, sizeof prefix, "%s/", names[i]);
        size_t prefixLength = strlen(prefix);
        int low = i + 1, high = count;                              /* the first name >= "name/" */
        while (low < high) { int mid = (low + high) / 2; if (strcmp(names[mid], prefix) < 0) low = mid + 1; else high = mid; }
        if (low < count && strncmp(names[low], prefix, prefixLength) == 0) return names[i];
    }
    return NULL;
}

/* Turns a snapshot into a new commit by the signed-in user, on top of the
   latest commit (and, for a merge, also on top of commit mergedCommit - or -1).
   The snapshot is used up either way. Returns false if there was no room. */
bool FinishCommit(Repo *repo, Snapshot *snap, const char *message, int mergedCommit)
{
    Commit commit;
    memset(&commit, 0, sizeof commit);
    commit.fileCount = snap->count;
    commit.names = snap->names; commit.blobs = snap->blobs; commit.modes = snap->modes;
    memset(snap, 0, sizeof *snap);
    if (!commit.names) { commit.names = malloc(sizeof *commit.names); commit.blobs = malloc(sizeof *commit.blobs); commit.modes = malloc(1); }
    commit.author = currentUserId;
    commit.time = (long long)time(NULL);
    snprintf(commit.message, TEXT_LEN, "%s", message);
    const uint8_t *parentShas[2];
    int parentCount = 0;
    int head = HeadIndex(repo);
    if (head >= 0)      { commit.parents[parentCount] = head;               parentShas[parentCount++] = repo->commits[head].sha; }
    if (mergedCommit >= 0)      { commit.parents[parentCount] = mergedCommit;               parentShas[parentCount++] = repo->commits[mergedCommit].sha; }
    commit.parentCount = parentCount;
    uint8_t treeSha[20];
    if (commit.names && CommitTree(repo, &commit, treeSha, NULL, NULL))
        commit.raw = MakeRawCommit(treeSha, parentShas, parentCount, UserName(currentUserId), UserEmail(currentUserId), commit.time, message, &commit.rawLen);
    if (!commit.raw || !AppendCommit(repo, &commit)) { FreeCommit(&commit); return false; }
    /* the branch moves forward to the new commit (the very first commit makes the first branch) */
    if (repo->branchCount) repo->branches[repo->currentBranch].tip = repo->commitCount - 1;
    else { AddBranch(repo, repo->initialBranch, repo->commitCount - 1); repo->defaultBranch = repo->currentBranch = 0; }
    return true;
}
