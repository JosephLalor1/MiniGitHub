/*
 * repo_header.c - the top of a repository page
 *
 * Name, visibility, Star / Fork / Code buttons and tabs; the Code and
 * branch menus; the fork bar and the merge banner.
 */

#include "repo_header.h"
#include "app_state.h"
#include "branches.h"
#include "downloads.h"
#include "drafts.h"
#include "editor.h"
#include "forks.h"
#include "git_server.h"
#include "merge.h"
#include "model.h"
#include "navigation.h"
#include "repo.h"
#include "repo_people.h"
#include "tab_code.h"
#include "ui_core.h"
#include "users.h"
#include "widgets.h"

/* The strip at the top of a fork's Code tab: where it came from, ahead / behind, and Sync. */
float DrawForkBar(Repo *repo, float top)
{
    if (!repo->isFork) return top;
    Rectangle bar = {20, top, (float)screenWidth - 40, 46};
    ForkState state = GetForkState(repo);
    bool upstreamHidden = state.upstream >= 0 && !CanSee(&repos[state.upstream], currentUserId);  /* the original went private */
    bool canSync = CanEdit(repo) && state.upstream >= 0 && !upstreamHidden && state.related && state.behind > 0;
    Color edgeColor = canSync ? UI_YELLOW : UI_BORDER;
    DrawRectangleRec(bar, UI_PANEL);
    DrawRectangleLinesEx(bar, 1, edgeColor);

    const char *originalName = TextFormat("%s / %s", UserName(repo->forkOwner), repo->forkName);
    const char *message;
    if (upstreamHidden)                   message = "Forked from a repository that has since been made private.";
    else if (state.upstream < 0)            message = TextFormat("Forked from %s, which has since been deleted.", originalName);
    else if (!state.related)          message = TextFormat("Forked from %s, but its history no longer matches this fork.", originalName);
    else if (!state.ahead && !state.behind) message = TextFormat("This fork is up to date with %s.", originalName);
    else if (!state.behind)           message = TextFormat("This fork is %d commit%s ahead of %s.", state.ahead, state.ahead == 1 ? "" : "s", originalName);
    else if (!state.ahead)            message = TextFormat("This fork is %d commit%s behind %s.", state.behind, state.behind == 1 ? "" : "s", originalName);
    else                          message = TextFormat("This fork is %d commit%s ahead of and %d behind %s.",
                                                   state.ahead, state.ahead == 1 ? "" : "s", state.behind, originalName);
    float buttonWidth = canSync ? 150 : 0;
    DrawTextFit(message, (int)bar.x + 16, (int)bar.y + 13, FONT_SIZE, (int)(bar.width - 32 - buttonWidth - 12), canSync ? UI_TEXT : UI_MUTED);

    if (canSync && Button((Rectangle){bar.x + bar.width - 8 - buttonWidth, bar.y + 5, buttonWidth, 36}, "Sync fork", UI_GREEN)) {
        char done[TEXT_LEN] = "";
        const char *error = SyncFork(repo, done, sizeof done);
        if (error) Toast(error);
        else { Toast(done); ForgetScratchFile(); MarkDirty(); }
    }
    return top + 46 + 12;
}

/* ---------- the Code menu: clone address and Download ZIP ---------- */

bool      codeMenuOpen;           /* the menu under the Code button is open */
static Rectangle codeButtonRect;  /* where the Code button is              */

/* The menu is drawn after everything else on the page so it sits on top,
   but it takes its clicks first (before the page under it can). So this is
   called twice a frame: draw = false to handle the mouse, true to draw. */
void CodeMenu(Repo *repo, bool draw)
{
    if (!codeMenuOpen) return;
    float width = 600, height = gitPort ? 236 : 210;
    Rectangle menu = {codeButtonRect.x + codeButtonRect.width - width, codeButtonRect.y + codeButtonRect.height + 6, width, height};
    if (menu.x < 20) menu.x = 20;
    const char *url = CloneUrl(repo);
    Rectangle box  = {menu.x + 16, menu.y + 48, width - 32 - 96, 40};
    Rectangle copyButton = {box.x + box.width + 8, box.y, 88, 40};
    Rectangle zipButton  = {menu.x + 16, menu.y + height - 56, width - 32, 40};
    Vector2 mouse = GetMousePosition();

    if (!draw) {
        hoverBlockedArea = menu; hoverBlocked = true;
        if (!IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) return;
        if (CheckCollisionPointRec(mouse, codeButtonRect)) return;     /* the button itself toggles it */
        if (!CheckCollisionPointRec(mouse, menu)) { codeMenuOpen = false; return; }
        clickHandled = true;                                           /* nothing under the menu gets this click */
        if (gitPort && CheckCollisionPointRec(mouse, copyButton)) {
            SetClipboardText(url);
            Toast("Copied the address - use it with git clone");
        } else if (CheckCollisionPointRec(mouse, zipButton)) {
            codeMenuOpen = false;
            DownloadRepo(repo);
        }
        return;
    }
    hoverBlocked = false;
    DrawRectangleRounded((Rectangle){menu.x + 3, menu.y + 5, menu.width, menu.height}, 0.04f, 6, (Color){0, 0, 0, 110});
    DrawRectangleRounded(menu, 0.04f, 6, (Color){22, 27, 34, 255});
    DrawRectangleRoundedLinesEx(menu, 0.04f, 6, 1, UI_BORDER);
    DrawTextBold("Clone with git", (int)menu.x + 16, (int)menu.y + 14, FONT_SIZE, UI_TEXT);
    if (gitPort) {
        DrawRectangleRec(box, UI_BASE);
        DrawRectangleLinesEx(box, 1, UI_BORDER);
        DrawTextFit(url, (int)box.x + 10, (int)box.y + 10, FONT_SIZE, (int)box.width - 20, UI_TEXT);
        bool copyHovered = CheckCollisionPointRec(mouse, copyButton);
        if (copyHovered) wantedCursor = MOUSE_CURSOR_POINTING_HAND;
        DrawRectangleRounded(copyButton, 0.25f, 6, copyHovered ? ColorBrightness(UI_BUTTON, 0.15f) : UI_BUTTON);
        DrawText("Copy", (int)(copyButton.x + (copyButton.width - MeasureText("Copy", FONT_SIZE)) / 2), (int)copyButton.y + 10, FONT_SIZE, UI_TEXT);
        DrawTextFit("Works with git clone, pull and push. When git asks, sign", (int)menu.x + 16, (int)menu.y + 100, FONT_SIZE, (int)width - 32, UI_MUTED);
        DrawTextFit("in with your Mini GitHub username and password.", (int)menu.x + 16, (int)menu.y + 124, FONT_SIZE, (int)width - 32, UI_MUTED);
    } else {
        DrawTextFit("Mini GitHub's git server isn't running:", (int)menu.x + 16, (int)menu.y + 50, FONT_SIZE, (int)width - 32, UI_ORANGE);
        DrawTextFit(gitProblem, (int)menu.x + 16, (int)menu.y + 76, FONT_SIZE, (int)width - 32, UI_MUTED);
    }
    DrawLine((int)menu.x + 1, (int)(zipButton.y - 14), (int)(menu.x + menu.width - 1), (int)(zipButton.y - 14), UI_BORDER);
    bool zipHovered = CheckCollisionPointRec(mouse, zipButton);
    if (zipHovered) wantedCursor = MOUSE_CURSOR_POINTING_HAND;
    DrawRectangleRounded(zipButton, 0.25f, 6, zipHovered ? ColorBrightness(UI_BUTTON, 0.15f) : UI_BUTTON);
    DrawText("Download ZIP", (int)(zipButton.x + (zipButton.width - MeasureText("Download ZIP", FONT_SIZE)) / 2), (int)zipButton.y + 10, FONT_SIZE, UI_TEXT);
}

/* ---------- the branch menu ---------- */
bool      branchMenuOpen;            /* open?                                   */
static Rectangle branchButtonRect;   /* the button it hangs from                */
static bool      branchMenuClicked;  /* this frame's click landed in the menu   */
static float     branchMenuScroll;

BranchStatsCache branchStats;
/* Works out how far each branch is ahead of and behind the default branch (only when the commits
   have changed). */
void UpdateBranchStats(Repo *repo)
{
    if (branchStats.repo == repo && branchStats.generation == repo->generation && branchStats.branchCount == repo->branchCount) return;
    branchStats.repo = repo; branchStats.generation = repo->generation; branchStats.branchCount = repo->branchCount;
    int defaultTip = DefaultTip(repo);
    for (int branch = 0; branch < repo->branchCount; branch++) {
        branchStats.ahead[branch]  = CountAhead(repo, repo->branches[branch].tip, defaultTip);
        branchStats.behind[branch] = CountAhead(repo, defaultTip, repo->branches[branch].tip);
    }
}

/* Moves onto another branch. */
void SwitchBranch(Repo *repo, int branchIndex)
{
    if (branchIndex < 0 || branchIndex >= repo->branchCount || branchIndex == repo->currentBranch) return;
    repo->currentBranch = branchIndex;
    ForgetScratchFile();
    fileListScroll = 0;
    Unfocus();
    Toast(TextFormat("Switched to branch %s", repo->branches[branchIndex].name));
}

/* A little "branch" picture: two dots joined by a line that forks. */
static void DrawBranchIcon(float x, float y, Color color)
{
    DrawLineEx((Vector2){x, y - 7}, (Vector2){x, y + 7}, 2, color);
    DrawLineBezier((Vector2){x, y + 3}, (Vector2){x + 8, y - 5}, 2, color);
    DrawCircle((int)x, (int)y + 7, 3, color);
    DrawCircle((int)x + 8, (int)y - 6, 3, color);
    DrawCircle((int)x, (int)y - 7, 3, color);
}

/* The button showing the branch you're on; clicking opens the menu. */
void BranchButton(Repo *repo, Rectangle rect)
{
    branchButtonRect = rect;
    bool hovered = IsHovered(rect);
    if (hovered) wantedCursor = MOUSE_CURSOR_POINTING_HAND;
    DrawRectangleRounded(rect, 0.25f, 6, hovered || branchMenuOpen ? ColorBrightness(UI_BUTTON, 0.15f) : UI_BUTTON);
    DrawRectangleRoundedLinesEx(rect, 0.25f, 6, 1, UI_BORDER);
    DrawBranchIcon(rect.x + 14, rect.y + rect.height / 2, UI_MUTED);
    const char *name = repo->branchCount ? CurrentBranchName(repo) : TextFormat("%s (no commits yet)", repo->initialBranch);
    DrawTextFit(name, (int)rect.x + 34, (int)(rect.y + (rect.height - FONT_SIZE) / 2), FONT_SIZE, (int)rect.width - 60, UI_TEXT);
    float arrowX = rect.x + rect.width - 16, arrowY = rect.y + rect.height / 2;
    DrawTriangle((Vector2){arrowX - 5, arrowY - 2}, (Vector2){arrowX, arrowY + 4}, (Vector2){arrowX + 5, arrowY - 2}, UI_MUTED);
    if (WasClicked(rect)) { branchMenuOpen = !branchMenuOpen; codeMenuOpen = false; branchMenuScroll = 0; TextFieldClear(&branchNameField); }
}

/* Like the Code menu: called with draw = false early in the frame (it takes
   its clicks before the page under it), and with draw = true at the end. */
void BranchMenu(Repo *repo, bool draw)
{
    if (!branchMenuOpen) { branchMenuClicked = false; return; }
    bool canEdit = CanEdit(repo) && repo->branchCount;
    float width = 560, rowHeight = 40, top = branchButtonRect.y + branchButtonRect.height + 6;
    float footerHeight = canEdit ? 98 : 0;
    float listHeight = repo->branchCount * rowHeight, maxListHeight = (float)screenHeight - top - 60 - footerHeight - 50;
    if (maxListHeight < rowHeight * 3) maxListHeight = rowHeight * 3;
    if (listHeight > maxListHeight) listHeight = maxListHeight;
    if (!repo->branchCount) listHeight = 64;
    Rectangle menu = {branchButtonRect.x, top, width, 50 + listHeight + footerHeight + 8};
    if (menu.x + menu.width > screenWidth - 20) menu.x = screenWidth - 20 - menu.width;
    Vector2 mouse = GetMousePosition();

    if (!draw) {
        branchMenuClicked = false;
        hoverBlockedArea = menu; hoverBlocked = true;
        if (!IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) return;
        if (CheckCollisionPointRec(mouse, branchButtonRect)) return;
        if (!CheckCollisionPointRec(mouse, menu)) { branchMenuOpen = false; return; }
        branchMenuClicked = true;
        clickHandled = true;                                 /* the page under the menu doesn't get it */
        return;
    }
    clickHandled = !branchMenuClicked;                       /* the menu's own buttons can use this click */
    hoverBlocked = false;
    DrawRectangleRounded((Rectangle){menu.x + 3, menu.y + 5, menu.width, menu.height}, 0.03f, 6, (Color){0, 0, 0, 110});
    DrawRectangleRounded(menu, 0.03f, 6, (Color){22, 27, 34, 255});
    DrawRectangleRoundedLinesEx(menu, 0.03f, 6, 1, UI_BORDER);
    DrawTextBold("Branches", (int)menu.x + 16, (int)menu.y + 14, FONT_SIZE, UI_TEXT);
    if (repo->branchCount > 1) {
        const char *compareText = TextFormat("ahead / behind %s", DefaultBranchName(repo));
        DrawText(compareText, (int)(menu.x + menu.width - 16 - MeasureText(compareText, FONT_SIZE)), (int)menu.y + 14, FONT_SIZE, UI_MUTED);
    }
    Rectangle listArea = {menu.x + 1, menu.y + 46, menu.width - 2, listHeight};
    if (!repo->branchCount) {
        DrawText("No branches yet - the first commit makes one.", (int)listArea.x + 15, (int)listArea.y + 12, FONT_SIZE, UI_MUTED);
    } else {
        UpdateBranchStats(repo);
        ScrollArea(listArea, &branchMenuScroll, repo->branchCount * rowHeight);
        /* default first, then the most recently changed */
        int order[MAX_BRANCHES], count = repo->branchCount;
        for (int i = 0; i < count; i++) order[i] = i;
        for (int i = 0; i < count; i++)
            for (int j = i + 1; j < count; j++) {
                int first = order[i], second = order[j];
                bool shouldSwap = second == repo->defaultBranch || (first != repo->defaultBranch && repo->commits[repo->branches[second].tip].time > repo->commits[repo->branches[first].tip].time);
                if (shouldSwap) { order[i] = second; order[j] = first; }
            }
        BeginScissorMode((int)listArea.x, (int)listArea.y, (int)listArea.width, (int)listArea.height);
        int actBranch = -1, actKind = 0;                     /* what got clicked: 1 switch, 2 merge, 3 default, 4 delete */
        for (int i = 0; i < count; i++) {
            int branchIndex = order[i];
            Rectangle row = {listArea.x, listArea.y + i * rowHeight - branchMenuScroll, listArea.width - 12, rowHeight};
            if (row.y + rowHeight < listArea.y || row.y > listArea.y + listArea.height) continue;
            bool hovered = CheckCollisionPointRec(mouse, row) && CheckCollisionPointRec(mouse, listArea);
            bool isCurrent = branchIndex == repo->currentBranch;
            if (hovered) DrawRectangleRec(row, UI_HOVER);
            if (isCurrent) DrawRectangle((int)row.x, (int)row.y + 6, 3, (int)rowHeight - 12, accentColor);
            DrawBranchIcon(row.x + 18, row.y + rowHeight / 2, isCurrent ? UI_TEXT : UI_MUTED);
            int nameX = (int)row.x + 38;
            const char *name = repo->branches[branchIndex].name;
            float right = row.x + row.width - 12;
            /* buttons when hovering a row you can act on */
            float buttonX = right;
            if (hovered && canEdit) {
                if (branchIndex != repo->defaultBranch) {
                    Rectangle deleteRect = {buttonX - 76, row.y + 6, 76, 28};
                    if (ConfirmButton(deleteRect, "Delete", "Sure?", 40 + branchIndex)) { actBranch = branchIndex; actKind = 4; }
                    buttonX -= 84;
                }
                if (IsMine(repo) && branchIndex != repo->defaultBranch) {
                    Rectangle defaultRect = {buttonX - 92, row.y + 6, 92, 28};
                    if (ButtonEnabled(defaultRect, "Default", UI_BUTTON, true)) { actBranch = branchIndex; actKind = 3; }
                    buttonX -= 100;
                }
                if (!isCurrent) {
                    const char *mergeLabel = TextFormat("Merge into %s", CurrentBranchName(repo));
                    float mergeWidth = (float)MeasureText(mergeLabel, FONT_SIZE) + 24;
                    if (mergeWidth > 230) { mergeLabel = "Merge in here"; mergeWidth = (float)MeasureText(mergeLabel, FONT_SIZE) + 24; }
                    Rectangle mergeRect = {buttonX - mergeWidth, row.y + 6, mergeWidth, 28};
                    if (ButtonEnabled(mergeRect, mergeLabel, UI_GREEN, true)) { actBranch = branchIndex; actKind = 2; }
                    buttonX -= mergeWidth + 8;
                }
            } else if (branchIndex != repo->defaultBranch) {
                const char *aheadBehind = !branchStats.ahead[branchIndex] && !branchStats.behind[branchIndex] ? "same as default"
                               : TextFormat("%d ahead  |  %d behind", branchStats.ahead[branchIndex], branchStats.behind[branchIndex]);
                DrawText(aheadBehind, (int)(right - MeasureText(aheadBehind, FONT_SIZE)), (int)row.y + 10, FONT_SIZE, UI_MUTED);
                buttonX = right - MeasureText(aheadBehind, FONT_SIZE) - 12;
            }
            int maxNameWidth = (int)(buttonX - nameX - (branchIndex == repo->defaultBranch ? 90 : 10));
            if (isCurrent) DrawTextBold(TextFormat("%s", name), nameX, (int)row.y + 10, FONT_SIZE, UI_TEXT);
            DrawTextFit(name, nameX, (int)row.y + 10, FONT_SIZE, maxNameWidth, isCurrent ? UI_TEXT : UI_LINK);
            if (branchIndex == repo->defaultBranch) {
                int nameWidth = MeasureText(name, FONT_SIZE); if (nameWidth > maxNameWidth) nameWidth = maxNameWidth;
                Rectangle pill = {(float)(nameX + nameWidth + 10), row.y + 9, (float)MeasureText("default", FONT_SIZE) + 16, 23};
                DrawRectangleRounded(pill, 0.5f, 6, UI_BUTTON);
                DrawText("default", (int)pill.x + 8, (int)pill.y + 2, FONT_SIZE, UI_MUTED);
            }
            if (hovered) wantedCursor = MOUSE_CURSOR_POINTING_HAND;
            if (hovered && actBranch < 0 && WasClicked(row)) { actBranch = branchIndex; actKind = 1; }
        }
        EndScissorMode();
        DrawScrollbar(listArea, branchMenuScroll, repo->branchCount * rowHeight);
        if (actKind == 1) { SwitchBranch(repo, actBranch); branchMenuOpen = false; }
        else if (actKind == 3) { repo->defaultBranch = actBranch; repo->generation++; MarkDirty(); Toast(TextFormat("%s is now the default branch", repo->branches[actBranch].name)); }
        else if (actKind == 4) {
            char branchName[NAME_LEN];
            snprintf(branchName, sizeof branchName, "%s", repo->branches[actBranch].name);
            const char *error = DeleteBranch(repo, actBranch);
            Toast(error ? error : TextFormat("Deleted branch %s", branchName));
            if (!error) { ForgetScratchFile(); MarkDirty(); }
        } else if (actKind == 2) {
            char done[TEXT_LEN * 2] = "";
            const char *error = MergeBranch(repo, actBranch, done, sizeof done);
            branchMenuOpen = false;
            if (error) Toast(error);
            else {
                Toast(done);
                ForgetScratchFile();
                MarkDirty();
                Draft *draft = GetDraft(repo, currentUserId, false);
                if (draft && draft->merging) {                /* clashes: off to the Code tab to fix them */
                    snprintf(commitMessageField.text, TEXT_LEN, "Merge branch '%s' into %s", draft->mergeName, CurrentBranchName(repo));
                    commitMessageField.length = (int)strlen(commitMessageField.text);
                    repoTab = TAB_CODE; screen = SCREEN_REPO;
                    const char *firstConflict = FileWithConflictMarkers(repo);
                    if (firstConflict) SelectFileName(firstConflict);
                }
            }
        }
    }
    /* make a new branch from the one you're on */
    if (canEdit && branchMenuOpen) {
        float footerY = menu.y + 46 + listHeight + 10;
        DrawLine((int)menu.x + 1, (int)footerY - 4, (int)(menu.x + menu.width - 1), (int)footerY - 4, UI_BORDER);
        DrawText(TextFormat("New branch, starting from %s:", CurrentBranchName(repo)), (int)menu.x + 16, (int)footerY + 6, FONT_SIZE, UI_MUTED);
        bool enter = TextFieldWidget(&branchNameField, (Rectangle){menu.x + 16, footerY + 36, width - 32 - 170, 40}, "e.g. feature/dark-mode");
        if (Button((Rectangle){menu.x + width - 16 - 160, footerY + 36, 160, 40}, "Create branch", UI_GREEN) || enter) {
            char branchName[NAME_LEN];
            snprintf(branchName, sizeof branchName, "%.*s", NAME_LEN - 1, branchNameField.text);
            for (char *ch = branchName; *ch; ch++) if (*ch == ' ') *ch = '-';
            const char *error = CreateBranch(repo, branchName);
            if (error) Toast(error);
            else { Toast(TextFormat("Made branch %s - you're on it now", branchName)); TextFieldClear(&branchNameField); branchMenuOpen = false; ForgetScratchFile(); MarkDirty(); }
        }
    }
    if (branchMenuClicked) {                                 /* only the menu's own box keeps focus */
        bool keepFocus = branchNameField.active;
        Unfocus();
        branchNameField.active = keepFocus;
    }
    clickHandled = true;
}

/* While a merge with clashes is being finished: what's left to do, and a way out. */
float DrawMergeBanner(Repo *repo, Rectangle bar)
{
    Draft *draft = GetDraft(repo, currentUserId, false);
    if (!draft || !draft->merging) return 0;
    const char *firstLeft = FileWithConflictMarkers(repo);
    int filesLeft = 0;
    for (int i = 0; i < draft->fileCount; i++)
        if (!draft->files[i].deleted && !draft->files[i].binary && HasConflictMarkers(draft->files[i].text, draft->files[i].length)) filesLeft++;
    Color edgeColor = filesLeft ? UI_ORANGE : UI_GREEN_TEXT;
    DrawRectangleRec(bar, filesLeft ? (Color){74, 40, 18, 255} : (Color){18, 52, 30, 255});
    DrawRectangleLinesEx(bar, 1, edgeColor);
    const char *message = filesLeft ? TextFormat("Merging %s into %s: %d file%s to fix, then commit", draft->mergeName, CurrentBranchName(repo),
                                         filesLeft, filesLeft == 1 ? "" : "s")
                            : TextFormat("Merging %s into %s: all fixed - commit to finish", draft->mergeName, CurrentBranchName(repo));
    float abortWidth = 150, nextWidth = filesLeft ? 130 : 0;
    DrawTextFit(message, (int)bar.x + 14, (int)bar.y + 10, FONT_SIZE, (int)(bar.width - 28 - abortWidth - nextWidth - 16), UI_TEXT);
    if (filesLeft && Button((Rectangle){bar.x + bar.width - 8 - abortWidth - 8 - nextWidth, bar.y + 4, nextWidth, bar.height - 8}, "Next clash", UI_BUTTON)) {
        /* the next clashing file after the open one (round to the start) */
        const char *nextFile = firstLeft;
        bool passedCurrent = false;
        for (int i = 0; i < draft->fileCount; i++) {
            WorkFile *draftFile = &draft->files[i];
            if (draftFile->deleted || draftFile->binary || !HasConflictMarkers(draftFile->text, draftFile->length)) continue;
            if (passedCurrent) { nextFile = draftFile->name; break; }
            if (strcmp(draftFile->name, selectedFile) == 0) passedCurrent = true;
        }
        SelectFileName(nextFile);
    }
    if (ConfirmButton((Rectangle){bar.x + bar.width - 8 - abortWidth, bar.y + 4, abortWidth, bar.height - 8}, "Abort merge", "Sure? Undo it", 60)) {
        AbortMerge(repo);
        TextFieldClear(&commitMessageField);
        ForgetScratchFile();
        MarkDirty();
        Toast("Merge aborted - your branch is as it was");
    }
    return bar.height;
}

/* Returns true if the repository was deleted or we left it (so stop drawing it). */
bool DrawRepoHeader(Repo *repo, int activeTab)
{
    CodeMenu(repo, false);                 /* the open menus take their clicks first */
    BranchMenu(repo, false);
    int x = 20, y = 72;
    const char *ownerName = UserName(repo->owner);
    int ownerWidth = MeasureText(ownerName, 30), slashWidth = MeasureText(" / ", 30), nameWidth = MeasureText(repo->name, 30);
    Rectangle ownerHit = {(float)x, (float)y, (float)ownerWidth, 30};
    if (IsHovered(ownerHit)) wantedCursor = MOUSE_CURSOR_POINTING_HAND;
    DrawText(ownerName, x, y, 30, UI_LINK);
    DrawText(" / ", x + ownerWidth, y, 30, UI_MUTED);
    DrawTextBold(repo->name, x + ownerWidth + slashWidth, y, 30, UI_TEXT);
    bool ownRepo = IsMine(repo);
    const char *badge = ownRepo ? "Yours" : IsCollaborator(repo, currentUserId) ? "Collaborator" : "Read-only";
    Rectangle pill = {(float)(x + ownerWidth + slashWidth + nameWidth + 16), (float)y + 2, (float)MeasureText(badge, FONT_SIZE) + 20, 26};
    DrawRectangleRounded(pill, 0.5f, 6, UI_BUTTON);
    DrawText(badge, (int)pill.x + 10, (int)pill.y + 3, FONT_SIZE, UI_MUTED);
    if (WasClicked(ownerHit)) { OpenProfile(repo->owner); return true; }

    /* Public / Private. The owner can click it to change it (twice, to be sure). */
    const char *visibilityText = repo->isPrivate ? "Private" : "Public";
    Rectangle visibilityPill = {pill.x + pill.width + 10, pill.y, (float)MeasureText(visibilityText, FONT_SIZE) + 20, 26};
    if (ownRepo) {
        const char *askText = repo->isPrivate ? "Make public?" : "Make private?";
        if (armedButtonId == 95) visibilityPill.width = (float)MeasureText(askText, FONT_SIZE) + 20;
        bool visibilityHovered = IsHovered(visibilityPill);
        if (visibilityHovered) {
            wantedCursor = MOUSE_CURSOR_POINTING_HAND;
            SetTooltip(repo->isPrivate ? "Private: only you, collaborators and people you invite can see it"
                                : "Public: everyone on this computer can see it and fork it",
                   repo->isPrivate ? "Click twice to make it public" : "Click twice to make it private", NULL);
        }
        Color pillColor = armedButtonId == 95 ? UI_DANGER : repo->isPrivate ? (Color){110, 80, 20, 255} : UI_BUTTON;
        DrawRectangleRounded(visibilityPill, 0.5f, 6, visibilityHovered ? ColorBrightness(pillColor, 0.15f) : pillColor);
        DrawText(armedButtonId == 95 ? askText : visibilityText, (int)visibilityPill.x + 10, (int)visibilityPill.y + 3, FONT_SIZE, repo->isPrivate || armedButtonId == 95 ? UI_TEXT : UI_MUTED);
        if (WasClicked(visibilityPill)) {
            if (armedButtonId != 95) { armedButtonId = 95; armedSeconds = 3; }
            else {
                armedButtonId = 0;
                repo->isPrivate = !repo->isPrivate;
                if (repo->isPrivate)               /* people who can't see it any more don't keep a star on it */
                    for (int i = repo->starCount - 1; i >= 0; i--) if (!CanSee(repo, repo->starredBy[i])) RemoveStar(repo, repo->starredBy[i]);
                MarkDirty();
                Toast(repo->isPrivate ? "Now private: only you, collaborators and people you invite can see it"
                                   : "Now public: everyone can see it and fork it");
            }
        }
    } else {
        DrawRectangleRounded(visibilityPill, 0.5f, 6, repo->isPrivate ? (Color){110, 80, 20, 255} : UI_BUTTON);
        DrawText(visibilityText, (int)visibilityPill.x + 10, (int)visibilityPill.y + 3, FONT_SIZE, repo->isPrivate ? UI_TEXT : UI_MUTED);
    }
    pill = visibilityPill;

    /* next to the badge: how many people have forked this */
    int forkCount = ForkCount(repo), infoX = (int)(pill.x + pill.width + 16);
    const char *infoText = TextFormat("%d branch%s%s", repo->branchCount, repo->branchCount == 1 ? "" : "es",
                                   forkCount ? TextFormat("   %d fork%s", forkCount, forkCount == 1 ? "" : "s") : "");
    if (repo->branchCount && screenWidth - 570 - infoX > MeasureText(infoText, FONT_SIZE)) DrawText(infoText, infoX, y + 5, FONT_SIZE, UI_MUTED);

    /* under the name: "forked from owner / name" (click to open the original), then the description */
    int descX = x, descMaxWidth = screenWidth - 400;
    if (repo->isFork) {
        int upstream = UpstreamIndex(repo);
        bool upstreamHidden = upstream >= 0 && !CanSee(&repos[upstream], currentUserId);
        if (upstreamHidden) upstream = -1;         /* private now: no link, no name */
        const char *prefix = "forked from ";
        const char *sourceName = upstreamHidden ? "a private repository" : TextFormat("%s / %s", UserName(repo->forkOwner), repo->forkName);
        int prefixWidth = MeasureText(prefix, FONT_SIZE), sourceWidth = MeasureText(sourceName, FONT_SIZE);
        DrawText(prefix, descX, y + 40, FONT_SIZE, UI_MUTED);
        DrawText(sourceName, descX + prefixWidth, y + 40, FONT_SIZE, upstream >= 0 ? UI_LINK : UI_MUTED);
        Rectangle hit = {(float)(descX + prefixWidth), (float)y + 36, (float)sourceWidth, FONT_SIZE + 8};
        if (upstream >= 0 && IsHovered(hit)) { wantedCursor = MOUSE_CURSOR_POINTING_HAND; DrawLine((int)hit.x, y + 41 + FONT_SIZE, (int)(hit.x + hit.width), y + 41 + FONT_SIZE, UI_LINK); }
        if (upstream >= 0 && WasClicked(hit)) { OpenRepo(upstream); return true; }
        const char *gap = "   |   ";
        DrawText(gap, descX + prefixWidth + sourceWidth, y + 40, FONT_SIZE, UI_MUTED);
        descX += prefixWidth + sourceWidth + MeasureText(gap, FONT_SIZE);
        descMaxWidth -= descX - x;
    }
    if (descMaxWidth > 40) DrawTextFit(repo->description[0] ? repo->description : "No description", descX, y + 40, FONT_SIZE, descMaxWidth, UI_MUTED);

    bool starred = HasStarred(repo, currentUserId);
    if (Button((Rectangle){(float)screenWidth - 170, (float)y, 150, 38},
               TextFormat("%s  %d", starred ? "Starred" : "Star", repo->starCount),
               starred ? (Color){110, 80, 20, 255} : UI_BUTTON)) {
        ToggleStar(repo, currentUserId);
        MarkDirty();
    }
    codeButtonRect = (Rectangle){(float)screenWidth - (ownRepo ? 550 : 360), (float)y, 180, 38};
    if (Button(codeButtonRect, codeMenuOpen ? "Code  ^" : "Code  v", UI_GREEN)) { codeMenuOpen = !codeMenuOpen; branchMenuOpen = false; }
    if (!ownRepo) {                    /* you can't fork your own repository - you already own it */
        int myFork = FindForkOf(currentUserId, currentRepo);
        if (Button((Rectangle){(float)screenWidth - 550, (float)y, 180, 38},
                   myFork >= 0 ? "Your fork" : TextFormat("Fork  %d", ForkCount(repo)), UI_BUTTON)) {
            TryForkRepo(currentRepo);
            return true;
        }
    }
    if (ownRepo && ConfirmButton((Rectangle){(float)screenWidth - 360, (float)y, 180, 38}, "Delete repo", "Really delete?", 1)) {
        int i = currentRepo;
        GoHome();
        DeleteRepo(i);
        MarkDirty();
        Toast("Repository deleted");
        return true;
    }

    if (HeaderPie(repo, (float)screenWidth - 20, 150)) { Unfocus(); repoTab = TAB_PEOPLE; screen = SCREEN_REPO; }

    int tabX = 20;
    for (int tabIndex = 0; tabIndex < 4; tabIndex++) {
        const char *label = tabIndex == 0 ? TextFormat("Code  %d", ShownFileCount(repo))
                          : tabIndex == 1 ? TextFormat("Commits  %d", repo->commitCount)
                          : tabIndex == 2 ? TextFormat("Issues  %d", OpenIssueCount(repo))
                                   : TextFormat("People  %d", 1 + repo->collaboratorCount);
        int tabWidth = MeasureText(label, FONT_SIZE) + 28;
        Rectangle tabRect = {(float)tabX, 132, (float)tabWidth, 38};
        bool selected = (tabIndex == activeTab);
        if (IsHovered(tabRect)) {
            wantedCursor = MOUSE_CURSOR_POINTING_HAND;
            if (!selected) DrawRectangleRounded((Rectangle){(float)tabX, 134, (float)tabWidth, 30}, 0.3f, 6, UI_HOVER);
        }
        DrawText(label, tabX + 14, 140, FONT_SIZE, selected ? UI_TEXT : UI_MUTED);
        if (selected) DrawRectangle(tabX, 167, tabWidth, 3, accentColor);
        if (WasClicked(tabRect)) { Unfocus(); repoTab = (Tab)tabIndex; screen = SCREEN_REPO; }
        tabX += tabWidth + 6;
    }
    DrawLine(0, 170, screenWidth, 170, UI_BORDER);
    return false;
}
