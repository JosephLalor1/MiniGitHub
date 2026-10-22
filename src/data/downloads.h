/*
 * downloads.h - saving files to the Downloads folder
 *
 * One file as it is, a whole repository as a .zip, or the files of one
 * commit.
 */

#ifndef MINIGITHUB_DOWNLOADS_H
#define MINIGITHUB_DOWNLOADS_H

#include "common.h"
#include "model.h"

/* Saves one file's bytes, exactly as they are. */
void DownloadFile(const char *name, const char *data, int length);

/* The whole repository as you see it: the latest commit, plus your own uncommitted changes if you
   can edit it. */
void DownloadRepo(Repo *repo);

/* The files exactly as they were in one commit. */
void DownloadCommit(Repo *repo, int commitIndex);

#endif
