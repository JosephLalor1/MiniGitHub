/*
 * repo_view.h - what someone sees in a repository
 *
 * The latest commit with their own draft laid on top, as one sorted list
 * of files.
 */

#ifndef MINIGITHUB_REPO_VIEW_H
#define MINIGITHUB_REPO_VIEW_H

#include "common.h"
#include "model.h"

/* What you see in a repository: the latest commit with your own draft laid
   on top. With userId = -1 it's just the latest commit (for read-only visitors). */
typedef struct { const char *name; const char *data; int length; bool binary; char status; WorkFile *draftFile; int blob; } ViewFile;

extern ViewFile *viewFiles;
extern int viewFileCount, viewFileCapacity;

/* Fills viewFiles with what userId sees: the latest commit plus their draft (-1: just the latest
   commit). */
void BuildView(Repo *repo, int userId);

/* Where a file is in viewFiles, or -1. */
int ViewIndex(const char *name);

/* Has the viewer deleted this file in their draft? */
bool ViewDeleted(ViewFile *viewFile);

#endif
