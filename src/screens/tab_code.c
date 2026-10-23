/*
 * tab_code.c - the Code tab
 *
 * The file list, the editor (or a viewer), and committing your changes.
 */

#include "tab_code.h"
#include "app_state.h"
#include "downloads.h"
#include "drafts.h"
#include "editor.h"
#include "file_tree.h"
#include "git_paths.h"
#include "git_server.h"
#include "languages.h"
#include "model.h"
#include "navigation.h"
#include "previews.h"
#include "repo.h"
#include "repo_header.h"
#include "repo_view.h"
#include "tech_stack.h"
#include "ui_core.h"
#include "uploads.h"
#include "users.h"
#include "util.h"
#include "widgets.h"
#include "work_file.h"

/* The bar above a file: its name, a tag like "modified", and where the cursor is
   (or the size, for a non-text file). rightGap leaves room for a button. */
static void DrawFileHeader(Rectangle bar, const char *name, const char *tag, Color tagColor, WorkFile *file, int rightGap, const char *langName)
{
    DrawRectangleRec(bar, UI_PANEL);
    DrawRectangleLinesEx(bar, 1, UI_BORDER);
    DrawTextBold(name, (int)bar.x + 14, (int)bar.y + 11, FONT_SIZE, UI_TEXT);
    if (tag) DrawText(tag, (int)bar.x + 30 + MeasureText(name, FONT_SIZE), (int)bar.y + 11, FONT_SIZE, tagColor);
    /* the details on the right: the long version, a shorter one if space is tight, just the language, or none */
    char full[128], brief[80], tiny[40];
    if (file->binary) {
        int slot = GetPreviewSlot(file->text, file->length);
        if (slot >= 0) snprintf(full, sizeof full, "%d x %d   |   %s", previewSlots[slot].width, previewSlots[slot].height, FormatSize(file->length));
        else        snprintf(full, sizeof full, "binary file   |   %s", FormatSize(file->length));
        snprintf(brief, sizeof brief, "%s", FormatSize(file->length));
        tiny[0] = 0;
    } else {
        int lineStart = LineStart(file->text, editor.cursor), lineCount = CountLines(file->text, file->length);  /* same count as the language breakdown */
        snprintf(full, sizeof full, "%s   |   Ln %d, Col %d   |   %d lines", langName, LineNumberAt(file->text, editor.cursor) + 1, editor.cursor - lineStart + 1, lineCount);
        snprintf(brief, sizeof brief, "%s   |   %d lines", langName, lineCount);
        snprintf(tiny, sizeof tiny, "%s", langName);
    }
    int leftEnd = (int)bar.x + 14 + MeasureText(name, FONT_SIZE) + (tag ? 16 + MeasureText(tag, FONT_SIZE) : 0) + 20;
    int right = (int)(bar.x + bar.width) - 14 - rightGap;
    const char *info = right - MeasureText(full, FONT_SIZE) >= leftEnd ? full : right - MeasureText(brief, FONT_SIZE) >= leftEnd ? brief
                     : tiny[0] && right - MeasureText(tiny, FONT_SIZE) >= leftEnd ? tiny : NULL;
    if (info) DrawText(info, right - MeasureText(info, FONT_SIZE), (int)bar.y + 11, FONT_SIZE, UI_MUTED);
}

/* A private copy of a committed file for the editor to work on. Nothing is
   saved while you only look or move the cursor; the moment you actually
   change something, the file is copied into your draft. */
static WorkFile    scratchFile;
static const Repo *scratchRepo;
static int         scratchBlob = -2;
static char        scratchName[PATH_LEN];
static WorkFile *GetScratchFile(Repo *repo, ViewFile *viewFile)
{
    if (scratchRepo != repo || scratchBlob != viewFile->blob || strcmp(scratchName, viewFile->name) != 0) {
        WorkFileSetBytes(&scratchFile, viewFile->data, viewFile->length);
        scratchRepo = repo;
        scratchBlob = viewFile->blob;
        snprintf(scratchName, PATH_LEN, "%s", viewFile->name);
        snprintf(scratchFile.name, PATH_LEN, "%s", viewFile->name);
    }
    return &scratchFile;
}
/* Throws away the editor's private copy of a committed file (after the commits change). */
void ForgetScratchFile(void) { scratchBlob = -2; }

/* Explains a conflict and offers the two ways out. Returns true if one was chosen. */
static bool ConflictBar(Repo *repo, ViewFile *viewFile, Rectangle bar)
{
    WorkFile *draftFile = viewFile->draftFile;
    int currentBlob = HeadBlobOf(repo, viewFile->name), changedBy = LastChangedBy(repo, viewFile->name);
    const char *byName = changedBy >= 0 ? (changedBy == currentUserId ? "You" : UserName(changedBy)) : "Someone";
    const char *reason = draftFile->deleted          ? TextFormat("You deleted this file, but %s changed it after that.", changedBy == currentUserId ? "you" : byName)
                    : currentBlob < 0             ? TextFormat("%s deleted this file after you started editing it.", byName)
                    : draftFile->baseBlob < 0         ? TextFormat("%s also added a file called %s.", byName, viewFile->name)
                                          : TextFormat("%s committed a newer version after you started editing it.", byName);
    DrawRectangleRec(bar, (Color){74, 40, 18, 255});
    DrawRectangleLinesEx(bar, 1, UI_ORANGE);
    float textWidth = bar.width - 32 - 300;
    DrawTextBold("Conflict - this file was changed by someone else too", (int)bar.x + 16, (int)bar.y + 10, FONT_SIZE, UI_ORANGE);
    DrawTextFit(reason, (int)bar.x + 16, (int)bar.y + 36, FONT_SIZE, (int)textWidth, UI_TEXT);
    if (Button((Rectangle){bar.x + bar.width - 300, bar.y + 14, 140, 36}, "Keep mine", UI_BUTTON)) {
        draftFile->baseBlob = currentBlob;   /* yours now counts as a change to the latest version */
        MarkDirty();
        Toast("Keeping your version - it will replace theirs when you commit");
        return true;
    }
    if (Button((Rectangle){bar.x + bar.width - 152, bar.y + 14, 140, 36}, "Use theirs", UI_BUTTON)) {
        DraftDropName(repo, currentUserId, viewFile->name);  /* forget your change; you see the latest version */
        MarkDirty();
        Toast("Switched to the latest version - your change was dropped");
        return true;
    }
    return false;
}

/* Would a new file called name clash with a folder of the same name (or the
   other way round, e.g. a new "notes.txt/a.md" when notes.txt is a file)? Uses the current view. */
static bool FolderOrFileClash(const char *name)
{
    size_t length = strlen(name);
    for (int i = 0; i < viewFileCount; i++) {
        if (ViewDeleted(&viewFiles[i])) continue;
        const char *path = viewFiles[i].name;
        size_t pathLength = strlen(path);
        if (pathLength > length && strncmp(path, name, length) == 0 && path[length] == '/') return true;  /* name is a folder already */
        if (length > pathLength && strncmp(name, path, pathLength) == 0 && name[pathLength] == '/') return true;  /* a file sits where a folder would go */
    }
    return false;
}

/* Adding a file starts as one button; pressing it asks whether to make a new
   file here or upload some from the computer, and making one asks for its name. */
enum { ADD_FILE_CLOSED, ADD_FILE_CHOOSING, ADD_FILE_NAMING };
static int         addFileStep = ADD_FILE_CLOSED;
static const Repo *addFileRepo;      /* the repository the steps above are for */

/* Draws a repository's Code tab. */
void TabCode(Repo *repo)
{
    bool canEdit = CanEdit(repo);
    float top = 186, bottom = (float)screenHeight - 20;
    top = DrawForkBar(repo, top);
    if (canEdit) DraftTidy(repo, currentUserId);
    BuildView(repo, canEdit ? currentUserId : -1);

    /* keep the open file, or open the first one */
    if (viewFileCount == 0) selectedFile[0] = 0;
    else if (!selectedFile[0] || ViewIndex(selectedFile) < 0) {
        int firstFile = 0;
        for (int i = 0; i < viewFileCount; i++) if (!ViewDeleted(&viewFiles[i])) { firstFile = i; break; }
        SelectFileName(viewFiles[firstFile].name);
    }

    /* ---- left: file list (with each file's language as a coloured dot) ---- */
    BuildLanguages(repo, canEdit ? currentUserId : -1);
    Rectangle leftPanel = {20, top, 260, bottom - top};
    DrawRectangleRec(leftPanel, UI_PANEL);
    DrawRectangleLinesEx(leftPanel, 1, UI_BORDER);
    BranchButton(repo, (Rectangle){leftPanel.x + 8, top + 6, leftPanel.width - 16, 32});  /* which branch the files are from */
    if (addFileRepo != repo) { addFileStep = ADD_FILE_CLOSED; addFileRepo = repo; }    /* a different repository: start closed */
    if (!canEdit) addFileStep = ADD_FILE_CLOSED;
    float addHeight = !canEdit ? 4 : addFileStep == ADD_FILE_CLOSED ? 104 : 156;      /* room kept at the bottom for adding files */
    DrawFileList(repo, (Rectangle){leftPanel.x + 1, top + 44, leftPanel.width - 2, leftPanel.height - 44 - addHeight});

    if (canEdit) {
        float bottomY = leftPanel.y + leftPanel.height - addHeight + 8, innerWidth = leftPanel.width - 24;
        DrawLine((int)leftPanel.x, (int)bottomY - 10, (int)(leftPanel.x + leftPanel.width), (int)bottomY - 10, UI_BORDER);
        if (addFileStep == ADD_FILE_CLOSED) {
            /* just the one button; it asks how next */
            if (Button((Rectangle){leftPanel.x + 12, bottomY, innerWidth, 38}, "Add file", UI_GREEN)) addFileStep = ADD_FILE_CHOOSING;
        } else {
            DrawText(addFileStep == ADD_FILE_CHOOSING ? "Add a file:" : "New file name:", (int)leftPanel.x + 14, (int)bottomY, FONT_SIZE, UI_MUTED);
            if (LinkText("Cancel", leftPanel.x + leftPanel.width - 14, bottomY, true)) {
                addFileStep = ADD_FILE_CLOSED;
                newFileField.active = false;
            }
        }
        if (addFileStep == ADD_FILE_CHOOSING) {
            if (Button((Rectangle){leftPanel.x + 12, bottomY + 28, innerWidth, 36}, "Create in Mini GitHub", UI_GREEN)) {
                addFileStep = ADD_FILE_NAMING;
                Unfocus();
                TextFieldClear(&newFileField);
                newFileField.active = true;
            }
            if (Button((Rectangle){leftPanel.x + 12, bottomY + 70, innerWidth, 36}, "Upload from computer", UI_BUTTON)) {
                addFileStep = ADD_FILE_CLOSED;
                OpenUpload();
                return;
            }
        } else if (addFileStep == ADD_FILE_NAMING) {
            bool addEnter = TextFieldWidget(&newFileField, (Rectangle){leftPanel.x + 12, bottomY + 28, innerWidth - 92, 36}, "new-file.txt");
            if (Button((Rectangle){leftPanel.x + leftPanel.width - 12 - 84, bottomY + 28, 84, 36}, "Create", UI_GREEN) || addEnter) {
                char name[PATH_LEN];
                SanitizePath(newFileField.text, name);
                BuildView(repo, currentUserId);
                int viewIndex = ViewIndex(name);
                if (!name[0])                                    Toast("Type a file name first, e.g. notes.txt or docs/notes.txt");
                else if (!IsValidPath(name))                       Toast("That isn't a usable file name");
                else if (FolderOrFileClash(name))                Toast("That name is already used by a folder (or a file on the way to it)");
                else if (viewIndex >= 0 && !ViewDeleted(&viewFiles[viewIndex]))   Toast("A file with that name already exists");
                else if (viewIndex < 0 && ShownFileCount(repo) >= MAX_FILES) Toast(TextFormat("A repository can hold up to %d files", MAX_FILES));
                else {
                    WorkFile *draftFile = DraftEdit(repo, currentUserId, name);
                    if (!draftFile) Toast("You're changing too many files at once - commit some first");
                    else {
                        draftFile->deleted = false;
                        WorkFileSetBytes(draftFile, "", 0);
                        SelectFileName(name);
                        Unfocus();
                        editor.focused = true;
                        TextFieldClear(&newFileField);
                        addFileStep = ADD_FILE_CLOSED;
                        MarkDirty();
                    }
                }
            }
        }
        int pendingChanges, pendingConflicts;
        DraftCounts(repo, currentUserId, &pendingChanges, &pendingConflicts);
        const char *statusText = pendingConflicts ? TextFormat("%d change%s, %d conflict%s", pendingChanges + pendingConflicts, pendingChanges + pendingConflicts == 1 ? "" : "s", pendingConflicts, pendingConflicts == 1 ? "" : "s")
                       : pendingChanges ? TextFormat("%d change%s to commit", pendingChanges, pendingChanges == 1 ? "" : "s")
                             : "All changes committed";
        float statusY = leftPanel.y + leftPanel.height - 34;
        DrawTextFit(statusText, (int)leftPanel.x + 14, (int)statusY, FONT_SIZE, (int)leftPanel.width - 28, pendingConflicts ? UI_ORANGE : pendingChanges ? UI_YELLOW : UI_MUTED);
    }

    /* ---- right: the language breakdown, then the editor (or a viewer) + bottom bar ---- */
    BuildLanguages(repo, canEdit ? currentUserId : -1);
    if (!(canEdit && DrawMergeBanner(repo, (Rectangle){300, top, (float)screenWidth - 320, 40})))
        DrawLanguageBar((Rectangle){300, top, (float)screenWidth - 320, 40});
    Rectangle rightPanel  = {300, top + 48, (float)screenWidth - 320, bottom - top - 48};
    Rectangle fileHeader  = {rightPanel.x, rightPanel.y, rightPanel.width, 42};
    Rectangle editorRect = {rightPanel.x, rightPanel.y + 42, rightPanel.width, rightPanel.height - 42 - 62};
    Rectangle commitBar  = {rightPanel.x, rightPanel.y + rightPanel.height - 48, rightPanel.width, 48};

    BuildView(repo, canEdit ? currentUserId : -1);
    int selectedIndex = ViewIndex(selectedFile);
    if (selectedIndex >= 0) {
        ViewFile *viewFile = &viewFiles[selectedIndex];
        Rectangle area = editorRect;
        if (viewFile->status == 'C') {
            if (ConflictBar(repo, viewFile, (Rectangle){editorRect.x, editorRect.y, editorRect.width, 64})) return;
            area.y += 64;
            area.height -= 64;
        }
        WorkFile shownFile = {0};                  /* what the header describes */
        snprintf(shownFile.name, PATH_LEN, "%s", viewFile->name);
        shownFile.text = (char *)viewFile->data; shownFile.length = viewFile->length; shownFile.binary = viewFile->binary;

        if (ViewDeleted(viewFile)) {
            DrawRectangleRec(area, UI_BASE);
            float centerX = area.x + area.width / 2, centerY = area.y + area.height / 2;
            const char *line1 = "You've deleted this file.";
            const char *line2 = "It will be removed from the repository when you commit.";
            DrawTextBold(line1, (int)centerX - MeasureText(line1, FONT_SIZE) / 2, (int)centerY - 50, FONT_SIZE, UI_TEXT);
            DrawText(line2, (int)centerX - MeasureText(line2, FONT_SIZE) / 2, (int)centerY - 22, FONT_SIZE, UI_MUTED);
            if (viewFile->status != 'C' && Button((Rectangle){centerX - 80, centerY + 16, 160, 40}, "Undo delete", UI_BUTTON)) {
                DraftDropName(repo, currentUserId, viewFile->name);
                MarkDirty();
                return;
            }
        } else if (viewFile->binary) {
            DrawBinaryView(viewFile->data, viewFile->length, area);
        } else if (viewFile->draftFile) {
            if (EditorWidget(viewFile->draftFile, area, false)) MarkDirty();
            shownFile = *viewFile->draftFile;
        } else {
            WorkFile *scratchCopy = GetScratchFile(repo, viewFile);
            if (EditorWidget(scratchCopy, area, !canEdit) && canEdit) {
                WorkFile *draftFile = DraftEdit(repo, currentUserId, viewFile->name);
                if (draftFile) { WorkFileSetBytes(draftFile, scratchCopy->text, scratchCopy->length); MarkDirty(); }
                else Toast("You're changing too many files at once - commit some first");
                ForgetScratchFile();
            }
            shownFile.text = scratchCopy->text; shownFile.length = scratchCopy->length;
        }
        DrawRectangleLinesEx(area, 1, UI_BORDER);

        const char *tag = NULL;
        Color tagColor = UI_MUTED;
        if (!canEdit)                  { Commit *head = HeadCommit(repo); tag = head ? TextFormat("as of commit %s", head->id) : NULL; }
        else if (viewFile->status == 'A')  { tag = "new file"; tagColor = UI_GREEN_TEXT; }
        else if (viewFile->status == 'M')  { tag = "modified"; tagColor = UI_YELLOW; }
        else if (viewFile->status == 'D')  { tag = "deleted";  tagColor = UI_RED_TEXT; }
        else if (viewFile->status == 'C')  { tag = "conflict"; tagColor = UI_ORANGE; }
        bool canDelete = canEdit && !ViewDeleted(viewFile), canDownload = !ViewDeleted(viewFile);
        DrawFileHeader(fileHeader, viewFile->name, tag, tagColor, &shownFile, (canDelete ? 104 : 0) + (canDownload ? 136 : 0), LangLabel(ViewFileLang(repo, viewFile, NULL)));
        if (canDownload && Button((Rectangle){fileHeader.x + fileHeader.width - (canDelete ? 106 : 0) - 136, fileHeader.y + 6, 128, 30}, "Download", UI_BUTTON))
            DownloadFile(viewFile->name, shownFile.text, shownFile.length);  /* exactly what's on screen */
        if (canDelete && ConfirmButton((Rectangle){fileHeader.x + fileHeader.width - 106, fileHeader.y + 6, 98, 30}, "Delete", "Sure?", 3)) {
            WorkFile *draftFile = DraftFind(repo, currentUserId, viewFile->name);
            if (draftFile && draftFile->baseBlob < 0 && HeadBlobOf(repo, viewFile->name) < 0) {  /* never committed: just remove it */
                DraftDropName(repo, currentUserId, viewFile->name);
                SelectFileName("");
            } else {
                draftFile = DraftEdit(repo, currentUserId, viewFile->name);
                if (draftFile) draftFile->deleted = true;
            }
            MarkDirty();
            Toast("File deleted - commit to record it in the history");
            return;
        }
    } else {
        Rectangle emptyArea = {rightPanel.x, rightPanel.y, rightPanel.width, rightPanel.height - 62};
        DrawRectangleRec(emptyArea, UI_PANEL);
        DrawRectangleLinesEx(emptyArea, 1, UI_BORDER);
        const char *message = canEdit ? "Pick a file on the left, add a new one, or upload some." : "This repository has no committed files yet.";
        DrawText(message, (int)(emptyArea.x + emptyArea.width / 2) - MeasureText(message, FONT_SIZE) / 2, (int)(emptyArea.y + emptyArea.height / 2) - 10, FONT_SIZE, UI_MUTED);
        if (canEdit && !repo->commitCount && gitPort) {    /* empty: say how to push a project that's already on this computer */
            const char *lines[3] = {"Or push a project you already have, from its folder:",
                                    TextFormat("git push %s main", CloneUrl(repo)),
                                    "(use master instead of main if that's what your branch is called)"};
            for (int i = 0; i < 3; i++) {
                int lineX = (int)(emptyArea.x + emptyArea.width / 2) - MeasureText(lines[i], FONT_SIZE) / 2;
                if (lineX < (int)emptyArea.x + 10) lineX = (int)emptyArea.x + 10;
                DrawTextFit(lines[i], lineX, (int)(emptyArea.y + emptyArea.height / 2) + 40 + i * 30, FONT_SIZE, (int)emptyArea.width - 20, i == 1 ? UI_TEXT : UI_MUTED);
            }
        }
    }

    if (!canEdit) {
        const char *ownerName = UserName(repo->owner);
        const char *note = repo->collaboratorCount
            ? TextFormat("Only %s and their collaborators can change this. Press Fork to get your own copy.", ownerName)
            : TextFormat("Only %s can change this. Press Fork to get your own copy.", ownerName);
        DrawTextFit(note, (int)commitBar.x, (int)commitBar.y + 14, FONT_SIZE, (int)commitBar.width, UI_MUTED);
        return;
    }
    bool enter = TextFieldWidget(&commitMessageField, (Rectangle){commitBar.x, commitBar.y + 4, commitBar.width - 156, 42}, "Describe your changes (the commit message)");
    if (Button((Rectangle){commitBar.x + commitBar.width - 146, commitBar.y + 4, 146, 42}, "Commit", UI_GREEN) || enter) {
        const char *error = DoCommit(repo, commitMessageField.text);
        if (error) Toast(error);
        else {
            Toast(TextFormat("Committed %s", HeadCommit(repo)->id));
            TextFieldClear(&commitMessageField);
            commitMessageField.active = false;
            ForgetScratchFile();
            MarkDirty();
        }
    }
}
