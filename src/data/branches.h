/*
 * branches.h - branches
 *
 * Walking a commit's history, finding where two lines of work split,
 * counting commits ahead, and creating and deleting branches.
 */

#ifndef MINIGITHUB_BRANCHES_H
#define MINIGITHUB_BRANCHES_H

#include "common.h"
#include "model.h"

/* Marks commit tip and every commit it was built on with bit. */
void MarkHistory(Repo *repo, int tip, uint8_t *mark, uint8_t bit);

/* Is commit ancestor part of descendant's history (or descendant itself)? */
bool IsAncestor(Repo *repo, int ancestor, int descendant);

/* The newest commit both a and b are built on: where the two lines of work split. */
int MergeBase(Repo *repo, int a, int b);

/* How many commits a has that b doesn't (b may be -1). */
int CountAhead(Repo *repo, int a, int b);

/* Git's rules for a branch name, kept simple: letters, digits and - _ . / (so "feature/login"
   works), no "..", no "//", not starting or ending with / or . */
const char *BranchNameProblem(const char *name);

/* A new branch starting at the commit you're on; you move onto it. */
const char *CreateBranch(Repo *repo, const char *name);

/* Deletes a branch (never the default one). Its commits stay if another branch leads to them. */
const char *DeleteBranch(Repo *repo, int branchIndex);

#endif
