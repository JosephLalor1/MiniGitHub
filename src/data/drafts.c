/*
 * drafts.c - each person's uncommitted changes
 *
 * Changing files without touching anyone else's work, spotting when
 * someone committed a newer version in the meantime, and turning a draft
 * into a commit.
 */

#include "drafts.h"
#include "diff.h"
#include "git_commit.h"
#include "merge.h"
#include "model.h"
#include "repo.h"
#include "snapshot.h"
#include "work_file.h"

/* ---------- drafts ---------- */

/* The blob a file has in the latest commit, or -1 if it isn't there. */
int HeadBlobOf(Repo *repo, const char *name)
{
    Commit *head = HeadCommit(repo);
    int index = head ? FindInCommit(head, name) : -1;
    return index >= 0 ? head->blobs[index] : -1;
}

/* Is the file's text exactly the same as this blob's? */
bool SameAsBlob(Repo *repo, WorkFile *file, int blob)
{
    return blob >= 0 && repo->blobs[blob].length == file->length && memcmp(repo->blobs[blob].data, file->text, (size_t)file->length) == 0;
}

/* The changes userId has made on the branch being worked on (made if create and there are none yet). */
Draft *GetDraft(Repo *repo, int userId, bool create)
{
    const char *branchName = CurrentBranchName(repo);
    for (int i = 0; i < repo->draftCount; i++) if (repo->drafts[i].userId == userId && strcmp(repo->drafts[i].branch, branchName) == 0) return &repo->drafts[i];
    if (!create || repo->draftCount >= MAX_DRAFTS) return NULL;
    WorkFile *files = calloc(MAX_DRAFT, sizeof(WorkFile));
    if (!files) return NULL;
    Draft *draft = &repo->drafts[repo->draftCount++];
    memset(draft, 0, sizeof *draft);
    draft->userId = userId; draft->fileCount = 0; draft->files = files;
    snprintf(draft->branch, NAME_LEN, "%s", branchName);
    return draft;
}

/* Where a file is in a draft, or -1 (draft may be NULL). */
int DraftIndex(Draft *draft, const char *name)
{
    for (int index = 0; draft && index < draft->fileCount; index++) if (strcmp(draft->files[index].name, name) == 0) return index;
    return -1;
}
/* Your draft's copy of a file, or NULL if you haven't changed it. */
WorkFile *DraftFind(Repo *repo, int userId, const char *name)
{
    Draft *draft = GetDraft(repo, userId, false);
    int index = DraftIndex(draft, name);
    return index >= 0 ? &draft->files[index] : NULL;
}

static void DraftDrop(Draft *draft, int index)
{
    WorkFileFree(&draft->files[index]);
    memmove(&draft->files[index], &draft->files[index + 1], (size_t)(draft->fileCount - index - 1) * sizeof(WorkFile));
    draft->fileCount--;
    memset(&draft->files[draft->fileCount], 0, sizeof(WorkFile));
}
/* Forgets your change to one file (it shows the latest commit's version again). */
void DraftDropName(Repo *repo, int userId, const char *name)
{
    Draft *draft = GetDraft(repo, userId, false);
    int index = DraftIndex(draft, name);
    if (index >= 0) DraftDrop(draft, index);
}
/* Forgets all your changes on the current branch, and any merge in progress. */
void DraftClear(Repo *repo, int userId)
{
    Draft *draft = GetDraft(repo, userId, false);
    while (draft && draft->fileCount) DraftDrop(draft, draft->fileCount - 1);
    if (draft) draft->merging = false;
}
/* Throws away drafts: userId's (on every branch) if branch is NULL, or everyone's on that branch if userId is -1. */
void RemoveDrafts(Repo *repo, int userId, const char *branch)
{
    for (int i = repo->draftCount - 1; i >= 0; i--)
        if ((userId < 0 || repo->drafts[i].userId == userId) && (!branch || strcmp(repo->drafts[i].branch, branch) == 0)) {
            FreeDraft(&repo->drafts[i]);
            memmove(&repo->drafts[i], &repo->drafts[i + 1], (size_t)(repo->draftCount - i - 1) * sizeof(Draft));
            repo->draftCount--;
            memset(&repo->drafts[repo->draftCount], 0, sizeof(Draft));
        }
}
/* Throws away all of userId's drafts, on every branch. */
void RemoveDraft(Repo *repo, int userId) { RemoveDrafts(repo, userId, NULL); }

/* Your own editable copy of a file: made from the latest commit the first
   time you change it (or empty, for a new file). NULL if your draft is full. */
WorkFile *DraftEdit(Repo *repo, int userId, const char *name)
{
    Draft *draft = GetDraft(repo, userId, true);
    if (!draft) return NULL;
    int i = DraftIndex(draft, name);
    if (i >= 0) return &draft->files[i];
    if (draft->fileCount >= MAX_DRAFT || strlen(name) >= PATH_LEN) return NULL;
    WorkFile *file = &draft->files[draft->fileCount++];
    memset(file, 0, sizeof *file);
    snprintf(file->name, PATH_LEN, "%s", name);
    file->baseBlob = HeadBlobOf(repo, name);
    if (file->baseBlob >= 0) WorkFileSetBytes(file, repo->blobs[file->baseBlob].data, repo->blobs[file->baseBlob].length);
    else              WorkFileSetBytes(file, "", 0);
    return file;
}
/* Sets the text of a file in your draft (adding the file if it's new). */
void DraftSetText(Repo *repo, int userId, const char *name, const char *text)
{
    WorkFile *file = DraftEdit(repo, userId, name);
    if (file) { file->deleted = false; WorkFileSetText(file, text); }
}

/* What a file in your draft means:
   'A' a new file, 'M' modified, 'D' deleted, 0 no real change,
   'C' a conflict - someone committed a different version of this file
       after you started changing it, so committing yours would wipe theirs. */
char DraftStatus(Repo *repo, WorkFile *draftFile)
{
    int currentBlob = HeadBlobOf(repo, draftFile->name);
    if (draftFile->deleted) {
        if (currentBlob < 0) return 0;                          /* it's gone anyway */
        return draftFile->baseBlob == currentBlob ? 'D' : 'C';  /* changed by someone after you deleted it */
    }
    if (SameAsBlob(repo, draftFile, currentBlob)) return 0;                                      /* yours matches the latest version */
    if (draftFile->baseBlob >= 0 && SameAsBlob(repo, draftFile, draftFile->baseBlob)) return 0;  /* you haven't actually changed it */
    if (draftFile->baseBlob != currentBlob) return 'C';
    return currentBlob < 0 ? 'A' : 'M';
}

/* Throws away draft files that aren't real changes, so they quietly follow
   the latest commit again. */
void DraftTidy(Repo *repo, int userId)
{
    Draft *draft = GetDraft(repo, userId, false);
    if (!draft) return;
    for (int i = draft->fileCount - 1; i >= 0; i--) if (!DraftStatus(repo, &draft->files[i])) DraftDrop(draft, i);
}

/* How many of your files are real changes, and how many are conflicts. */
void DraftCounts(Repo *repo, int userId, int *changedFiles, int *conflicts)
{
    *changedFiles = *conflicts = 0;
    Draft *draft = GetDraft(repo, userId, false);
    for (int i = 0; draft && i < draft->fileCount; i++) {
        char status = DraftStatus(repo, &draft->files[i]);
        if (status == 'C') (*conflicts)++;
        else if (status)   (*changedFiles)++;
    }
}

/* Who most recently committed a change to this file (or -1): follows the
   latest commit back through the commits it was built on. */
int LastChangedBy(Repo *repo, const char *name)
{
    int commitIndex = HeadIndex(repo);
    while (commitIndex >= 0) {
        Commit *commit = &repo->commits[commitIndex], *parent = ParentOf(repo, commitIndex);
        int indexNow = FindInCommit(commit, name);
        int blobNow = indexNow >= 0 ? commit->blobs[indexNow] : -1, blobBefore = -1;
        if (parent) { int parentIndex = FindInCommit(parent, name); blobBefore = parentIndex >= 0 ? parent->blobs[parentIndex] : -1; }
        if (blobNow != blobBefore) return commit->author;
        commitIndex = commit->parentCount ? commit->parents[0] : -1;
    }
    return -1;
}

/* Returns NULL on success, or a message explaining what went wrong. */
const char *DoCommit(Repo *repo, const char *message)
{
    if (!message[0]) return "Write a commit message first";
    DraftTidy(repo, currentUserId);
    int changedFiles, conflicts;
    DraftCounts(repo, currentUserId, &changedFiles, &conflicts);
    Draft *mergeDraft = GetDraft(repo, currentUserId, false);
    bool merging = mergeDraft && mergeDraft->merging;
    int mergedCommit = merging ? CommitBySha(repo, mergeDraft->mergeSha) : -1;
    if (conflicts)                     return "Sort out the conflicts first (files marked !)";
    if (!changedFiles && !merging)          return "Nothing to commit - no changes since the last commit";
    if (repo->commitCount >= MAX_COMMITS) return "This repository has reached its commit limit";
    if (merging && mergedCommit < 0)           return "The branch being merged has gone - abort the merge";
    if (merging && FileWithConflictMarkers(repo))     return TextFormat("%s still has conflict markers (<<<<<<<) - fix those parts first", FileWithConflictMarkers(repo));

    /* start from the latest commit, then apply your changes */
    Draft *draft = GetDraft(repo, currentUserId, false);
    Snapshot snap;
    if (!SnapshotFromCommit(&snap, HeadCommit(repo))) return "Out of memory";
    for (int i = 0; draft && i < draft->fileCount; i++) {
        WorkFile *draftFile = &draft->files[i];
        char status = DraftStatus(repo, draftFile);
        if (status == 'D') { SnapshotRemove(&snap, draftFile->name); continue; }
        int blob = StoreBlob(repo, draftFile->text, draftFile->length);
        if (blob < 0) { SnapshotFree(&snap); return "This repository is out of storage space"; }
        if (!SnapshotSet(&snap, draftFile->name, blob, -1)) { SnapshotFree(&snap); return TextFormat("A repository can hold up to %d files", MAX_FILES); }
    }
    const char *clash = PathClash(snap.names, snap.count);
    if (clash) { SnapshotFree(&snap); return TextFormat("%s is both a file and a folder - rename one of them first", clash); }
    if (!FinishCommit(repo, &snap, message, mergedCommit)) return "Couldn't save the commit (out of memory?)";
    DraftClear(repo, currentUserId);        /* everything is committed now (and a merge is finished) */
    return NULL;
}

/* Replace the working files with the files from an old commit. */
void RestoreCommit(Repo *repo, int commitIndex)
{
    DraftClear(repo, currentUserId);
    Commit *commit = &repo->commits[commitIndex], *head = HeadCommit(repo);
    for (int index = 0; index < commit->fileCount; index++) {
        if (HeadBlobOf(repo, commit->names[index]) == commit->blobs[index]) continue;  /* already the same */
        WorkFile *draftFile = DraftEdit(repo, currentUserId, commit->names[index]);
        if (draftFile) WorkFileSetBytes(draftFile, repo->blobs[commit->blobs[index]].data, repo->blobs[commit->blobs[index]].length);
    }
    for (int index = 0; head && index < head->fileCount; index++)        /* files that didn't exist back then */
        if (FindInCommit(commit, head->names[index]) < 0) {
            WorkFile *draftFile = DraftEdit(repo, currentUserId, head->names[index]);
            if (draftFile) draftFile->deleted = true;
        }
}
