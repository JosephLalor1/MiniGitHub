/*
 * merge.h - merging
 *
 * A three-way, line-by-line merge of text files, and merging one branch
 * into another: at once when nothing clashes, otherwise through your draft
 * with the clashes marked for you to fix.
 */

#ifndef MINIGITHUB_MERGE_H
#define MINIGITHUB_MERGE_H

#include "common.h"
#include "model.h"

/* Does this text still have a conflict marker at the start of a line? */
bool HasConflictMarkers(const char *text, int length);

/* Merges branch fromBranch into the branch you're on. */
const char *MergeBranch(Repo *repo, int fromBranch, char *doneMessage, int doneCapacity);

/* Gives up on a merge with clashes: back to how your branch was. */
void AbortMerge(Repo *repo);

/* The first of your files that still has conflict markers, or NULL. */
const char *FileWithConflictMarkers(Repo *repo);

#endif
