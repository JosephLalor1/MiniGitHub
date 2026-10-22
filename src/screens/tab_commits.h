/*
 * tab_commits.h - the Commits tab
 *
 * The history as a list, or as a graph of branches and merges.
 */

#ifndef MINIGITHUB_TAB_COMMITS_H
#define MINIGITHUB_TAB_COMMITS_H

#include "common.h"
#include "model.h"

/* Draws a repository's Commits tab. */
void TabCommits(Repo *repo);

#endif
