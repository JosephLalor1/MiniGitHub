/*
 * repo.h - repositories, blobs and commits
 *
 * Creating and freeing repositories, storing each version of a file once
 * (blobs), and finding branches, the latest commit and the files in a
 * commit.
 */

#ifndef MINIGITHUB_REPO_H
#define MINIGITHUB_REPO_H

#include "common.h"
#include "model.h"

/* Adds an empty repository to the list, or returns NULL if the list is full. */
Repo *NewRepo(int owner, const char *name, const char *description);

/* Frees every file in a draft. */
void FreeDraft(Draft *draft);

/* Frees a commit's file lists and git data. */
void FreeCommit(Commit *commit);

/* Frees everything a repository holds (after any save being written in the background has finished). */
void FreeRepo(Repo *repo);

/* Removes a repository from the list (the ones after it move up). */
void DeleteRepo(int index);

/* Store contents once; returns the blob's index (or -1 if out of room). If sha is given it must be
   the contents' git fingerprint (saves working it out again). */
int StoreBlobWithSha(Repo *repo, const char *data, int length, const uint8_t *knownSha);

/* Stores a version of a file once; returns the blob's index (or -1 if there's no room). */
int StoreBlob(Repo *repo, const char *data, int length);

/* A blob's contents ("" for blob -1). */
const char *BlobText(Repo *repo, int blob);

/* A blob's size in bytes (0 for blob -1). */
int         BlobLength(Repo *repo, int blob);

/* Is the blob a non-text file (false for blob -1)? */
bool        BlobIsBinary(Repo *repo, int blob);

/* The branch you're on, and its newest commit ("head"). An empty repository has no branches yet. */
int HeadIndex(Repo *repo);

/* The newest commit on the current branch, or NULL if there are no commits yet. */
Commit *HeadCommit(Repo *repo);

/* The name of the branch being looked at (or the name the first branch will get). */
const char *CurrentBranchName(Repo *repo);

/* The name of the default branch (or the name the first branch will get). */
const char *DefaultBranchName(Repo *repo);

/* The newest commit on the default branch, or -1. */
int DefaultTip(Repo *repo);

/* When anything last happened in the repository: its creation, or its newest commit on any branch. */
long long LastActivity(Repo *repo);

/* The index of the branch called name, or -1. */
int FindBranch(Repo *repo, const char *name);

/* Adds a branch pointing at commit tip; returns its index, or -1 if there are too many. */
int AddBranch(Repo *repo, const char *name, int tip);

/* The commit this one was built on (the first one, for a merge), or NULL. */
Commit *ParentOf(Repo *repo, int commitIndex);

/* The index of the commit with this fingerprint, or -1. */
int CommitBySha(Repo *repo, const uint8_t *sha);

/* Commits keep their files sorted by path, so finding one is quick. */
int FindInCommit(Commit *commit, const char *name);

/* How many of the repository's issues are still open. */
int OpenIssueCount(Repo *repo);

#endif
