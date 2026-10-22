/*
 * contributions.h - contributions by day
 *
 * A contribution is a commit you made or an issue you opened. This gathers
 * them by day for the six-month calendar on profiles and its pop-up.
 */

#ifndef MINIGITHUB_CONTRIBUTIONS_H
#define MINIGITHUB_CONTRIBUTIONS_H

#include "common.h"
#include "model.h"

/* Midnight (local time) at the start of the day that when falls on. */
long long DayStart(long long when);

/* Midnight that many days later (days may be negative). Safe across clock changes. */
long long AddDays(long long dayStart, int days);

/* "Tuesday 13 October 2026" (long) or "Tue 13 Oct 2026" (short). */
const char *FormatDay(long long day, bool longForm);

/* "1 commit", "3 commits", "2 repositories"... written into the caller's buffer. */
const char *CountText(char *buf, int count, const char *one, const char *many);

/* "hello-world" for your own repos, "alice/hello-world" for anyone else's. */
const char *RepoLabel(int repoIndex);

/* Works out (once) how many files a commit changed and how many lines it added and removed. */
void CommitStats(Repo *repo, int commitIndex);

/* The files a commit changed and how ('A' added, 'M' modified, 'D' deleted). Returns how many. */
int CommitFileList(Repo *repo, int commitIndex, char *kinds, const char **names, int capacity);

/* Everything one user did on one day. */
typedef struct { char kind; int repo, index; long long time; } Activity;  /* kind: 'C' commit, 'I' issue */

typedef struct { int repo, commits, issues, adds, dels; } RepoTally;
#define MAX_ACTIVITIES 1024
extern Activity activities[MAX_ACTIVITIES];
extern int activityCount, activityTotal;
extern RepoTally tallies[MAX_REPOS];
extern int tallyCount, dayCommits, dayIssues, dayAdds, dayDels, dayFiles;

/* Collects everything userId did on the day starting at start (into activities and tallies). */
void GatherDay(int userId, long long start);

/* "3 commits, 1 issue" */
const char *TallyText(int commits, int issues);

/* Draws "+12 -4" in green and red. Returns the width used. */
int DrawAddsDels(int adds, int dels, int x, int y);

/* The little pop-up shown when the mouse is over a day square. */
void DrawDayTooltip(int userId, long long day);

/* Squares for the last 26 weeks, coloured by how many contributions userId made that day. Hovering
   a square sets *hoverDay; clicking one opens that day's page. */
void DrawContributions(Rectangle area, int userId, long long *hoverDay, bool narrow);

#endif
