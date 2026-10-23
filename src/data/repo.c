/*
 * repo.c - repositories, blobs and commits
 *
 * Creating and freeing repositories, storing each version of a file once
 * (blobs), and finding branches, the latest commit and the files in a
 * commit.
 */

#include "repo.h"
#include "model.h"
#include "save_file.h"
#include "sha1.h"
#include "sha_map.h"
#include "util.h"
#include "work_file.h"

/* ------------------------------------------------------------------ */
/* Repositories, blobs and commits                                    */
/* ------------------------------------------------------------------ */

/* Adds an empty repository to the list, or returns NULL if the list is full. */
Repo *NewRepo(int owner, const char *name, const char *description)
{
    if (repoCount >= MAX_REPOS) return NULL;
    Repo *repo = &repos[repoCount++];
    memset(repo, 0, sizeof *repo);
    repo->owner = owner;
    snprintf(repo->name, NAME_LEN, "%s", name);
    snprintf(repo->description, TEXT_LEN, "%s", description);
    snprintf(repo->initialBranch, NAME_LEN, "main");
    repo->created = (long long)time(NULL);
    return repo;
}

/* Frees every file in a draft. */
void FreeDraft(Draft *draft)
{
    for (int i = 0; i < draft->fileCount; i++) WorkFileFree(&draft->files[i]);
    free(draft->files);
    draft->files = NULL;
    draft->fileCount = 0;
}

/* Frees a commit's file lists and git data. */
void FreeCommit(Commit *commit)
{
    free(commit->names); free(commit->blobs); free(commit->modes); free(commit->raw); free(commit->langLines);
    commit->names = NULL; commit->blobs = NULL; commit->modes = NULL; commit->raw = NULL; commit->langLines = NULL;
}

/* Frees everything a repository holds. (A save being written in the background
   may still be reading its files' contents, so that finishes first.) */
void FreeRepo(Repo *repo)
{
    WaitForSave();
    for (int i = 0; i < repo->draftCount; i++) FreeDraft(&repo->drafts[i]);
    repo->draftCount = 0;
    for (int i = 0; i < repo->blobCount; i++) { free(repo->blobs[i].data); free(repo->blobs[i].lang); }
    for (int i = 0; i < repo->commitCount; i++) FreeCommit(&repo->commits[i]);
    free(repo->blobs); free(repo->commits);
    repo->blobs = NULL; repo->commits = NULL;
    repo->blobCount = repo->blobCapacity = repo->commitCount = repo->commitCapacity = 0;
    ShaMapFree(&repo->commitMap); ShaMapFree(&repo->blobMap);
    fileContentsGeneration++;
}

/* Removes a repository from the list (the ones after it move up). */
void DeleteRepo(int index)
{
    FreeRepo(&repos[index]);
    memmove(&repos[index], &repos[index + 1], (size_t)(repoCount - index - 1) * sizeof(Repo));
    repoCount--;
    memset(&repos[repoCount], 0, sizeof(Repo));
}

/* Store contents once; returns the blob's index (or -1 if out of room).
   If sha is given it must be the contents' git fingerprint (saves working it out again). */
int StoreBlobWithSha(Repo *repo, const char *data, int length, const uint8_t *knownSha)
{
    uint8_t sha[20];
    if (knownSha) memcpy(sha, knownSha, 20);
    else GitObjectSha("blob", data, (size_t)length, sha);
    int existing = ShaMapGet(&repo->blobMap, sha);
    if (existing >= 0) return existing;
    if (repo->blobCount >= MAX_BLOBS) return -1;
    repo->blobs = GrowArray(repo->blobs, &repo->blobCapacity, repo->blobCount + 1, sizeof(Blob));
    if (repo->blobCount >= repo->blobCapacity) return -1;
    Blob *blob = &repo->blobs[repo->blobCount];
    blob->data = malloc((size_t)length + 1);
    if (!blob->data) return -1;
    memcpy(blob->data, data, (size_t)length);
    blob->data[length] = 0;
    blob->length = length;
    blob->binary = LooksBinary(data, length);
    blob->lang = NULL;
    memcpy(blob->sha, sha, 20);
    ShaMapPut(&repo->blobMap, sha, repo->blobCount);
    return repo->blobCount++;
}
/* Stores a version of a file once; returns the blob's index (or -1 if there's no room). */
int StoreBlob(Repo *repo, const char *data, int length) { return StoreBlobWithSha(repo, data, length, NULL); }

/* A blob's contents ("" for blob -1). */
const char *BlobText(Repo *repo, int blob)   { return blob >= 0 ? repo->blobs[blob].data : ""; }
/* A blob's size in bytes (0 for blob -1). */
int         BlobLength(Repo *repo, int blob)    { return blob >= 0 ? repo->blobs[blob].length : 0; }
/* Is the blob a non-text file (false for blob -1)? */
bool        BlobIsBinary(Repo *repo, int blob) { return blob >= 0 && repo->blobs[blob].binary; }
/* The branch you're on, and its newest commit ("head"). An empty repository has no branches yet. */
int HeadIndex(Repo *repo)
{
    if (!repo->branchCount) return -1;
    if (repo->currentBranch < 0 || repo->currentBranch >= repo->branchCount) repo->currentBranch = repo->defaultBranch;
    return repo->branches[repo->currentBranch].tip;
}
/* The newest commit on the current branch, or NULL if there are no commits yet. */
Commit *HeadCommit(Repo *repo) { int head = HeadIndex(repo); return head >= 0 ? &repo->commits[head] : NULL; }
/* The name of the branch being looked at (or the name the first branch will get). */
const char *CurrentBranchName(Repo *repo) { HeadIndex(repo); return repo->branchCount ? repo->branches[repo->currentBranch].name : repo->initialBranch; }
/* The name of the default branch (or the name the first branch will get). */
const char *DefaultBranchName(Repo *repo) { return repo->branchCount ? repo->branches[repo->defaultBranch].name : repo->initialBranch; }
/* The newest commit on the default branch, or -1. */
int DefaultTip(Repo *repo) { return repo->branchCount ? repo->branches[repo->defaultBranch].tip : -1; }
/* When anything last happened in the repository: its creation, or its newest commit on any branch. */
long long LastActivity(Repo *repo)
{
    long long latest = repo->created;
    for (int branch = 0; branch < repo->branchCount; branch++) if (repo->commits[repo->branches[branch].tip].time > latest) latest = repo->commits[repo->branches[branch].tip].time;
    return latest;
}

/* The index of the branch called name, or -1. */
int FindBranch(Repo *repo, const char *name)
{
    for (int branch = 0; branch < repo->branchCount; branch++) if (strcmp(repo->branches[branch].name, name) == 0) return branch;
    return -1;
}
/* Adds a branch pointing at commit tip; returns its index, or -1 if there are too many. */
int AddBranch(Repo *repo, const char *name, int tip)
{
    if (repo->branchCount >= MAX_BRANCHES) return -1;
    char nameCopy[NAME_LEN];                   /* name may be part of the same repository (repo->initialBranch) */
    snprintf(nameCopy, sizeof nameCopy, "%s", name);
    Branch *branch = &repo->branches[repo->branchCount];
    memcpy(branch->name, nameCopy, sizeof nameCopy);
    branch->tip = tip;
    return repo->branchCount++;
}
/* The commit this one was built on (the first one, for a merge), or NULL. */
Commit *ParentOf(Repo *repo, int commitIndex)
{
    Commit *commit = &repo->commits[commitIndex];
    return commit->parentCount ? &repo->commits[commit->parents[0]] : NULL;
}
/* The index of the commit with this fingerprint, or -1. */
int CommitBySha(Repo *repo, const uint8_t *sha) { return ShaMapGet(&repo->commitMap, sha); }

/* Commits keep their files sorted by path, so finding one is quick. */
int FindInCommit(Commit *commit, const char *name)
{
    int low = 0, high = commit->fileCount - 1;
    while (low <= high) {
        int mid = (low + high) / 2, order = strcmp(commit->names[mid], name);
        if (!order) return mid;
        if (order < 0) low = mid + 1; else high = mid - 1;
    }
    return -1;
}

/* How many of the repository's issues are still open. */
int OpenIssueCount(Repo *repo)
{
    int count = 0;
    for (int i = 0; i < repo->issueCount; i++) if (repo->issues[i].open) count++;
    return count;
}
