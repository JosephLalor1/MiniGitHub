/*
 * repo_view.c - what someone sees in a repository
 *
 * The latest commit with their own draft laid on top, as one sorted list
 * of files.
 */

#include "repo_view.h"
#include "drafts.h"
#include "git_paths.h"
#include "model.h"
#include "repo.h"
#include "util.h"

ViewFile *viewFiles;
int       viewFileCount, viewFileCapacity;

static int CompareViewFiles(const void *left, const void *right) { return strcmp(((const ViewFile *)left)->name, ((const ViewFile *)right)->name); }

/* Fills viewFiles with what userId sees: the latest commit plus their draft (-1: just the latest
   commit). */
void BuildView(Repo *repo, int userId)
{
    viewFileCount = 0;
    Commit *head = HeadCommit(repo);
    Draft *draft = userId >= 0 ? GetDraft(repo, userId, false) : NULL;
    int maxFiles = (head ? head->fileCount : 0) + (draft ? draft->fileCount : 0);
    viewFiles = GrowArray(viewFiles, &viewFileCapacity, maxFiles + 1, sizeof(ViewFile));
    if (viewFileCapacity < maxFiles) return;
    for (int index = 0; head && index < head->fileCount; index++) {
        ViewFile *viewFile = &viewFiles[viewFileCount++];
        memset(viewFile, 0, sizeof *viewFile);
        viewFile->name = head->names[index];
        viewFile->blob = head->blobs[index];
        int draftIndex = DraftIndex(draft, viewFile->name);
        WorkFile *draftFile = draftIndex >= 0 ? &draft->files[draftIndex] : NULL;
        char status = draftFile ? DraftStatus(repo, draftFile) : 0;
        if (draftFile && status) { viewFile->draftFile = draftFile; viewFile->status = status; viewFile->data = draftFile->text; viewFile->length = draftFile->length; viewFile->binary = draftFile->binary; }
        else { viewFile->data = repo->blobs[viewFile->blob].data; viewFile->length = repo->blobs[viewFile->blob].length; viewFile->binary = repo->blobs[viewFile->blob].binary; }
    }
    bool addedNewFiles = false;
    for (int draftIndex = 0; draft && draftIndex < draft->fileCount; draftIndex++) {  /* your new files */
        WorkFile *draftFile = &draft->files[draftIndex];
        if (HeadBlobOf(repo, draftFile->name) >= 0) continue;
        char status = DraftStatus(repo, draftFile);
        if (!status) continue;
        const char *name = InternPath(draftFile->name);
        if (!name) continue;
        ViewFile *viewFile = &viewFiles[viewFileCount++];
        memset(viewFile, 0, sizeof *viewFile);
        viewFile->name = name;
        viewFile->blob = -1; viewFile->draftFile = draftFile; viewFile->status = status; viewFile->data = draftFile->text; viewFile->length = draftFile->length; viewFile->binary = draftFile->binary;
        addedNewFiles = true;
    }
    if (addedNewFiles) qsort(viewFiles, (size_t)viewFileCount, sizeof(ViewFile), CompareViewFiles);  /* keep folders together */
}
/* Where a file is in viewFiles, or -1. */
int ViewIndex(const char *name)
{
    int low = 0, high = viewFileCount - 1;
    while (low <= high) {
        int mid = (low + high) / 2, order = strcmp(viewFiles[mid].name, name);
        if (!order) return mid;
        if (order < 0) low = mid + 1; else high = mid - 1;
    }
    return -1;
}
/* Has the viewer deleted this file in their draft? */
bool ViewDeleted(ViewFile *viewFile) { return viewFile->draftFile && viewFile->draftFile->deleted; }
