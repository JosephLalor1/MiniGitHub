/*
 * contributions.c - contributions by day
 *
 * A contribution is a commit you made or an issue you opened. This gathers
 * them by day for the six-month calendar on profiles and its pop-up.
 */

#include "contributions.h"
#include "app_state.h"
#include "diff.h"
#include "model.h"
#include "navigation.h"
#include "repo.h"
#include "ui_core.h"
#include "users.h"

/* ------------------------------------------------------------------ */
/* Days and contributions                                             */
/*                                                                    */
/* A "contribution" is a commit you made or an issue you opened.      */
/* Days run from local midnight to local midnight.                    */
/* ------------------------------------------------------------------ */

/* Midnight (local time) at the start of the day that when falls on. */
long long DayStart(long long when)
{
    time_t whenTime = (time_t)when;
    struct tm local = *localtime(&whenTime);
    local.tm_hour = local.tm_min = local.tm_sec = 0;
    local.tm_isdst = -1;
    return (long long)mktime(&local);
}

/* Midnight that many days later (days may be negative). Safe across clock changes. */
long long AddDays(long long dayStart, int days)
{
    time_t whenTime = (time_t)dayStart;
    struct tm local = *localtime(&whenTime);
    local.tm_mday += days;
    local.tm_hour = local.tm_min = local.tm_sec = 0;
    local.tm_isdst = -1;
    return (long long)mktime(&local);
}

/* Whole days from midnight from to midnight to (rounded, so a 23- or 25-hour day still counts as one). */
static int DaysBetween(long long from, long long to)
{
    long long seconds = to - from;
    return (int)((seconds >= 0 ? seconds + 43200 : seconds - 43200) / 86400);
}

/* "Tuesday 13 October 2026" (long) or "Tue 13 Oct 2026" (short). */
const char *FormatDay(long long day, bool longForm)
{
    static char formatted[64];
    time_t dayTime = (time_t)day;
    struct tm local = *localtime(&dayTime);
    char weekday[16], month[16];
    strftime(weekday,  sizeof weekday,  longForm ? "%A" : "%a", &local);
    strftime(month, sizeof month, longForm ? "%B" : "%b", &local);
    snprintf(formatted, sizeof formatted, "%s %d %s %d", weekday, local.tm_mday, month, local.tm_year + 1900);
    return formatted;
}

/* "1 commit", "3 commits", "2 repositories"... written into the caller's buffer. */
const char *CountText(char *buf, int count, const char *one, const char *many)
{
    snprintf(buf, 48, "%d %s", count, count == 1 ? one : many);
    return buf;
}

/* "hello-world" for your own repos, "alice/hello-world" for anyone else's. */
const char *RepoLabel(int repoIndex)
{
    static char labels[2][NAME_LEN * 2 + 4];
    static int which;
    which ^= 1;
    Repo *repo = &repos[repoIndex];
    if (IsMine(repo)) snprintf(labels[which], sizeof labels[which], "%s", repo->name);
    else           snprintf(labels[which], sizeof labels[which], "%s/%s", UserName(repo->owner), repo->name);
    return labels[which];
}

/* How many files a commit touched and how many lines it added / removed.
   Worked out the first time it's needed, then remembered. */
typedef struct { Repo *repo; int files, adds, dels; } StatCounter;
static void CountStatChange(void *context, const char *name, int oldBlob, int newBlob)
{
    (void)name;
    StatCounter *counter = context;
    counter->files++;
    if (oldBlob == newBlob || BlobIsBinary(counter->repo, oldBlob) || BlobIsBinary(counter->repo, newBlob)) return;  /* binary: no lines to count */
    int added, removed;
    DiffCounts(BlobText(counter->repo, oldBlob), BlobText(counter->repo, newBlob), &added, &removed);
    counter->adds += added; counter->dels += removed;
}
/* Works out (once) how many files a commit changed and how many lines it added and removed. */
void CommitStats(Repo *repo, int commitIndex)
{
    Commit *commit = &repo->commits[commitIndex];
    if (commit->statsDone) return;
    StatCounter counter = {repo, 0, 0, 0};
    WalkChanges(ParentOf(repo, commitIndex), commit, CountStatChange, &counter);
    commit->statFiles = counter.files; commit->statAdds = counter.adds; commit->statDels = counter.dels;
    commit->statsDone = true;
}

/* The files a commit changed: kind is 'A' added, 'M' modified, 'D' deleted. Cheap - no diffing. */
typedef struct { char *kinds; const char **names; int count, capacity; } FileListBuilder;
static void AddListedChange(void *context, const char *name, int oldBlob, int newBlob)
{
    FileListBuilder *list = context;
    if (list->count >= list->capacity) return;
    list->kinds[list->count] = oldBlob < 0 ? 'A' : newBlob < 0 ? 'D' : 'M';
    list->names[list->count++] = name;
}
/* The files a commit changed and how ('A' added, 'M' modified, 'D' deleted). Returns how many. */
int CommitFileList(Repo *repo, int commitIndex, char *kinds, const char **names, int capacity)
{
    FileListBuilder list = {kinds, names, 0, capacity};
    WalkChanges(ParentOf(repo, commitIndex), &repo->commits[commitIndex], AddListedChange, &list);
    return list.count;
}

Activity  activities[MAX_ACTIVITIES];
int       activityCount, activityTotal;       /* stored / actually happened */
RepoTally tallies[MAX_REPOS];
int       tallyCount, dayCommits, dayIssues, dayAdds, dayDels, dayFiles;

/* Collects everything userId did on the day starting at start (into activities and tallies). */
void GatherDay(int userId, long long start)
{
    long long end = AddDays(start, 1);
    activityCount = activityTotal = tallyCount = 0;
    dayCommits = dayIssues = dayAdds = dayDels = dayFiles = 0;

    for (int i = 0; i < repoCount; i++) {
        Repo *repo = &repos[i];
        if (!CanSee(repo, currentUserId)) continue;       /* private, and you're not in it */
        RepoTally tally = {i, 0, 0, 0, 0};
        for (int k = 0; k < repo->commitCount; k++) {
            Commit *commit = &repo->commits[k];
            if (commit->author != userId || commit->copied || commit->time < start || commit->time >= end) continue;
            CommitStats(repo, k);
            tally.commits++; tally.adds += commit->statAdds; tally.dels += commit->statDels;
            dayFiles += commit->statFiles;
            if (activityCount < MAX_ACTIVITIES) activities[activityCount++] = (Activity){'C', i, k, commit->time};
            activityTotal++;
        }
        for (int k = 0; k < repo->issueCount; k++) {
            Issue *issue = &repo->issues[k];
            if (issue->author != userId || issue->time < start || issue->time >= end) continue;
            tally.issues++;
            if (activityCount < MAX_ACTIVITIES) activities[activityCount++] = (Activity){'I', i, k, issue->time};
            activityTotal++;
        }
        if (tally.commits || tally.issues) {
            tallies[tallyCount++] = tally;
            dayCommits += tally.commits; dayIssues += tally.issues;
            dayAdds += tally.adds;       dayDels += tally.dels;
        }
    }
    /* timeline: earliest first */
    for (int sorted = 1; sorted < activityCount; sorted++) {
        Activity item = activities[sorted];
        int before = sorted - 1;
        while (before >= 0 && activities[before].time > item.time) { activities[before + 1] = activities[before]; before--; }
        activities[before + 1] = item;
    }
    /* repositories: busiest first */
    for (int sorted = 1; sorted < tallyCount; sorted++) {
        RepoTally item = tallies[sorted];
        int before = sorted - 1;
        while (before >= 0 && tallies[before].commits + tallies[before].issues < item.commits + item.issues) { tallies[before + 1] = tallies[before]; before--; }
        tallies[before + 1] = item;
    }
}

/* "3 commits, 1 issue" */
const char *TallyText(int commits, int issues)
{
    static char text[104];
    char commitsText[48], issuesText[48];
    if (commits && issues) snprintf(text, sizeof text, "%s, %s", CountText(commitsText, commits, "commit", "commits"), CountText(issuesText, issues, "issue", "issues"));
    else if (commits)      snprintf(text, sizeof text, "%s", CountText(commitsText, commits, "commit", "commits"));
    else                   snprintf(text, sizeof text, "%s", CountText(issuesText, issues, "issue opened", "issues opened"));
    return text;
}

/* Draws "+12 -4" in green and red. Returns the width used. */
int DrawAddsDels(int adds, int dels, int x, int y)
{
    const char *plusText = TextFormat("+%d", adds);
    int plusWidth = MeasureText(plusText, FONT_SIZE);
    DrawText(plusText, x, y, FONT_SIZE, UI_GREEN_TEXT);
    const char *minusText = TextFormat("-%d", dels);
    DrawText(minusText, x + plusWidth + 10, y, FONT_SIZE, UI_RED_TEXT);
    return plusWidth + 10 + MeasureText(minusText, FONT_SIZE);
}

/* The little pop-up shown when the mouse is over a day square. */
void DrawDayTooltip(int userId, long long day)
{
    GatherDay(userId, day);

    enum { MAX_LINES = 12 };
    char  text[MAX_LINES][160];
    Color colors[MAX_LINES];
    int   addsDels[MAX_LINES][2];  /* -1 = none; otherwise +adds -dels drawn after the text */
    bool  bold[MAX_LINES] = {0}, rule[MAX_LINES] = {0};
    int   linkLen[MAX_LINES] = {0};  /* this many leading characters are a repository name, drawn in blue */
    int   lineCount = 0;
    char  countText1[48], countText2[48];

#define ADD_LINE(color) (addsDels[lineCount][0] = -1, colors[lineCount] = (color), lineCount++)
    snprintf(text[lineCount], 160, "%s", FormatDay(day, false)); bold[lineCount] = true; ADD_LINE(UI_TEXT);
    if (activityTotal == 0) {
        snprintf(text[lineCount], 160, "No contributions"); ADD_LINE(UI_MUTED);
    } else {
        snprintf(text[lineCount], 160, "%s in %s", CountText(countText1, activityTotal, "contribution", "contributions"),
                 CountText(countText2, tallyCount, "repository", "repositories"));
        ADD_LINE(UI_TEXT);
        if (dayCommits) {
            snprintf(text[lineCount], 160, "%s, %s changed   ", CountText(countText1, dayCommits, "commit", "commits"),
                     CountText(countText2, dayFiles, "file", "files"));
            addsDels[lineCount][0] = dayAdds; addsDels[lineCount][1] = dayDels; colors[lineCount] = UI_MUTED; lineCount++;
        }
        if (dayIssues) { snprintf(text[lineCount], 160, "%s", CountText(countText1, dayIssues, "issue opened", "issues opened")); ADD_LINE(UI_MUTED); }

        rule[lineCount] = true;
        int shown = tallyCount < 5 ? tallyCount : 5;
        for (int i = 0; i < shown; i++) {
            RepoTally *tally = &tallies[i];
            const char *label = RepoLabel(tally->repo);
            snprintf(text[lineCount], 160, "%s:  %s   ", label, TallyText(tally->commits, tally->issues));
            linkLen[lineCount] = (int)strlen(label);
            colors[lineCount] = UI_MUTED;
            addsDels[lineCount][0] = tally->commits ? tally->adds : -1;
            addsDels[lineCount][1] = tally->dels;
            lineCount++;
        }
        if (tallyCount > shown) {
            snprintf(text[lineCount], 160, "...and %s", CountText(countText1, tallyCount - shown, "more repository", "more repositories"));
            ADD_LINE(UI_MUTED);
        }
    }
    rule[lineCount] = true;
    snprintf(text[lineCount], 160, "Click to see the whole day"); ADD_LINE(UI_MUTED);
#undef ADD_LINE

    /* size and place it next to the mouse, kept on screen */
    int width = 0;
    for (int i = 0; i < lineCount; i++) {
        int lineWidth = MeasureText(text[i], FONT_SIZE);
        if (addsDels[i][0] >= 0) lineWidth += MeasureText(TextFormat("+%d -%d", addsDels[i][0], addsDels[i][1]), FONT_SIZE) + 10;
        if (lineWidth > width) width = lineWidth;
    }
    int ruleCount = 0;
    for (int i = 0; i < lineCount; i++) if (rule[i]) ruleCount++;
    int boxWidth = width + 32, boxHeight = lineCount * 26 + ruleCount * 10 + 22;
    Vector2 mouse = GetMousePosition();
    int x = (int)mouse.x + 18, y = (int)mouse.y + 22;
    if (x + boxWidth > screenWidth - 8) x = (int)mouse.x - 18 - boxWidth;
    if (x < 8) x = 8;
    if (y + boxHeight > screenHeight - 8) y = (int)mouse.y - 12 - boxHeight;
    if (y < 8) y = 8;

    Rectangle box = {(float)x, (float)y, (float)boxWidth, (float)boxHeight};
    DrawRectangleRounded((Rectangle){box.x + 3, box.y + 4, box.width, box.height}, 0.06f, 6, (Color){0, 0, 0, 120});
    DrawRectangleRounded(box, 0.06f, 6, (Color){36, 41, 49, 255});
    DrawRectangleRoundedLinesEx(box, 0.06f, 6, 1, UI_BORDER);

    int textY = y + 12;
    for (int i = 0; i < lineCount; i++) {
        if (rule[i]) { DrawLine(x + 12, textY + 3, x + boxWidth - 12, textY + 3, UI_BORDER); textY += 10; }
        if (bold[i]) DrawTextBold(text[i], x + 16, textY, FONT_SIZE, colors[i]);
        else         DrawText(text[i], x + 16, textY, FONT_SIZE, colors[i]);
        if (linkLen[i] > 0) {            /* redraw the name part in blue on a clean background */
            int nameWidth = MeasureText(DisplaySlice(text[i], linkLen[i]), FONT_SIZE);
            DrawRectangle(x + 15, textY - 2, nameWidth + 2, FONT_SIZE + 4, (Color){36, 41, 49, 255});
            DrawText(DisplaySlice(text[i], linkLen[i]), x + 16, textY, FONT_SIZE, UI_LINK);
        }
        if (addsDels[i][0] >= 0) DrawAddsDels(addsDels[i][0], addsDels[i][1], x + 16 + MeasureText(text[i], FONT_SIZE), textY);
        textY += 26;
    }
}

static Color HeatColor(int count)
{
    if (count <= 0) return (Color){ 33,  38,  45, 255};
    if (count == 1) return (Color){ 14,  68,  41, 255};
    if (count == 2) return (Color){  0, 109,  50, 255};
    if (count <= 4) return (Color){ 38, 166,  65, 255};
    return             (Color){ 57, 211,  83, 255};
}

/* Squares for the last 26 weeks, coloured by how many contributions userId made that day.
   Hovering a square sets *hoverDay; clicking one opens that day's page. */
void DrawContributions(Rectangle area, int userId, long long *hoverDay, bool narrow)
{
    enum { WEEKS = 26 };
    int counts[WEEKS * 7] = {0}, total = 0;
    long long today = DayStart((long long)time(NULL));
    time_t todayTime = (time_t)today;
    int weekday = localtime(&todayTime)->tm_wday;     /* 0 = Sunday: rows run Sunday to Saturday */
    int todayPos = (WEEKS - 1) * 7 + weekday;

    for (int i = 0; i < repoCount; i++) {
        Repo *repo = &repos[i];
        if (!CanSee(repo, currentUserId)) continue;       /* private, and you're not in it */
        for (int k = 0; k < repo->commitCount + repo->issueCount; k++) {
            bool isCommit = k < repo->commitCount;
            int author    = isCommit ? repo->commits[k].author : repo->issues[k - repo->commitCount].author;
            long long when   = isCommit ? repo->commits[k].time   : repo->issues[k - repo->commitCount].time;
            if (author != userId || (isCommit && repo->commits[k].copied)) continue;
            int pos = todayPos - DaysBetween(DayStart(when), today);
            if (pos >= 0 && pos <= todayPos) { counts[pos]++; total++; }
        }
    }

    DrawRectangleRec(area, UI_PANEL);
    DrawRectangleLinesEx(area, 1, UI_BORDER);
    DrawText(TextFormat("%d contribution%s in the last 6 months", total, total == 1 ? "" : "s"),
             (int)area.x + 16, (int)area.y + 12, FONT_SIZE, UI_TEXT);
    const char *hint = "Hover a day for details, click it for the full list";
    int titleWidth = MeasureText(TextFormat("%d contribution%s in the last 6 months", total, total == 1 ? "" : "s"), FONT_SIZE);
    if (!narrow && area.width > titleWidth + MeasureText(hint, FONT_SIZE) + 60)
        DrawText(hint, (int)(area.x + area.width) - 16 - MeasureText(hint, FONT_SIZE), (int)area.y + 12, FONT_SIZE, UI_MUTED);

    int hovered = -1;
    for (int pos = 0; pos <= todayPos; pos++) {
        int column = pos / 7, row = pos % 7;
        Rectangle cell = {area.x + 16 + column * 15, area.y + 44 + row * 15, 12, 12};
        Rectangle hit  = {cell.x - 1, cell.y - 1, 15, 15};     /* no gaps between hover areas */
        DrawRectangleRounded(cell, 0.3f, 4, HeatColor(counts[pos]));
        if (IsHovered(hit)) hovered = pos;
    }
    if (hovered >= 0) {
        int column = hovered / 7, row = hovered % 7;
        Rectangle cell = {area.x + 16 + column * 15, area.y + 44 + row * 15, 12, 12};
        DrawRectangleRoundedLinesEx((Rectangle){cell.x - 1, cell.y - 1, 14, 14}, 0.3f, 4, 2, UI_TEXT);
        wantedCursor = MOUSE_CURSOR_POINTING_HAND;
        long long day = AddDays(today, hovered - todayPos);
        *hoverDay = day;
        if (WasClicked((Rectangle){cell.x - 1, cell.y - 1, 15, 15})) OpenDay(userId, day);
    }

    int legendX = (int)(area.x + area.width) - 200, legendY = (int)(area.y + area.height) - 30;
    if (narrow) { legendX = (int)area.x + 16 + 26 * 15 - 190; legendY = (int)area.y + 44 + 7 * 15 + 6; }  /* under the grid */
    DrawText("Less", legendX, legendY, FONT_SIZE, UI_MUTED);
    for (int i = 0; i < 5; i++)
        DrawRectangleRounded((Rectangle){(float)legendX + 52 + i * 16, (float)legendY + 4, 12, 12}, 0.3f, 4, HeatColor(i == 4 ? 5 : i));
    DrawText("More", legendX + 138, legendY, FONT_SIZE, UI_MUTED);
}
