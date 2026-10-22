/*
 * nav_history.c - the Back button
 *
 * Pages you visit are remembered like a web browser's history; Back skips
 * any that no longer exist.
 */

#include "nav_history.h"
#include "app_state.h"
#include "contributions.h"
#include "forks.h"
#include "model.h"
#include "navigation.h"
#include "repo.h"
#include "repo_header.h"
#include "users.h"

/* ------------------------------------------------------------------ */
/* Going back                                                         */
/*                                                                    */
/* Every page you visit is remembered (like a web browser's history), */
/* so Back returns to the one before. Pages that only make sense once */
/* - "New repository", "Upload files", the recovery code, and the     */
/* sign-in pages - are never remembered, so Back can't return to a    */
/* form whose job is done. And before going back to a page, it's      */
/* checked that it still exists (the repository wasn't deleted, the   */
/* commit is still there...); if not, Back goes further back. With    */
/* nowhere left to go, Back signs you out: every chain ends at the    */
/* sign-in page.                                                      */
/* ------------------------------------------------------------------ */

Page navHistory[MAX_HISTORY];
static int  navHistoryCount;
static Page navCurrentPage;                          /* the page shown last frame */
static bool navHasCurrentPage, navGoingBack;
static int  navUserId = -1;                          /* whose history it is */

static bool IsRememberedScreen(Screen which)
{
    return which == SCREEN_PROFILE || which == SCREEN_REPO || which == SCREEN_COMMIT || which == SCREEN_DAY || which == SCREEN_SETTINGS || which == SCREEN_PEOPLE;
}

static Page CurrentPage(void)
{
    Page page;
    memset(&page, 0, sizeof page);
    page.screen = screen;
    page.profileUserId = -1; page.dayUserId = -1; page.repoOwner = -1;
    if ((screen == SCREEN_REPO || screen == SCREEN_COMMIT || screen == SCREEN_UPLOAD) && currentRepo >= 0 && currentRepo < repoCount) {
        page.repoOwner = repos[currentRepo].owner;
        snprintf(page.repoName, NAME_LEN, "%s", repos[currentRepo].name);
        page.repoTab = repoTab;
        if (screen == SCREEN_COMMIT && currentCommit >= 0 && currentCommit < repos[currentRepo].commitCount) {
            memcpy(page.commit, repos[currentRepo].commits[currentCommit].sha, 20);
            page.openedFromDay = openedFromDay;
            page.dayUserId = dayUserId; page.day = currentDay;
        }
    }
    if (screen == SCREEN_PROFILE) page.profileUserId = profileUserId;
    if (screen == SCREEN_DAY) { page.dayUserId = dayUserId; page.day = currentDay; }
    if (screen == SCREEN_SETTINGS) page.settingsTab = settingsTab;
    return page;
}

static bool SamePage(const Page *a, const Page *b)
{
    if (a->screen != b->screen) return false;
    switch (a->screen) {
    case SCREEN_PROFILE: return a->profileUserId == b->profileUserId;
    case SCREEN_REPO:    return a->repoOwner == b->repoOwner && strcmp(a->repoName, b->repoName) == 0 && a->repoTab == b->repoTab;
    case SCREEN_COMMIT:  return a->repoOwner == b->repoOwner && strcmp(a->repoName, b->repoName) == 0 && memcmp(a->commit, b->commit, 20) == 0;
    case SCREEN_UPLOAD:  return a->repoOwner == b->repoOwner && strcmp(a->repoName, b->repoName) == 0;
    case SCREEN_DAY:     return a->dayUserId == b->dayUserId && a->day == b->day;
    case SCREEN_SETTINGS: return a->settingsTab == b->settingsTab;
    default:          return true;
    }
}

static void NavClearHistory(void) { navHistoryCount = 0; navHasCurrentPage = false; }

/* Called once a frame, after the page has been drawn: if you've moved to a
   different page, the one you left goes on the history (if it's one to keep). */
void NavTrack(void)
{
    if (currentUserId != navUserId) { NavClearHistory(); navUserId = currentUserId; }  /* signed in or out: a fresh history */
    Page now = CurrentPage();
    if (!navHasCurrentPage) { navCurrentPage = now; navHasCurrentPage = true; navGoingBack = false; return; }
    if (SamePage(&now, &navCurrentPage)) return;
    if (!navGoingBack && IsRememberedScreen(navCurrentPage.screen) && currentUserId >= 0 &&
        !(navHistoryCount && SamePage(&navHistory[navHistoryCount - 1], &navCurrentPage))) {
        if (navHistoryCount == MAX_HISTORY) { memmove(navHistory, navHistory + 1, sizeof(Page) * (MAX_HISTORY - 1)); navHistoryCount--; }
        navHistory[navHistoryCount++] = navCurrentPage;
    }
    navCurrentPage = now;
    navGoingBack = false;
}

/* Is this remembered page still there? (index of its repository in *repoIndex, commit in *commitIndex) */
static bool PageStillThere(const Page *page, int *repoIndex, int *commitIndex)
{
    *repoIndex = *commitIndex = -1;
    if (currentUserId < 0) return false;
    if (page->screen == SCREEN_REPO || page->screen == SCREEN_COMMIT) {
        *repoIndex = FindRepoByOwner(page->repoOwner, page->repoName);
        if (*repoIndex < 0) return false;                               /* deleted (or renamed) */
        if (!CanSee(&repos[*repoIndex], currentUserId)) return false;   /* private now, and you're not in it */
        if (page->screen == SCREEN_COMMIT) { *commitIndex = CommitBySha(&repos[*repoIndex], page->commit); if (*commitIndex < 0) return false; }
        return true;
    }
    if (page->screen == SCREEN_PROFILE) return FindUserById(page->profileUserId) != NULL;
    if (page->screen == SCREEN_DAY)     return FindUserById(page->dayUserId) != NULL;
    return page->screen == SCREEN_SETTINGS || page->screen == SCREEN_PEOPLE;
}

/* Where Back would go: the newest remembered page that's still there (-1 = none). */
int NavTarget(void)
{
    Page now = CurrentPage();
    for (int i = navHistoryCount - 1; i >= 0; i--) {
        int repoIndex, commitIndex;
        if (SamePage(&navHistory[i], &now)) continue;                   /* that's where you are */
        if (PageStillThere(&navHistory[i], &repoIndex, &commitIndex)) return i;
    }
    return -1;
}

/* A few words for the tooltip: "jo / hello-world (Commits)". */
const char *PageLabel(const Page *page)
{
    static const char *tabNames[4] = {"Code", "Commits", "Issues", "People"};
    switch (page->screen) {
    case SCREEN_PROFILE: return page->profileUserId == currentUserId ? "your profile" : TextFormat("%s's profile", UserName(page->profileUserId));
    case SCREEN_REPO:    return TextFormat("%s / %s (%s)", UserName(page->repoOwner), page->repoName, tabNames[page->repoTab]);
    case SCREEN_COMMIT:  return TextFormat("a commit in %s / %s", UserName(page->repoOwner), page->repoName);
    case SCREEN_DAY:     return TextFormat("%s's contributions on %s", UserName(page->dayUserId), FormatDay(page->day, false));
    case SCREEN_SETTINGS: return "your settings";
    case SCREEN_PEOPLE:  return "People on this computer";
    default:          return "the previous page";
    }
}

/* Goes back. Returns false if there was nowhere to go. */
bool NavBack(void)
{
    int target = NavTarget();
    if (target < 0) { navHistoryCount = 0; return false; }
    Page page = navHistory[target];
    navHistoryCount = target;                                           /* it and everything after it are used up */
    int repoIndex, commitIndex;
    PageStillThere(&page, &repoIndex, &commitIndex);
    codeMenuOpen = branchMenuOpen = false;
    switch (page.screen) {
    case SCREEN_PROFILE: OpenProfile(page.profileUserId); break;
    case SCREEN_PEOPLE:  OpenPeople(); break;
    case SCREEN_DAY:     OpenDay(page.dayUserId, page.day); break;
    case SCREEN_SETTINGS: if (screen != SCREEN_SETTINGS) OpenSettings(); settingsTab = page.settingsTab; Unfocus(); ClearSecrets(); break;
    case SCREEN_REPO:
        /* the same repository: just change tab (your file, scroll and commit message stay) */
        if (currentRepo == repoIndex && (screen == SCREEN_REPO || screen == SCREEN_COMMIT || screen == SCREEN_UPLOAD)) { Unfocus(); screen = SCREEN_REPO; }
        else OpenRepo(repoIndex);
        repoTab = page.repoTab;
        break;
    case SCREEN_COMMIT:
        if (currentRepo != repoIndex) OpenRepo(repoIndex);
        OpenCommit(commitIndex);
        openedFromDay = page.openedFromDay; dayUserId = page.dayUserId; currentDay = page.day;
        break;
    default: break;
    }
    navGoingBack = true;
    return true;
}
