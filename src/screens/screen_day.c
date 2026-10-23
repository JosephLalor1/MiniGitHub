/*
 * screen_day.c - one day of someone's contributions
 *
 * Totals by repository, and every commit and issue in the order they
 * happened.
 */

#include "screen_day.h"
#include "animation.h"
#include "app_state.h"
#include "contributions.h"
#include "model.h"
#include "navigation.h"
#include "ui_core.h"
#include "users.h"
#include "widgets.h"

/* ---------------- one day of contributions ---------------- */
/* Draws the day page: one person's contributions on currentDay. */
void ScreenDay(void)
{
    long long today = DayStart((long long)time(NULL));

    bool ownDay = dayUserId == currentUserId;
    (void)ownDay;
    if (Button((Rectangle){(float)screenWidth - 380, 72, 175, 38}, "< Earlier day", UI_BUTTON)) { currentDay = AddDays(currentDay, -1); dayScroll = dayReposScroll = 0; }
    bool canLater = currentDay < today;
    if (ButtonEnabled((Rectangle){(float)screenWidth - 195, 72, 175, 38}, "Later day >", canLater ? UI_BUTTON : UI_PANEL, canLater)) {
        currentDay = AddDays(currentDay, 1);
        dayScroll = dayReposScroll = 0;
    }

    char title[96];
    if (ownDay) snprintf(title, sizeof title, "%s%s", FormatDay(currentDay, true), currentDay == today ? "  (today)" : "");
    else      snprintf(title, sizeof title, "%s on %s", UserName(dayUserId), FormatDay(currentDay, true));
    DrawTextFit(title, 150, 75, 30, screenWidth - 560, UI_TEXT);

    GatherDay(dayUserId, currentDay);

    /* one-line summary */
    char countText1[48], countText2[48];
    int summaryY = 124;
    if (activityTotal == 0) {
        DrawText(TextFormat("%s didn't make any commits or open any issues on this day.", ownDay ? "You" : UserName(dayUserId)),
                 20, summaryY, FONT_SIZE, UI_MUTED);
    } else {
        const char *text = TextFormat("%s in %s   |   ", CountText(countText1, activityTotal, "contribution", "contributions"),
                                   CountText(countText2, tallyCount, "repository", "repositories"));
        DrawText(text, 20, summaryY, FONT_SIZE, UI_TEXT);
        int x = 20 + MeasureText(text, FONT_SIZE);
        text = TextFormat("%s, %s changed   ", CountText(countText1, dayCommits, "commit", "commits"), CountText(countText2, dayFiles, "file", "files"));
        DrawText(text, x, summaryY, FONT_SIZE, UI_MUTED);
        x += MeasureText(text, FONT_SIZE);
        x += DrawAddsDels(dayAdds, dayDels, x, summaryY);
        if (dayIssues) DrawText(TextFormat("   |   %s", CountText(countText1, dayIssues, "issue opened", "issues opened")), x, summaryY, FONT_SIZE, UI_MUTED);
    }
    DrawLine(0, 160, screenWidth, 160, UI_BORDER);

    float top = 176, bottom = (float)screenHeight - 20;

    /* ---- left: breakdown by repository ---- */
    Rectangle leftPanel = {20, top, 300, bottom - top};
    DrawRectangleRec(leftPanel, UI_PANEL);
    DrawRectangleLinesEx(leftPanel, 1, UI_BORDER);
    DrawTextBold("By repository", (int)leftPanel.x + 14, (int)top + 12, FONT_SIZE, UI_TEXT);
    Rectangle listArea = {leftPanel.x + 1, top + 44, leftPanel.width - 2, leftPanel.height - 46};
    float rowHeight = 84;
    ScrollArea(listArea, &dayReposScroll, tallyCount * rowHeight);
    if (!tallyCount) DrawText("Nothing here", (int)listArea.x + 14, (int)listArea.y + 6, FONT_SIZE, UI_MUTED);
    BeginScissorMode((int)listArea.x, (int)listArea.y, (int)listArea.width, (int)listArea.height);
    for (int i = 0; i < tallyCount; i++) {
        RepoTally *tally = &tallies[i];
        Rectangle row = {listArea.x, listArea.y + i * rowHeight - dayReposScroll, listArea.width, rowHeight};
        bool hovered = IsHovered(listArea) && IsHovered(row);
        DrawHoverFill(row, AnimationKey("day repo", (float)tally->repo, 0), hovered);
        if (hovered) wantedCursor = MOUSE_CURSOR_POINTING_HAND;
        if (i) DrawLine((int)row.x + 12, (int)row.y, (int)(row.x + row.width - 12), (int)row.y, UI_BORDER);
        DrawTextFit(RepoLabel(tally->repo), (int)row.x + 14, (int)row.y + 10, FONT_SIZE, (int)row.width - 28, UI_LINK);
        DrawText(TallyText(tally->commits, tally->issues), (int)row.x + 14, (int)row.y + 34, FONT_SIZE, UI_MUTED);
        if (tally->commits) DrawAddsDels(tally->adds, tally->dels, (int)row.x + 14, (int)row.y + 56);
        if (IsHovered(listArea) && WasClicked(row)) { OpenRepo(tally->repo); EndScissorMode(); return; }
    }
    EndScissorMode();
    DrawScrollbar(listArea, dayReposScroll, tallyCount * rowHeight);

    /* ---- right: every contribution, in the order it happened ---- */
    Rectangle rightArea = {340, top, (float)screenWidth - 360, bottom - top};
    if (activityTotal == 0) {
        DrawRectangleRec(rightArea, UI_PANEL);
        DrawRectangleLinesEx(rightArea, 1, UI_BORDER);
        const char *message = "Use the buttons at the top to look at other days.";
        DrawText(message, (int)(rightArea.x + rightArea.width / 2) - MeasureText(message, FONT_SIZE) / 2, (int)(rightArea.y + rightArea.height / 2) - 10, FONT_SIZE, UI_MUTED);
        return;
    }

    /* work out each card's height first, so the list can scroll */
    static float cardY[MAX_ACTIVITIES], cardH[MAX_ACTIVITIES];
    static char        kinds[MAX_FILES * 2];
    static const char *names[MAX_FILES * 2];
    float totalHeight = 0;
    for (int i = 0; i < activityCount; i++) {
        Activity *activity = &activities[i];
        int fileCount = activity->kind == 'C' ? CommitFileList(&repos[activity->repo], activity->index, kinds, names, MAX_FILES * 2) : 0;
        cardY[i] = totalHeight;
        cardH[i] = 76 + (fileCount ? fileCount * 24 + 8 : 0);
        totalHeight += cardH[i] + 10;
    }
    float extraHeight = activityCount < activityTotal ? 40 : 0;
    ScrollArea(rightArea, &dayScroll, totalHeight + extraHeight);

    BeginScissorMode((int)rightArea.x, (int)rightArea.y, (int)rightArea.width, (int)rightArea.height);
    for (int i = 0; i < activityCount; i++) {
        Activity *activity = &activities[i];
        Repo *repo = &repos[activity->repo];
        Rectangle card = {rightArea.x, rightArea.y + cardY[i] - dayScroll, rightArea.width - 12, cardH[i]};
        if (card.y > rightArea.y + rightArea.height || card.y + card.height < rightArea.y) continue;
        bool hovered = IsHovered(rightArea) && IsHovered(card);
        if (hovered) wantedCursor = MOUSE_CURSOR_POINTING_HAND;
        DrawRectangleRec(card, CardColor(AnimationKey("day card", (float)i, 0), hovered));
        DrawRectangleLinesEx(card, 1, UI_BORDER);

        /* left column: time and what kind of thing it was */
        char clockText[16];
        time_t when = (time_t)activity->time;
        strftime(clockText, sizeof clockText, "%H:%M", localtime(&when));
        DrawText(clockText, (int)card.x + 18, (int)card.y + 12, FONT_SIZE, UI_MUTED);
        Rectangle pill = {card.x + 14, card.y + 40, 82, 26};
        const char *kindLabel = activity->kind == 'C' ? "Commit" : "Issue";
        DrawRectangleRounded(pill, 0.5f, 6, activity->kind == 'C' ? UI_GREEN : UI_PURPLE);
        DrawText(kindLabel, (int)(pill.x + pill.width / 2) - MeasureText(kindLabel, FONT_SIZE) / 2, (int)pill.y + 3, FONT_SIZE, UI_TEXT);

        int contentX = (int)card.x + 116, contentWidth = (int)card.width - 132;
        if (activity->kind == 'C') {
            Commit *commit = &repo->commits[activity->index];
            CommitStats(repo, activity->index);
            DrawTextFit(commit->message, contentX, (int)card.y + 12, FONT_SIZE, contentWidth, UI_TEXT);
            const char *meta = TextFormat("%s   |   commit %s   |   %d file%s changed   ",
                                          RepoLabel(activity->repo), commit->id, commit->statFiles, commit->statFiles == 1 ? "" : "s");
            DrawText(meta, contentX, (int)card.y + 42, FONT_SIZE, UI_MUTED);
            DrawAddsDels(commit->statAdds, commit->statDels, contentX + MeasureText(meta, FONT_SIZE), (int)card.y + 42);

            int fileCount = CommitFileList(repo, activity->index, kinds, names, MAX_FILES * 2);
            for (int fileIndex = 0; fileIndex < fileCount; fileIndex++) {
                int fileY = (int)card.y + 76 + fileIndex * 24;
                char kindText[2] = {kinds[fileIndex], 0};
                Color kindColor = kinds[fileIndex] == 'A' ? UI_GREEN_TEXT : kinds[fileIndex] == 'D' ? UI_RED_TEXT : UI_YELLOW;
                DrawText(kindText, contentX, fileY, FONT_SIZE, kindColor);
                DrawTextFit(names[fileIndex], contentX + 24, fileY, FONT_SIZE, contentWidth - 24, UI_TEXT);
            }
            if (IsHovered(rightArea) && WasClicked(card)) {
                int repoIndex = activity->repo, ci = activity->index;
                EndScissorMode();
                OpenRepo(repoIndex);
                repoTab = TAB_COMMITS;
                OpenCommit(ci);
                openedFromDay = true;
                return;
            }
        } else {
            Issue *issue = &repo->issues[activity->index];
            DrawTextFit(issue->title, contentX, (int)card.y + 12, FONT_SIZE, contentWidth, UI_TEXT);
            DrawText(TextFormat("%s   |   opened issue #%d   |   %s", RepoLabel(activity->repo), activity->index + 1,
                                issue->open ? "still open" : "now closed"),
                     contentX, (int)card.y + 42, FONT_SIZE, UI_MUTED);
            if (IsHovered(rightArea) && WasClicked(card)) {
                int repoIndex = activity->repo;
                EndScissorMode();
                OpenRepo(repoIndex);
                repoTab = TAB_ISSUES;
                return;
            }
        }
    }
    if (extraHeight > 0)
        DrawText(TextFormat("...and %d more not shown", activityTotal - activityCount), (int)rightArea.x + 4, (int)(rightArea.y + totalHeight - dayScroll + 8), FONT_SIZE, UI_MUTED);
    EndScissorMode();
    DrawScrollbar(rightArea, dayScroll, totalHeight + extraHeight);
}
