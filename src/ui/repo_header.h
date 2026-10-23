/*
 * repo_header.h - the top of a repository page
 *
 * Name, visibility, the Star / Fork / Copy repo address buttons and the
 * tabs; the address menu and the branch menu; the fork bar and the merge
 * banner.
 */

#ifndef MINIGITHUB_REPO_HEADER_H
#define MINIGITHUB_REPO_HEADER_H

#include "common.h"
#include "model.h"

/* The strip at the top of a fork's Code tab: where it came from, ahead / behind, and Sync. */
float DrawForkBar(Repo *repo, float top);
extern bool codeMenuOpen;  /* the menu under the "Copy repo address" arrow is open */

/* The menu is drawn after everything else on the page so it sits on top, but it takes its clicks
   first (before the page under it can). */
void CodeMenu(Repo *repo, bool draw);
extern bool branchMenuOpen;  /* open? */

/* How far ahead / behind the default branch each branch is (worked out again only when commits change). */
typedef struct { const Repo *repo; unsigned generation; int branchCount; int ahead[MAX_BRANCHES], behind[MAX_BRANCHES]; } BranchStatsCache;

extern BranchStatsCache branchStats;

/* Works out how far each branch is ahead of and behind the default branch (only when the commits
   have changed). */
void UpdateBranchStats(Repo *repo);

/* Moves onto another branch. */
void SwitchBranch(Repo *repo, int branchIndex);

/* The button showing the branch you're on; clicking opens the menu. */
void BranchButton(Repo *repo, Rectangle rect);

/* Like the Code menu: called with draw = false early in the frame (it takes its clicks before the
   page under it), and with draw = true at the end. */
void BranchMenu(Repo *repo, bool draw);

/* While a merge with clashes is being finished: what's left to do, and a way out. */
float DrawMergeBanner(Repo *repo, Rectangle bar);

/* Returns true if the repository was deleted or we left it (so stop drawing it). */
bool DrawRepoHeader(Repo *repo, int activeTab);

#endif
