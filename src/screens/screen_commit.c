/*
 * screen_commit.c - one commit
 *
 * Its details, the files it changed, and each file's changes line by line.
 */

#include "screen_commit.h"
#include "animation.h"
#include "app_state.h"
#include "diff.h"
#include "downloads.h"
#include "drafts.h"
#include "editor.h"
#include "git_commit.h"
#include "model.h"
#include "navigation.h"
#include "previews.h"
#include "repo.h"
#include "sha1.h"
#include "ui_core.h"
#include "uploads.h"
#include "users.h"
#include "util.h"
#include "widgets.h"

static void DrawDiff(Rectangle area)
{
    DrawRectangleRec(area, UI_BG);
    DrawRectangleLinesEx(area, 1, UI_BORDER);
    ScrollArea(area, &diffScroll, diffOpCount * LINE_HEIGHT + 16);
    if (diffOpCount == 0) { DrawText("(empty file)", (int)area.x + 16, (int)area.y + 12, FONT_SIZE, UI_MUTED); return; }

    BeginScissorMode((int)area.x + 1, (int)area.y + 1, (int)area.width - 2, (int)area.height - 2);
    for (int i = 0; i < diffOpCount; i++) {
        float y = area.y + 8 + i * LINE_HEIGHT - diffScroll;
        if (y > area.y + area.height) break;
        if (y + LINE_HEIGHT < area.y) continue;
        DiffOp *op = &diffOps[i];
        Color background = op->kind == '+' ? (Color){46, 160, 67, 45} : op->kind == '-' ? (Color){248, 81, 73, 45} : BLANK;
        Color foreground = op->kind == '+' ? UI_GREEN_TEXT : op->kind == '-' ? UI_RED_TEXT : UI_TEXT;
        DrawRectangle((int)area.x, (int)y - 2, (int)area.width, LINE_HEIGHT, background);
        if (op->oldLine >= 0) DrawText(TextFormat("%d", op->oldLine + 1), (int)area.x + 10, (int)y, FONT_SIZE, UI_MUTED);
        if (op->newLine >= 0) DrawText(TextFormat("%d", op->newLine + 1), (int)area.x + 62, (int)y, FONT_SIZE, UI_MUTED);
        char sign[2] = {op->kind, 0};
        DrawText(sign, (int)area.x + 118, (int)y, FONT_SIZE, foreground);
        LineRef line = op->newLine >= 0 ? diffNewLines[op->newLine] : diffOldLines[op->oldLine];
        DrawText(DisplaySlice(line.start, line.length), (int)area.x + 140, (int)y, FONT_SIZE, op->kind == ' ' ? UI_TEXT : foreground);
    }
    EndScissorMode();
    DrawScrollbar(area, diffScroll, diffOpCount * LINE_HEIGHT + 16);
}

/* A non-text file in a commit: no lines to compare, so show the sizes,
   and for pictures the old and new versions side by side. */
static void DrawBinaryChange(Repo *repo, Change *change, Rectangle area)
{
    DrawRectangleRec(area, UI_BASE);
    DrawRectangleLinesEx(area, 1, UI_BORDER);
    int oldLength = BlobLength(repo, change->oldBlob), newLength = BlobLength(repo, change->newBlob);
    const char *what = change->kind == 'A' ? TextFormat("Added   |   %s", FormatSize(newLength))
                     : change->kind == 'D' ? TextFormat("Deleted   |   was %s", FormatSize(oldLength))
                                       : TextFormat("Changed   |   %s  ->  %s", FormatSize(oldLength), FormatSize(newLength));
    DrawText(what, (int)area.x + 16, (int)area.y + 14, FONT_SIZE, UI_TEXT);
    Rectangle box = {area.x + 16, area.y + 54, area.width - 32, area.height - 70};
    int oldSlot = change->oldBlob >= 0 ? GetPreviewSlot(BlobText(repo, change->oldBlob), oldLength) : -1;
    int newSlot = change->newBlob >= 0 ? GetPreviewSlot(BlobText(repo, change->newBlob), newLength) : -1;
    if (change->kind == 'M' && oldSlot >= 0 && newSlot >= 0) {
        float halfWidth = (box.width - 20) / 2;
        DrawText("Before", (int)box.x, (int)box.y, FONT_SIZE, UI_RED_TEXT);
        DrawText("After", (int)(box.x + halfWidth + 20), (int)box.y, FONT_SIZE, UI_GREEN_TEXT);
        DrawPicture(oldSlot, (Rectangle){box.x, box.y + 30, halfWidth, box.height - 30});
        DrawPicture(newSlot, (Rectangle){box.x + halfWidth + 20, box.y + 30, halfWidth, box.height - 30});
    } else if (newSlot >= 0 || oldSlot >= 0) {
        DrawPicture(newSlot >= 0 ? newSlot : oldSlot, box);
    } else {
        DrawText("This isn't a text file, so there are no lines to compare.", (int)box.x, (int)box.y, FONT_SIZE, UI_MUTED);
    }
}

/* Draws the commit page for the commit open in repo. */
void ScreenCommit(Repo *repo)
{
    Commit *commit = &repo->commits[currentCommit];

    if (Button((Rectangle){20, 186, 170, 38}, "All commits", UI_BUTTON)) {  /* (Back, at the top, returns to wherever you came from) */
        Unfocus();
        screen = SCREEN_REPO; repoTab = TAB_COMMITS;
        return;
    }
    if (Button((Rectangle){(float)screenWidth - (CanEdit(repo) ? 590 : 270), 186, 250, 38}, "Download this version", UI_BUTTON)) DownloadCommit(repo, currentCommit);
    if (CanEdit(repo) &&
        ConfirmButton((Rectangle){(float)screenWidth - 330, 186, 310, 38}, "Restore files from here", "Click again to confirm", 2)) {
        RestoreCommit(repo, currentCommit);
        SelectFileName("");
        MarkDirty();
        Toast(TextFormat("Files restored to %s - commit them to keep this version", commit->id));
        Unfocus();
        screen = SCREEN_REPO;
        repoTab = TAB_CODE;
        return;
    }

    DrawTextFit(commit->message, 20, 238, 30, screenWidth - 40, UI_TEXT);
    /* who wrote it according to git, if that isn't simply the Mini GitHub user */
    CommitInfo info;
    bool parsed = ParseCommit(commit->raw, commit->rawLen, &info);
    const char *who = CommitAuthorName(commit);
    const char *meta = TextFormat("%s committed %s   |   %s %s   |   %d file%s changed   ",
                                  who, TimeAgo(commit->time), commit->parentCount > 1 ? "merge" : "commit", commit->id,
                                  commitChangeCount, commitChangeCount == 1 ? "" : "s");
    DrawText(meta, 20, 276, FONT_SIZE, UI_MUTED);
    int statsX = 20 + MeasureText(meta, FONT_SIZE);
    if (IsHovered((Rectangle){20, 272, (float)statsX - 20, 28})) {  /* the details git has */
        char hex[41], parent1[41] = "", parent2[41] = "";
        ShaToHex(commit->sha, hex);
        if (commit->parentCount > 0) ShaToHex(repo->commits[commit->parents[0]].sha, parent1);
        if (commit->parentCount > 1) ShaToHex(repo->commits[commit->parents[1]].sha, parent2);
        SetTooltip(TextFormat("commit %s", hex),
               parsed ? TextFormat("Author: %.60s <%.60s>", info.authorName, info.authorEmail) : NULL,
               commit->parentCount > 1 ? TextFormat("Merges %.7s and %.7s", parent1, parent2) : commit->parentCount ? TextFormat("Built on %.7s", parent1) : "The first commit");
    }
    const char *plus = TextFormat("+%d ", totalAdds);
    DrawText(plus, statsX, 276, FONT_SIZE, UI_GREEN_TEXT);
    DrawText(TextFormat("-%d", totalDels), statsX + MeasureText(plus, FONT_SIZE) + 4, 276, FONT_SIZE, UI_RED_TEXT);

    float top = 314, bottom = (float)screenHeight - 20;

    /* left: changed files */
    Rectangle leftPanel = {20, top, 260, bottom - top};
    DrawRectangleRec(leftPanel, UI_PANEL);
    DrawRectangleLinesEx(leftPanel, 1, UI_BORDER);
    DrawTextBold("Files changed", (int)leftPanel.x + 14, (int)top + 12, FONT_SIZE, UI_TEXT);
    Rectangle listArea = {leftPanel.x + 1, top + 44, leftPanel.width - 2, leftPanel.height - 46};
    float rowHeight = 30;
    ScrollArea(listArea, &changeListScroll, commitChangeCount * rowHeight);
    BeginScissorMode((int)listArea.x, (int)listArea.y, (int)listArea.width, (int)listArea.height);
    for (int i = 0; i < commitChangeCount; i++) {
        Rectangle row = {listArea.x, listArea.y + i * rowHeight - changeListScroll, listArea.width, rowHeight};
        bool hovered = IsHovered(listArea) && IsHovered(row);
        if (i == currentChange) DrawRectangleRec(row, (Color){56, 139, 253, 40});
        else DrawHoverFill(row, AnimationKey("change row", (float)i, 0), hovered);
        if (hovered) wantedCursor = MOUSE_CURSOR_POINTING_HAND;
        Color kindColor = commitChanges[i].kind == 'A' ? UI_GREEN_TEXT : commitChanges[i].kind == 'D' ? UI_RED_TEXT : UI_YELLOW;
        char kindText[2] = {commitChanges[i].kind, 0};
        DrawText(kindText, (int)row.x + 14, (int)row.y + 5, FONT_SIZE, kindColor);
        DrawTextFit(commitChanges[i].name, (int)row.x + 38, (int)row.y + 5, FONT_SIZE, (int)row.width - 52, UI_TEXT);
        if (IsHovered(listArea) && WasClicked(row)) SelectChange(i);
    }
    EndScissorMode();

    /* right: the diff */
    Rectangle rightPanel = {300, top, (float)screenWidth - 320, bottom - top};
    if (currentChange < 0) {
        DrawRectangleRec(rightPanel, UI_PANEL);
        DrawRectangleLinesEx(rightPanel, 1, UI_BORDER);
        DrawText("This commit didn't change any files.", (int)rightPanel.x + 16, (int)rightPanel.y + 16, FONT_SIZE, UI_MUTED);
        return;
    }
    Change *change = &commitChanges[currentChange];
    Rectangle fileHeader = {rightPanel.x, rightPanel.y, rightPanel.width, 42};
    DrawRectangleRec(fileHeader, UI_PANEL);
    DrawRectangleLinesEx(fileHeader, 1, UI_BORDER);
    DrawTextBold(change->name, (int)fileHeader.x + 14, (int)fileHeader.y + 11, FONT_SIZE, UI_TEXT);
    const char *kindLabel = change->kind == 'A' ? "added" : change->kind == 'D' ? "deleted" : "modified";
    DrawText(kindLabel, (int)fileHeader.x + 30 + MeasureText(change->name, FONT_SIZE), (int)fileHeader.y + 11, FONT_SIZE, UI_MUTED);
    const char *addsText = change->binary ? "binary" : TextFormat("+%d  -%d", change->adds, change->dels);
    DrawText(addsText, (int)(fileHeader.x + fileHeader.width) - 14 - MeasureText(addsText, FONT_SIZE), (int)fileHeader.y + 11, FONT_SIZE, UI_MUTED);
    Rectangle area = {rightPanel.x, rightPanel.y + 42, rightPanel.width, rightPanel.height - 42};
    if (change->binary) DrawBinaryChange(repo, change, area);
    else            DrawDiff(area);
}
