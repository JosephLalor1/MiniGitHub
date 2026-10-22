/*
 * diff.c - which lines changed between two versions of a file
 *
 * A full diff (longest common subsequence) for showing changes, a quick
 * Myers count of lines added and removed, and the list of files a commit
 * changed.
 */

#include "diff.h"
#include "util.h"
#include "model.h"
#include "repo.h"

/* ------------------------------------------------------------------ */
/* Diff: which lines were added, removed or kept                       */
/* (longest common subsequence of lines)                               */
/* ------------------------------------------------------------------ */

/* These grow as needed, so files of any length can be compared. */
LineRef *diffOldLines, *diffNewLines;
DiffOp  *diffOps;
int      diffOldCapacity, diffNewCapacity, diffOpsCapacity, diffOpCount;

/* Cuts text into lines (without the newline characters). Returns how many. */
int SplitLines(const char *text, LineRef **lines, int *capacity)
{
    int count = 0;
    const char *lineStart = text;
    while (*lineStart) {
        *lines = GrowArray(*lines, capacity, count + 1, sizeof(LineRef));
        if (count >= *capacity) break;
        const char *lineEnd = strchr(lineStart, '\n');
        (*lines)[count].start = lineStart;
        (*lines)[count].length = lineEnd ? (int)(lineEnd - lineStart) : (int)strlen(lineStart);
        count++;
        if (!lineEnd) break;
        lineStart = lineEnd + 1;
    }
    return count;
}
/* Are two lines exactly the same? */
bool SameLine(LineRef first, LineRef second) { return first.length == second.length && memcmp(first.start, second.start, (size_t)first.length) == 0; }

/* Works out the smallest set of added (+) and removed (-) lines that turns
   oldText into newText, using the "longest common subsequence" method: find the
   longest list of lines both versions share in order; everything else was
   added or removed.

   Lines that are the same at the very start and very end are matched first,
   so a small change in a big file is still quick. If the part in the middle
   is still huge, it's simply shown as all removed then all added. */
/* Works out every line added, removed or kept (into diffOps) and how many were added and removed. */
void ComputeDiff(const char *oldText, const char *newText, int *adds, int *dels)
{
    int oldCount = SplitLines(oldText, &diffOldLines, &diffOldCapacity), newCount = SplitLines(newText, &diffNewLines, &diffNewCapacity);
    int addCount = 0, delCount = 0;
    diffOps = GrowArray(diffOps, &diffOpsCapacity, oldCount + newCount + 1, sizeof(DiffOp));
    diffOpCount = 0;
    if (diffOpsCapacity < oldCount + newCount + 1) { if (adds) *adds = 0; if (dels) *dels = 0; return; }

    int prefix = 0, suffix = 0;
    while (prefix < oldCount && prefix < newCount && SameLine(diffOldLines[prefix], diffNewLines[prefix])) prefix++;
    while (suffix < oldCount - prefix && suffix < newCount - prefix && SameLine(diffOldLines[oldCount - 1 - suffix], diffNewLines[newCount - 1 - suffix])) suffix++;
    for (int k = 0; k < prefix; k++) diffOps[diffOpCount++] = (DiffOp){' ', k, k};

    /* the middle part: rows i = prefix .. oldCount-suffix-1 of the old, j = prefix .. newCount-suffix-1 of the new */
    int oldMiddle = oldCount - prefix - suffix, newMiddle = newCount - prefix - suffix, i = 0, j = 0;
    int *lcs = NULL;
    if (oldMiddle > 0 && newMiddle > 0 && (long long)(oldMiddle + 1) * (newMiddle + 1) <= 6000000LL)
        lcs = calloc((size_t)(oldMiddle + 1) * (size_t)(newMiddle + 1), sizeof(int));
    if (lcs) {
        int rowWidth = newMiddle + 1;
        for (int x = oldMiddle - 1; x >= 0; x--)
            for (int y = newMiddle - 1; y >= 0; y--) {
                if (SameLine(diffOldLines[prefix + x], diffNewLines[prefix + y])) lcs[x * rowWidth + y] = lcs[(x + 1) * rowWidth + y + 1] + 1;
                else {
                    int skipOld = lcs[(x + 1) * rowWidth + y], skipNew = lcs[x * rowWidth + y + 1];
                    lcs[x * rowWidth + y] = skipOld > skipNew ? skipOld : skipNew;
                }
            }
        while (i < oldMiddle && j < newMiddle) {
            if (SameLine(diffOldLines[prefix + i], diffNewLines[prefix + j]))          { diffOps[diffOpCount++] = (DiffOp){' ', prefix + i, prefix + j}; i++; j++; }
            else if (lcs[(i + 1) * rowWidth + j] >= lcs[i * rowWidth + j + 1]) { diffOps[diffOpCount++] = (DiffOp){'-', prefix + i, -1}; i++; delCount++; }
            else                                             { diffOps[diffOpCount++] = (DiffOp){'+', -1, prefix + j}; j++; addCount++; }
        }
        free(lcs);
    }
    while (i < oldMiddle) { diffOps[diffOpCount++] = (DiffOp){'-', prefix + i, -1}; i++; delCount++; }
    while (j < newMiddle) { diffOps[diffOpCount++] = (DiffOp){'+', -1, prefix + j}; j++; addCount++; }
    for (int k = 0; k < suffix; k++) diffOps[diffOpCount++] = (DiffOp){' ', oldCount - suffix + k, newCount - suffix + k};
    if (adds) *adds = addCount;
    if (dels) *dels = delCount;
}

Change commitChanges[MAX_FILES * 2];
int    commitChangeCount, totalAdds, totalDels;

/* How many lines were added and removed, without listing them - for counting.
   Uses the Myers method (what git uses): it tries 0 changes, then 1, 2, ...
   following the matching lines diagonally, so a small change in a big file
   is found almost at once. Very different files fall back to ComputeDiff. */
static LineRef *countOldLines, *countNewLines;
static int      countOldCapacity, countNewCapacity, *countFurthest, countFurthestCapacity;
/* Counts the lines added and removed between two texts, without listing them (quicker than
   ComputeDiff). */
void DiffCounts(const char *oldText, const char *newText, int *adds, int *dels)
{
    int oldCount = SplitLines(oldText, &countOldLines, &countOldCapacity), newCount = SplitLines(newText, &countNewLines, &countNewCapacity);
    int prefix = 0, suffix = 0;
    while (prefix < oldCount && prefix < newCount && SameLine(countOldLines[prefix], countNewLines[prefix])) prefix++;
    while (suffix < oldCount - prefix && suffix < newCount - prefix && SameLine(countOldLines[oldCount - 1 - suffix], countNewLines[newCount - 1 - suffix])) suffix++;
    LineRef *oldMiddle = countOldLines + prefix, *newMiddle = countNewLines + prefix;
    int oldMiddleCount = oldCount - prefix - suffix, newMiddleCount = newCount - prefix - suffix;
    if (!oldMiddleCount || !newMiddleCount) { *adds = newMiddleCount; *dels = oldMiddleCount; return; }
    int maxEdits = oldMiddleCount + newMiddleCount < 4000 ? oldMiddleCount + newMiddleCount : 4000;
    countFurthest = GrowArray(countFurthest, &countFurthestCapacity, 2 * maxEdits + 3, sizeof(int));
    if (countFurthestCapacity < 2 * maxEdits + 3) { ComputeDiff(oldText, newText, adds, dels); return; }
    int *furthest = countFurthest + maxEdits + 1;
    furthest[1] = 0;
    for (int edits = 0; edits <= maxEdits; edits++)
        for (int diagonal = -edits; diagonal <= edits; diagonal += 2) {
            int x = (diagonal == -edits || (diagonal != edits && furthest[diagonal - 1] < furthest[diagonal + 1])) ? furthest[diagonal + 1] : furthest[diagonal - 1] + 1, y = x - diagonal;
            while (x < oldMiddleCount && y < newMiddleCount && SameLine(oldMiddle[x], newMiddle[y])) { x++; y++; }
            furthest[diagonal] = x;
            if (x >= oldMiddleCount && y >= newMiddleCount) { *dels = (edits + oldMiddleCount - newMiddleCount) / 2; *adds = (edits - oldMiddleCount + newMiddleCount) / 2; return; }
        }
    ComputeDiff(oldText, newText, adds, dels);            /* hugely different: the slow, careful way */
}

/* Calls callback for every file that differs between two commits (before may be NULL: the first
   commit). */
void WalkChanges(Commit *before, Commit *after, ChangeCallback callback, void *context)
{
    int i = 0, j = 0, beforeCount = before ? before->fileCount : 0, afterCount = after->fileCount;
    while (i < beforeCount || j < afterCount) {
        int order = i >= beforeCount ? 1 : j >= afterCount ? -1 : before->names[i] == after->names[j] ? 0 : strcmp(before->names[i], after->names[j]);
        if (order == 0) { if (before->blobs[i] != after->blobs[j] || before->modes[i] != after->modes[j]) callback(context, after->names[j], before->blobs[i], after->blobs[j]); i++; j++; }
        else if (order < 0) { callback(context, before->names[i], before->blobs[i], -1); i++; }
        else            { callback(context, after->names[j], -1, after->blobs[j]); j++; }
    }
}

static void AddChange(void *context, const char *name, int oldBlob, int newBlob)
{
    (void)context;
    if (commitChangeCount >= MAX_FILES * 2) return;
    Change *change = &commitChanges[commitChangeCount++];
    memset(change, 0, sizeof *change);
    change->name = name; change->oldBlob = oldBlob; change->newBlob = newBlob;
    change->kind = oldBlob < 0 ? 'A' : newBlob < 0 ? 'D' : 'M';
}

/* The list of files a commit changed, compared with the commit it was built on. */
void BuildChanges(Repo *repo, int commitIndex)
{
    commitChangeCount = totalAdds = totalDels = 0;
    WalkChanges(ParentOf(repo, commitIndex), &repo->commits[commitIndex], AddChange, NULL);
    for (int index = 0; index < commitChangeCount; index++) {
        Change *change = &commitChanges[index];
        change->binary = BlobIsBinary(repo, change->oldBlob) || BlobIsBinary(repo, change->newBlob);
        if (change->binary) { change->adds = change->dels = 0; continue; }  /* no lines to compare */
        if (change->oldBlob == change->newBlob) continue;                   /* only the mode changed */
        ComputeDiff(BlobText(repo, change->oldBlob), BlobText(repo, change->newBlob), &change->adds, &change->dels);
        totalAdds += commitChanges[index].adds;
        totalDels += commitChanges[index].dels;
    }
}
