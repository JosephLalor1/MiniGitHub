/*
 * screen_upload.c - the upload page
 *
 * Drop or paste files, check them, and commit them.
 */

#include "screen_upload.h"
#include "app_state.h"
#include "editor.h"
#include "languages.h"
#include "model.h"
#include "previews.h"
#include "repo.h"
#include "ui_core.h"
#include "uploads.h"
#include "users.h"
#include "widgets.h"

/* Draws the upload page for repo. */
void ScreenUpload(Repo *repo)
{
    if (!CanEdit(repo)) { BackToCode(); return; }
    TextField *fields[] = {&uploadPathField, &uploadMessageField};
    CycleFocusOnTab(fields, 2);

    DrawTextBold(TextFormat("Upload files to %s", repo->name), 20, 190, 30, UI_TEXT);
    DrawText("Everything you upload here is saved together as one commit.", 20, 228, FONT_SIZE, UI_MUTED);

    /* the drop zone, with a path box for people who'd rather type */
    Rectangle zone = {20, 262, (float)screenWidth - 40, 116};
    DrawRectangleRec(zone, UI_PANEL);
    DrawDashedRect(zone, accentColor);
    const char *heading = "Drag files from your computer onto this window";
    DrawTextBold(heading, (int)(zone.x + zone.width / 2) - MeasureText(heading, FONT_SIZE) / 2, (int)zone.y + 16, FONT_SIZE, UI_TEXT);
    const char *orText = "or paste a path:";
    int orWidth = MeasureText(orText, FONT_SIZE);
    DrawText(orText, (int)zone.x + 24, (int)zone.y + 64, FONT_SIZE, UI_MUTED);
    float pathX = zone.x + 24 + orWidth + 12, pathWidth = zone.x + zone.width - 24 - 100 - pathX;
    bool enter = TextFieldWidget(&uploadPathField, (Rectangle){pathX, zone.y + 54, pathWidth, 40}, "e.g. C:/Users/jo/Documents/notes.txt");
    if (Button((Rectangle){zone.x + zone.width - 24 - 90, zone.y + 54, 90, 40}, "Add", UI_BUTTON) || enter) {
        const char *addError = StageFile(uploadPathField.text);
        if (addError) Toast(addError);
        else TextFieldClear(&uploadPathField);
    }

    /* the files waiting to be committed */
    int changedCount = 0;
    for (int i = 0; i < stagedFileCount; i++) if (StagedKind(repo, &stagedFiles[i]) != '=') changedCount++;
    DrawTextBold(stagedFileCount ? TextFormat("Files to upload (%d)", stagedFileCount) : "Files to upload", 20, 394, FONT_SIZE, UI_TEXT);

    float bottom = (float)screenHeight - 112;
    Rectangle listArea = {20, 424, (float)screenWidth - 40, bottom - 424 - 8};
    float rowHeight = 50;
    ScrollArea(listArea, &uploadScroll, stagedFileCount * rowHeight);
    if (!stagedFileCount) DrawText("Nothing added yet.", 20, 430, FONT_SIZE, UI_MUTED);
    int removeAt = -1;
    BeginScissorMode((int)listArea.x, (int)listArea.y, (int)listArea.width, (int)listArea.height);
    for (int i = 0; i < stagedFileCount; i++) {
        StagedFile *file = &stagedFiles[i];
        Rectangle row = {listArea.x, listArea.y + i * rowHeight - uploadScroll, listArea.width - 12, rowHeight - 6};
        if (row.y > listArea.y + listArea.height || row.y + row.height < listArea.y) continue;
        DrawRectangleRec(row, UI_PANEL);
        DrawRectangleLinesEx(row, 1, UI_BORDER);

        char kind = StagedKind(repo, file);
        const char *badge = kind == 'A' ? "New" : kind == 'M' ? "Replaces" : "Unchanged";
        Color badgeColor = kind == 'A' ? UI_GREEN : kind == 'M' ? (Color){150, 105, 20, 255} : UI_BUTTON;
        Rectangle pill = {row.x + 10, row.y + 9, 112, 26};
        DrawRectangleRounded(pill, 0.5f, 6, badgeColor);
        DrawText(badge, (int)(pill.x + pill.width / 2) - MeasureText(badge, FONT_SIZE) / 2, (int)pill.y + 3, FONT_SIZE, UI_TEXT);

        int textX = (int)row.x + 136;
        DrawTextBold(file->name, textX, (int)row.y + 12, FONT_SIZE, UI_TEXT);
        textX += MeasureText(file->name, FONT_SIZE) + 18;
        const char *kindText = file->binary ? (IsPictureBytes(file->data, file->length) ? "picture" : "binary file")
                                     : TextFormat("%s, %d line%s", LangLabel(file->lang), CountLines(file->data, file->length), CountLines(file->data, file->length) == 1 ? "" : "s");
        const char *detail = TextFormat("%s, %s", kindText, FormatSize(file->length));
        int room = (int)(row.x + row.width) - 120 - textX;
        DrawTextFit(detail, textX, (int)row.y + 12, FONT_SIZE, room, UI_MUTED);
        if (StagedReplacesEdits(repo, file)) {
            const char *warning = "   - replaces your uncommitted edits";
            int detailWidth = MeasureText(detail, FONT_SIZE);
            if (detailWidth + MeasureText(warning, FONT_SIZE) < room) DrawText(warning, textX + detailWidth, (int)row.y + 12, FONT_SIZE, UI_YELLOW);
        }
        if (ButtonEnabled((Rectangle){row.x + row.width - 106, row.y + 7, 96, 30}, "Remove", UI_BUTTON, IsHovered(listArea))) removeAt = i;
    }
    EndScissorMode();
    DrawScrollbar(listArea, uploadScroll, stagedFileCount * rowHeight);
    if (removeAt >= 0) {
        free(stagedFiles[removeAt].data);
        memmove(&stagedFiles[removeAt], &stagedFiles[removeAt + 1], (size_t)(stagedFileCount - removeAt - 1) * sizeof(StagedFile));
        stagedFileCount--;
        memset(&stagedFiles[stagedFileCount], 0, sizeof(StagedFile));
    }

    /* commit message and buttons */
    DrawLine(20, (int)bottom, screenWidth - 20, (int)bottom, UI_BORDER);
    DrawText("Commit message", 20, (int)bottom + 14, FONT_SIZE, UI_TEXT);
    if (stagedFileCount && !changedCount) DrawText("These are all the same as the latest versions - nothing to commit.",
                                          20 + MeasureText("Commit message", FONT_SIZE) + 24, (int)bottom + 14, FONT_SIZE, UI_YELLOW);
    bool msgEnter = TextFieldWidget(&uploadMessageField, (Rectangle){20, bottom + 44, (float)screenWidth - 40 - 360, 44}, "Add files via upload");
    bool cancelPressed = Button((Rectangle){(float)screenWidth - 350, bottom + 44, 130, 44}, "Cancel", UI_BUTTON);
    bool commitPressed = ButtonEnabled((Rectangle){(float)screenWidth - 210, bottom + 44, 190, 44}, "Commit changes", changedCount ? UI_GREEN : UI_BUTTON, changedCount > 0);
    if (cancelPressed) { BackToCode(); return; }
    if (commitPressed || (msgEnter && changedCount)) {
        char firstFile[PATH_LEN];
        snprintf(firstFile, PATH_LEN, "%s", stagedFiles[0].name);
        const char *error = DoUploadCommit(repo, uploadMessageField.text);
        if (error) { Toast(error); return; }
        Toast(TextFormat("Uploaded %d file%s in commit %s", changedCount, changedCount == 1 ? "" : "s", HeadCommit(repo)->id));
        ClearStagedFiles();
        MarkDirty();
        BackToCode();
        SelectFileName(firstFile);
    }
}
