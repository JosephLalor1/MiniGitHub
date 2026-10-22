/*
 * git_commit.h - real git commits
 *
 * Working out tree (folder) fingerprints, writing and reading git's commit
 * text, and adding finished commits to a repository.
 */

#ifndef MINIGITHUB_GIT_COMMIT_H
#define MINIGITHUB_GIT_COMMIT_H

#include "common.h"
#include "model.h"
#include "snapshot.h"

typedef void (*TreeEmit)(void *context, const uint8_t sha[20], const uint8_t *data, size_t length);

/* Works out the fingerprint of the folder holding entries first..end-1 (paths all starting with
   the same prefixLength characters, e.g. */
bool BuildTree(const char *const *names, const uint8_t (*shas)[20], const uint8_t *modes, int lo, int hi, int plen, uint8_t out[20], TreeEmit emit, void *ctx);

/* The fingerprint of a commit's top folder; emit, if given, is handed every tree made on the way. */
bool CommitTree(Repo *repo, Commit *commit, uint8_t treeSha[20], TreeEmit emit, void *context);

/* The text git stores for a commit (tree, parents, author, committer, message). Returns a malloc'd
   string; *length gets its length. */
char *MakeRawCommit(const uint8_t treeSha[20], const uint8_t *const *parents, int parentCount, const char *who, const char *email, long long when, const char *message, int *length);

/* The parts of a git commit object Mini GitHub uses. */
#define MAX_PARENTS 16

typedef struct {
    uint8_t   tree[20];
    int       parentCount;
    uint8_t   parents[MAX_PARENTS][20];
    char      authorName[NAME_LEN * 2], authorEmail[TEXT_LEN];
    long long authorTime;
    char      summary[TEXT_LEN];
} CommitInfo;

/* Reads the parts of a git commit Mini GitHub uses into *info. False if it has no tree. */
bool ParseCommit(const char *raw, int length, CommitInfo *info);

/* Who to show as a commit's author: the Mini GitHub user, plus the name git has if that's
   different (e.g. "Jo Lalor (jo)" for a pushed commit). */
const char *CommitAuthorName(Commit *commit);

/* Adds a finished commit to the end of the history (the repository owns its lists from now on). */
bool AppendCommit(Repo *repo, Commit *commit);

/* A file can't have the same name as a folder ("docs" and "docs/guide.md"): git couldn't store
   that. Returns the clashing name, or NULL. */
const char *PathClash(const char *const *names, int count);

/* Turns a snapshot into a new commit by the signed-in user, on top of the latest commit (and, for
   a merge, also on top of commit mergedCommit - or -1). */
bool FinishCommit(Repo *repo, Snapshot *snap, const char *message, int mergedCommit);

#endif
