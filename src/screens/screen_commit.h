/*
 * screen_commit.h - one commit
 *
 * Its details, the files it changed, and each file's changes line by line.
 */

#ifndef MINIGITHUB_SCREEN_COMMIT_H
#define MINIGITHUB_SCREEN_COMMIT_H

#include "common.h"
#include "model.h"

/* Draws the commit page for the commit open in repo. */
void ScreenCommit(Repo *repo);

#endif
