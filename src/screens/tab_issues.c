/*
 * tab_issues.c - the Issues tab
 */

#include "tab_issues.h"
#include "app_state.h"
#include "model.h"
#include "repo.h"
#include "ui_core.h"
#include "users.h"
#include "util.h"
#include "widgets.h"

/* Draws a repository's Issues tab. */
void TabIssues(Repo *repo)
{
    bool enter = TextFieldWidget(&issueTitleField, (Rectangle){20, 186, (float)screenWidth - 200, 42}, "Title for a new issue (a bug, an idea, a to-do...)");
    if (Button((Rectangle){(float)screenWidth - 170, 186, 150, 42}, "New issue", UI_GREEN) || enter) {
        if (!issueTitleField.text[0])                  Toast("Give the issue a title first");
        else if (repo->issueCount >= MAX_ISSUES) Toast("This repository has too many issues");
        else {
            Issue *issue = &repo->issues[repo->issueCount++];
            snprintf(issue->title, TEXT_LEN, "%s", issueTitleField.text);
            issue->open   = true;
            issue->time   = (long long)time(NULL);
            issue->author = currentUserId;
            TextFieldClear(&issueTitleField);
            MarkDirty();
            Toast(TextFormat("Opened issue #%d", repo->issueCount));
        }
    }

    int openCount = OpenIssueCount(repo);
    DrawText(TextFormat("%d open    %d closed", openCount, repo->issueCount - openCount), 20, 244, FONT_SIZE, UI_TEXT);

    /* open issues first, newest first within each group */
    int order[MAX_ISSUES], count = 0;
    for (int pass = 0; pass < 2; pass++)
        for (int i = repo->issueCount - 1; i >= 0; i--)
            if (repo->issues[i].open == (pass == 0)) order[count++] = i;

    Rectangle listArea = {20, 276, (float)screenWidth - 40, (float)screenHeight - 296};
    float rowHeight = 68;
    ScrollArea(listArea, &issueListScroll, count * rowHeight);
    if (!count) DrawText("No issues yet. Use issues to keep track of bugs and ideas.", 20, 286, FONT_SIZE, UI_MUTED);

    BeginScissorMode((int)listArea.x, (int)listArea.y, (int)listArea.width, (int)listArea.height);
    for (int k = 0; k < count; k++) {
        int i = order[k];
        Issue *issue = &repo->issues[i];
        Rectangle row = {listArea.x, listArea.y + k * rowHeight - issueListScroll, listArea.width - 12, rowHeight};
        if (row.y > listArea.y + listArea.height || row.y + row.height < listArea.y) continue;
        DrawRectangleRec(row, UI_PANEL);
        DrawRectangleLinesEx(row, 1, UI_BORDER);

        Rectangle pill = {row.x + 16, row.y + 19, 90, 30};
        DrawRectangleRounded(pill, 0.5f, 6, issue->open ? UI_GREEN : UI_PURPLE);
        const char *state = issue->open ? "Open" : "Closed";
        DrawText(state, (int)(pill.x + pill.width / 2) - MeasureText(state, FONT_SIZE) / 2, (int)pill.y + 5, FONT_SIZE, UI_TEXT);

        DrawTextFit(issue->title, (int)row.x + 124, (int)row.y + 12, FONT_SIZE, (int)row.width - 290, issue->open ? UI_TEXT : UI_MUTED);
        DrawText(TextFormat("#%d opened %s by %s", i + 1, TimeAgo(issue->time), UserName(issue->author)),
                 (int)row.x + 124, (int)row.y + 38, FONT_SIZE, UI_MUTED);

        /* only the repo owner or whoever opened the issue may close / reopen it */
        bool canManage = CanEdit(repo) || issue->author == currentUserId;
        if (canManage &&
/* Draws a button that can be switched off (greyed, not clickable). Returns true when clicked. */
            ButtonEnabled((Rectangle){row.x + row.width - 140, row.y + 16, 124, 36}, issue->open ? "Close" : "Reopen", UI_BUTTON, IsHovered(listArea))) {
            issue->open = !issue->open;
            MarkDirty();
        }
    }
    EndScissorMode();
    DrawScrollbar(listArea, issueListScroll, count * rowHeight);
}
