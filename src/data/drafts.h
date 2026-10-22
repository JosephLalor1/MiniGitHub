/*
 * drafts.h - each person's uncommitted changes
 *
 * Changing files without touching anyone else's work, spotting when
 * someone committed a newer version in the meantime, and turning a draft
 * into a commit.
 */

#ifndef MINIGITHUB_DRAFTS_H
#define MINIGITHUB_DRAFTS_H

#include "common.h"
#include "diff.h"
#include "model.h"

/* The blob a file has in the latest commit, or -1 if it isn't there. */
int HeadBlobOf(Repo *repo, const char *name);

/* Is the file's text exactly the same as this blob's? */
bool SameAsBlob(Repo *repo, WorkFile *file, int blob);

/* The changes userId has made on the branch being worked on (made if create and there are none
   yet). */
Draft *GetDraft(Repo *repo, int userId, bool create);

/* Where a file is in a draft, or -1 (draft may be NULL). */
int DraftIndex(Draft *draft, const char *name);

/* Your draft's copy of a file, or NULL if you haven't changed it. */
WorkFile *DraftFind(Repo *repo, int userId, const char *name);

/* Forgets your change to one file (it shows the latest commit's version again). */
void DraftDropName(Repo *repo, int userId, const char *name);

/* Forgets all your changes on the current branch, and any merge in progress. */
void DraftClear(Repo *repo, int userId);

/* Throws away drafts: userId's (on every branch) if branch is NULL, or everyone's on that branch
   if userId is -1. */
void RemoveDrafts(Repo *repo, int userId, const char *branch);

/* Throws away all of userId's drafts, on every branch. */
void RemoveDraft(Repo *repo, int userId);

/* Your own editable copy of a file: made from the latest commit the first time you change it (or
   empty, for a new file). NULL if your draft is full. */
WorkFile *DraftEdit(Repo *repo, int userId, const char *name);

/* Sets the text of a file in your draft (adding the file if it's new). */
void DraftSetText(Repo *repo, int userId, const char *name, const char *text);

/* What a file in your draft means: 'A' a new file, 'M' modified, 'D' deleted, 0 no real change,
   'C' a conflict - someone committed a different version of this file after you started changing
   it, so committing yours would wipe theirs. */
char DraftStatus(Repo *repo, WorkFile *draftFile);

/* Throws away draft files that aren't real changes, so they quietly follow the latest commit
   again. */
void DraftTidy(Repo *repo, int userId);

/* How many of your files are real changes, and how many are conflicts. */
void DraftCounts(Repo *repo, int userId, int *changedFiles, int *conflicts);

/* Who most recently committed a change to this file (or -1): follows the latest commit back
   through the commits it was built on. */
int LastChangedBy(Repo *repo, const char *name);

/* Returns NULL on success, or a message explaining what went wrong. */
const char *DoCommit(Repo *repo, const char *message);

/* Replace the working files with the files from an old commit. */
void RestoreCommit(Repo *repo, int commitIndex);

#endif
