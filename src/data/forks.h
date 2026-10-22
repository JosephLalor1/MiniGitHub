/*
 * forks.h - forks
 *
 * Copying someone else's repository with its whole history, how far a fork
 * is ahead of or behind its original, and bringing in the original's new
 * commits ("Sync fork").
 */

#ifndef MINIGITHUB_FORKS_H
#define MINIGITHUB_FORKS_H

#include "common.h"
#include "model.h"

/* The index of the repository owner has called name (ignoring capitals), or -1. */
int FindRepoByOwner(int owner, const char *name);

/* The original a fork came from (its index), or -1 if it isn't a fork or the original is gone. */
int UpstreamIndex(Repo *repo);

/* How many repositories are forks of this one. */
int ForkCount(Repo *repo);

/* userId's fork of repos[originalIndex], or -1 */
int FindForkOf(int userId, int originalIndex);

typedef struct {
    int  upstream;    /* index of the original, -1 = gone                              */
    bool related;     /* the two still share history                                   */
    int  baseCommit;  /* the original's newest commit this fork also has (-1 = none)   */
    int  behind;      /* the original's commits this fork doesn't have yet             */
    int  ahead;       /* this fork's commits the original doesn't have                 */
} ForkState;

/* How a fork compares with its original: whether it still exists and shares history, and how
   many commits each has that the other doesn't. */
ForkState GetForkState(Repo *repo);

/* Makes the signed-in user's fork of a repository and opens it (or opens the fork they already
   have). */
void TryForkRepo(int originalIndex);

/* A file's contents in a commit (exists = false: the file isn't there). */
typedef struct { bool exists; const char *data; int length; int mode; } FileVersion;

/* A file's contents and mode in a commit (commit may be NULL). */
FileVersion FileVersionIn(Repo *repo, Commit *commit, const char *name);

/* Are two versions of a file the same (both missing counts as the same)? */
bool SameVersion(FileVersion a, FileVersion b);

/* Brings the original's new commits into the fork repo. Returns NULL on success (and a message for
   the user in *doneMessage), or an error. */
const char *SyncFork(Repo *repo, char *doneMessage, int doneCapacity);

#endif
