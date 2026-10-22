/*
 * diff.h - which lines changed between two versions of a file
 *
 * A full diff (longest common subsequence) for showing changes, a quick
 * Myers count of lines added and removed, and the list of files a commit
 * changed.
 */

#ifndef MINIGITHUB_DIFF_H
#define MINIGITHUB_DIFF_H

#include "common.h"
#include "model.h"

typedef struct { const char *start; int length; } LineRef;
typedef struct { char kind; int oldLine, newLine; } DiffOp;  /* kind: ' ', '+', '-' */
extern LineRef *diffOldLines, *diffNewLines;
extern DiffOp *diffOps;
extern int diffOldCapacity, diffNewCapacity, diffOpsCapacity, diffOpCount;

/* Cuts text into lines (without the newline characters). Returns how many. */
int SplitLines(const char *text, LineRef **lines, int *capacity);

/* Are two lines exactly the same? */
bool SameLine(LineRef first, LineRef second);

/* Works out every line added, removed or kept (into diffOps) and how many were added and removed. */
void ComputeDiff(const char *oldText, const char *newText, int *adds, int *dels);

/* The list of files a commit changed, compared with the commit before it. */
typedef struct { const char *name; int oldBlob, newBlob; char kind; int adds, dels; bool binary; } Change;

extern Change commitChanges[MAX_FILES * 2];
extern int commitChangeCount, totalAdds, totalDels;

/* Counts the lines added and removed between two texts, without listing them (quicker than
   ComputeDiff). */
void DiffCounts(const char *oldText, const char *newText, int *adds, int *dels);

/* Walks two commits' file lists side by side (both are sorted by path) and
   calls back for each file that differs: old or new blob is -1 if it isn't there. */
typedef void (*ChangeCallback)(void *context, const char *name, int oldBlob, int newBlob);

/* Calls callback for every file that differs between two commits (before may be NULL: the first
   commit). */
void WalkChanges(Commit *before, Commit *after, ChangeCallback callback, void *context);

/* The list of files a commit changed, compared with the commit it was built on. */
void BuildChanges(Repo *repo, int commitIndex);

#endif
