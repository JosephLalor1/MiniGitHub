/*
 * merge.c - merging
 *
 * A three-way, line-by-line merge of text files, and merging one branch
 * into another: at once when nothing clashes, otherwise through your draft
 * with the clashes marked for you to fix.
 */

#include "merge.h"
#include "branches.h"
#include "byte_buffer.h"
#include "diff.h"
#include "drafts.h"
#include "forks.h"
#include "git_commit.h"
#include "model.h"
#include "repo.h"
#include "snapshot.h"
#include "util.h"
#include "work_file.h"

/* ---------- merging two versions of a text file, line by line ----------
   The classic "three-way merge": compare each side with the common starting
   version (base). Lines that are the same in all three are fixed points;
   between two fixed points, if only one side changed something, take that
   side; if both made the very same change, fine; if they changed it in
   different ways, that's a conflict - both are kept, between git's markers:
       <<<<<<< main
       (your lines)
       =======
       (their lines)
       >>>>>>> feature */
static void MapLines(const char *base, const char *other, int baseCount, int *map)
{
    for (int i = 0; i < baseCount; i++) map[i] = -1;
    ComputeDiff(base, other, NULL, NULL);
    for (int i = 0; i < diffOpCount; i++) if (diffOps[i].kind == ' ' && diffOps[i].oldLine < baseCount) map[diffOps[i].oldLine] = diffOps[i].newLine;
}
static bool SameLines(LineRef *a, int aStart, int aEnd, LineRef *b, int bStart, int bEnd)
{
    if (aEnd - aStart != bEnd - bStart) return false;
    for (int i = 0; i < aEnd - aStart; i++) if (!SameLine(a[aStart + i], b[bStart + i])) return false;
    return true;
}
static void AddLines(ByteBuffer *out, LineRef *lines, int from, int to)
{
    for (int i = from; i < to; i++) { BufferAppend(out, lines[i].start, (size_t)lines[i].length); BufferAppendByte(out, '\n'); }
}
static bool EndsWithNewline(const char *text, int length) { return length > 0 && text[length - 1] == '\n'; }

/* Returns the merged text (malloc'd, NUL-terminated); *conflicts gets how many clashing places. */
static char *MergeText(const char *baseText, int baseLength, const char *ourText, int ourLength, const char *theirText, int theirLength,
                       const char *ourName, const char *theirName, int *outLength, int *conflicts)
{
    LineRef *baseLines = NULL, *ourLines = NULL, *theirLines = NULL;
    int baseCapacity = 0, ourCapacity = 0, theirCapacity = 0;
    int baseCount = SplitLines(baseText, &baseLines, &baseCapacity), ourCount = SplitLines(ourText, &ourLines, &ourCapacity), theirCount = SplitLines(theirText, &theirLines, &theirCapacity);
    int *baseToOurs = malloc(((size_t)baseCount + 1) * sizeof(int)), *baseToTheirs = malloc(((size_t)baseCount + 1) * sizeof(int));
    ByteBuffer merged = {0};
    *conflicts = 0;
    if (baseToOurs && baseToTheirs) {
        MapLines(baseText, ourText, baseCount, baseToOurs);
        MapLines(baseText, theirText, baseCount, baseToTheirs);
        int prevBase = -1, prevOurs = -1, prevTheirs = -1;
        for (int i = 0; i <= baseCount; i++) {
            if (i < baseCount && (baseToOurs[i] < 0 || baseToTheirs[i] < 0 || baseToOurs[i] <= prevOurs || baseToTheirs[i] <= prevTheirs)) continue;  /* not a fixed point */
            int ourLine = i < baseCount ? baseToOurs[i] : ourCount, theirLine = i < baseCount ? baseToTheirs[i] : theirCount;
            int baseStart = prevBase + 1, ourStart = prevOurs + 1, theirStart = prevTheirs + 1;
            bool oursUnchanged = SameLines(ourLines, ourStart, ourLine, baseLines, baseStart, i), theirsUnchanged = SameLines(theirLines, theirStart, theirLine, baseLines, baseStart, i);
            if (oursUnchanged)                                    AddLines(&merged, theirLines, theirStart, theirLine);  /* only they changed it  */
            else if (theirsUnchanged || SameLines(ourLines, ourStart, ourLine, theirLines, theirStart, theirLine)) AddLines(&merged, ourLines, ourStart, ourLine);  /* only you, or the same */
            else {                                                                       /* both, differently     */
                (*conflicts)++;
                BufferAppendString(&merged, "<<<<<<< "); BufferAppendString(&merged, ourName); BufferAppendByte(&merged, '\n');
                AddLines(&merged, ourLines, ourStart, ourLine);
                BufferAppendString(&merged, "=======\n");
                AddLines(&merged, theirLines, theirStart, theirLine);
                BufferAppendString(&merged, ">>>>>>> "); BufferAppendString(&merged, theirName); BufferAppendByte(&merged, '\n');
            }
            if (i < baseCount) AddLines(&merged, baseLines, i, i + 1);
            prevBase = i; prevOurs = ourLine; prevTheirs = theirLine;
        }
        /* does the file end with a newline? take whichever side changed that */
        bool baseEndsNewline = EndsWithNewline(baseText, baseLength), oursEndsNewline = EndsWithNewline(ourText, ourLength), theirsEndsNewline = EndsWithNewline(theirText, theirLength);
        bool wantNewline = oursEndsNewline == baseEndsNewline ? theirsEndsNewline : oursEndsNewline;
        if (!wantNewline && merged.length > 0 && merged.data[merged.length - 1] == '\n' && !*conflicts) merged.length--;
    }
    free(baseLines); free(ourLines); free(theirLines); free(baseToOurs); free(baseToTheirs);
    BufferAppendByte(&merged, 0);
    if (merged.outOfMemory) { BufferFree(&merged); return NULL; }
    *outLength = (int)merged.length - 1;
    return (char *)merged.data;
}

/* Does this text still have a conflict marker at the start of a line? */
bool HasConflictMarkers(const char *text, int length)
{
    for (int i = 0; i + 8 <= length; i++)
        if ((i == 0 || text[i - 1] == '\n') && memcmp(text + i, "<<<<<<< ", 8) == 0) return true;
    return false;
}

/* What happened in the last merge, for the message shown afterwards. */
static int mergeConflictFiles, mergeAutoFiles;
static char mergeNote[TEXT_LEN];

/* Merges branch fromBranch into the branch you're on.
   No clashes: a merge commit is made straight away. Clashes: the merged
   files go into your draft, the clashing places marked, and the merge is
   finished when you commit. Returns NULL (and *doneMessage) or an error. */
const char *MergeBranch(Repo *repo, int fromBranch, char *doneMessage, int doneCapacity)
{
    if (fromBranch < 0 || fromBranch >= repo->branchCount || fromBranch == repo->currentBranch) return "Pick another branch to merge";
    char ourName[NAME_LEN], theirName[NAME_LEN];
    snprintf(ourName, sizeof ourName, "%s", CurrentBranchName(repo));
    snprintf(theirName, sizeof theirName, "%s", repo->branches[fromBranch].name);
    int ourTip = HeadIndex(repo), theirTip = repo->branches[fromBranch].tip;
    if (IsAncestor(repo, theirTip, ourTip)) return TextFormat("Nothing to merge - %s already has everything from %s", ourName, theirName);
    Draft *draft = GetDraft(repo, currentUserId, false);
    if (draft && draft->merging) return "Finish (or abort) the merge you've started first";
    DraftTidy(repo, currentUserId);
    int changedFiles, conflictFiles;
    DraftCounts(repo, currentUserId, &changedFiles, &conflictFiles);
    if (changedFiles || conflictFiles) return TextFormat("Commit or undo your changes on %s first", ourName);
    if (repo->commitCount >= MAX_COMMITS) return "This repository has reached its commit limit";

    int baseIndex = MergeBase(repo, ourTip, theirTip);
    Commit *baseCommit = baseIndex >= 0 ? &repo->commits[baseIndex] : NULL, *ourCommit = &repo->commits[ourTip], *theirCommit = &repo->commits[theirTip];
    Snapshot allPaths = {0}, result;
    for (int side = 0; side < 3; side++) {                 /* every path on any side */
        Commit *commit = side == 0 ? ourCommit : side == 1 ? theirCommit : baseCommit;
        for (int j = 0; commit && j < commit->fileCount; j++)
            if (SnapshotFind(&allPaths, commit->names[j]) < 0 && !SnapshotSet(&allPaths, commit->names[j], 0, 0)) { SnapshotFree(&allPaths); return "Out of memory"; }
    }
    if (!SnapshotFromCommit(&result, ourCommit)) { SnapshotFree(&allPaths); return "Out of memory"; }
    /* conflicting texts wait here until we know whether there are any */
    typedef struct { const char *name; char *text; int length; } Clash;
    Clash *clashes = NULL;
    int clashCount = 0, clashCapacity = 0, autoMerged = 0, keptOurs = 0;
    const char *error = NULL;
    for (int q = 0; q < allPaths.count && !error; q++) {
        const char *path = allPaths.names[q];
        FileVersion baseVersion = FileVersionIn(repo, baseCommit, path), theirVersion = FileVersionIn(repo, theirCommit, path), ourVersion = FileVersionIn(repo, ourCommit, path);
        if (SameVersion(ourVersion, theirVersion) || SameVersion(baseVersion, theirVersion)) continue;  /* nothing new from them */
        if (SameVersion(baseVersion, ourVersion)) {                                     /* only they changed it  */
            if (!theirVersion.exists) { SnapshotRemove(&result, path); continue; }
            int blob = StoreBlob(repo, theirVersion.data, theirVersion.length);
            if (blob < 0 || !SnapshotSet(&result, path, blob, theirVersion.mode)) error = "This repository is out of storage space";
            continue;
        }
        /* both changed it */
        if (!ourVersion.exists || !theirVersion.exists) { keptOurs++; if (!ourVersion.exists) { int blob = StoreBlob(repo, theirVersion.data, theirVersion.length); if (blob < 0 || !SnapshotSet(&result, path, blob, theirVersion.mode)) error = "Out of space"; } continue; }  /* deleted on one side: keep the changed one */
        if (LooksBinary(ourVersion.data, ourVersion.length) || LooksBinary(theirVersion.data, theirVersion.length)) { keptOurs++; continue; }  /* can't merge pictures: keep yours */
        int mergedLength = 0, conflictCount = 0;
        char *mergedText = MergeText(baseVersion.exists ? baseVersion.data : "", baseVersion.length, ourVersion.data, ourVersion.length, theirVersion.data, theirVersion.length, ourName, theirName, &mergedLength, &conflictCount);
        if (!mergedText) { error = "Out of memory"; break; }
        if (conflictCount) {
            clashes = GrowArray(clashes, &clashCapacity, clashCount + 1, sizeof(Clash));
            if (clashCount < clashCapacity) clashes[clashCount++] = (Clash){path, mergedText, mergedLength}; else free(mergedText);
            continue;
        }
        autoMerged++;
        int blob = StoreBlob(repo, mergedText, mergedLength);
        free(mergedText);
        if (blob < 0 || !SnapshotSet(&result, path, blob, ourVersion.mode)) error = "This repository is out of storage space";
    }
    SnapshotFree(&allPaths);
    const char *pathClash = !error ? PathClash(result.names, result.count) : NULL;
    if (pathClash && !error) error = TextFormat("Can't merge: %s would be both a file and a folder", pathClash);
    if (error) { SnapshotFree(&result); for (int i = 0; i < clashCount; i++) free(clashes[i].text); free(clashes); return error; }

    mergeConflictFiles = clashCount; mergeAutoFiles = autoMerged;
    mergeNote[0] = 0;
    if (keptOurs) snprintf(mergeNote, sizeof mergeNote, " (%d file%s changed on both sides couldn't be combined - kept %s's version)", keptOurs, keptOurs == 1 ? "" : "s", ourName);
    char commitMessage[TEXT_LEN];
    snprintf(commitMessage, sizeof commitMessage, "Merge branch '%s' into %s", theirName, ourName);
    if (!clashCount) {                                       /* clean: commit it now */
        if (!FinishCommit(repo, &result, commitMessage, theirTip)) return "Couldn't save the merge commit";
        snprintf(doneMessage, (size_t)doneCapacity, "Merged %s into %s%s%s", theirName, ourName,
                 autoMerged ? TextFormat(" - %d file%s changed on both sides were combined line by line", autoMerged, autoMerged == 1 ? "" : "s") : "", mergeNote);
        return NULL;
    }
    /* clashes: everything the merge changes goes into your draft, and you finish it by committing */
    draft = GetDraft(repo, currentUserId, true);
    if (!draft) { SnapshotFree(&result); for (int i = 0; i < clashCount; i++) free(clashes[i].text); free(clashes); return "Too many drafts in this repository"; }
    int resultIndex = 0, headIndex = 0;
    while (resultIndex < result.count || headIndex < ourCommit->fileCount) {  /* the result vs your head, side by side (both sorted) */
        int order = resultIndex >= result.count ? 1 : headIndex >= ourCommit->fileCount ? -1 : strcmp(result.names[resultIndex], ourCommit->names[headIndex]);
        if (order == 0) {
            if (result.blobs[resultIndex] != ourCommit->blobs[headIndex]) { WorkFile *draftFile = DraftEdit(repo, currentUserId, result.names[resultIndex]); if (draftFile) WorkFileSetBytes(draftFile, repo->blobs[result.blobs[resultIndex]].data, repo->blobs[result.blobs[resultIndex]].length); }
            resultIndex++; headIndex++;
        } else if (order < 0) { WorkFile *draftFile = DraftEdit(repo, currentUserId, result.names[resultIndex]); if (draftFile) WorkFileSetBytes(draftFile, repo->blobs[result.blobs[resultIndex]].data, repo->blobs[result.blobs[resultIndex]].length); resultIndex++; }
        else { WorkFile *draftFile = DraftEdit(repo, currentUserId, ourCommit->names[headIndex]); if (draftFile) draftFile->deleted = true; headIndex++; }
    }
    for (int i = 0; i < clashCount; i++) {
        WorkFile *draftFile = DraftEdit(repo, currentUserId, clashes[i].name);
        if (draftFile) { draftFile->deleted = false; WorkFileSetBytes(draftFile, clashes[i].text, clashes[i].length); }
        free(clashes[i].text);
    }
    free(clashes);
    SnapshotFree(&result);
    draft = GetDraft(repo, currentUserId, false);
    draft->merging = true;
    memcpy(draft->mergeSha, theirCommit->sha, 20);
    snprintf(draft->mergeName, NAME_LEN, "%s", theirName);
    snprintf(doneMessage, (size_t)doneCapacity, "%d file%s clashes - fix the marked parts, then commit to finish the merge", clashCount, clashCount == 1 ? " has" : "s have");
    return NULL;
}

/* Gives up on a merge with clashes: back to how your branch was. */
void AbortMerge(Repo *repo)
{
    DraftClear(repo, currentUserId);
}
/* The first of your files that still has conflict markers, or NULL. */
const char *FileWithConflictMarkers(Repo *repo)
{
    Draft *draft = GetDraft(repo, currentUserId, false);
    for (int i = 0; draft && i < draft->fileCount; i++)
        if (!draft->files[i].deleted && !draft->files[i].binary && HasConflictMarkers(draft->files[i].text, draft->files[i].length)) return draft->files[i].name;
    return NULL;
}
