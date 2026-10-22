/*
 * nav_history.h - the Back button
 *
 * Pages you visit are remembered like a web browser's history; Back skips
 * any that no longer exist.
 */

#ifndef MINIGITHUB_NAV_HISTORY_H
#define MINIGITHUB_NAV_HISTORY_H

#include "common.h"
#include "app_state.h"

typedef struct {
    Screen    screen;
    int       repoOwner; char repoName[NAME_LEN];   /* a repository by owner + name: positions in the list change */
    Tab       repoTab;
    uint8_t   commit[20];                            /* a commit by its fingerprint */
    int       profileUserId, dayUserId;
    long long day;
    int       settingsTab;
    bool      openedFromDay;
} Page;

#define MAX_HISTORY 100
extern Page navHistory[MAX_HISTORY];

/* Called once a frame, after the page has been drawn: if you've moved to a different page, the one
   you left goes on the history (if it's one to keep). */
void NavTrack(void);

/* Where Back would go: the newest remembered page that's still there (-1 = none). */
int NavTarget(void);

/* A few words for the tooltip: "jo / hello-world (Commits)". */
const char *PageLabel(const Page *page);

/* Goes back. Returns false if there was nowhere to go. */
bool NavBack(void);

#endif
