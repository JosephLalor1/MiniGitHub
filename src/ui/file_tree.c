/*
 * file_tree.c - the file list on the Code tab
 *
 * Files shown as folders that open and close.
 */

#include "file_tree.h"
#include "animation.h"
#include "app_state.h"
#include "editor.h"
#include "git_paths.h"
#include "languages.h"
#include "model.h"
#include "repo_view.h"
#include "tech_stack.h"
#include "ui_core.h"
#include "users.h"
#include "util.h"
#include "widgets.h"

/* ---------- the file list, as folders ----------
   Paths like "src/ui/button.c" are shown as a tree: a "src" folder with a
   "ui" folder inside. Folders start closed (big projects stay tidy) and
   open with a click; the folders holding the open file, and any folder
   with changes of yours in it, are opened for you. */
static const Repo  *openFoldersRepo;
static const char  *openFolders[512];
static int          openFolderCount;

static int FolderOpenIndex(const char *path)
{
    for (int index = 0; index < openFolderCount; index++) if (openFolders[index] == path) return index;
    return -1;
}
static void OpenFolder(const char *path)
{
    if (path && FolderOpenIndex(path) < 0 && openFolderCount < 512) openFolders[openFolderCount++] = path;
}
static void ToggleFolder(const char *path)
{
    int index = FolderOpenIndex(path);
    if (index >= 0) openFolders[index] = openFolders[--openFolderCount];
    else OpenFolder(path);
}
/* Opens every folder above a file, e.g. "src" and "src/ui" for "src/ui/button.c". */
static void RevealPath(const char *name)
{
    char prefix[PATH_LEN];
    snprintf(prefix, sizeof prefix, "%s", name);
    for (char *slash = strchr(prefix, '/'); slash; slash = strchr(slash + 1, '/')) { *slash = 0; OpenFolder(InternPath(prefix)); *slash = '/'; }
}

typedef struct { const char *name; int nameLength; int depth; int viewIndex; bool isFolder; const char *path; int endRow; char status; } FileRow;
static FileRow *fileRows;
static int      fileRowCount, fileRowCapacity;

/* Turns the (sorted) view into rows: a row for each folder, then its contents. */
static void BuildFileRows(void)
{
    fileRowCount = 0;
    int folderRows[64], depth = 0;                   /* rows of the folders we're inside */
    char folderPath[PATH_LEN];
    for (int i = 0; i <= viewFileCount; i++) {
        const char *path = i < viewFileCount ? viewFiles[i].name : "";
        /* how many of the current folders this path is still inside */
        int stillInside = 0;
        while (stillInside < depth) {
            FileRow *row = &fileRows[folderRows[stillInside]];
            size_t pathLength = strlen(row->path);
            if (i < viewFileCount && strncmp(path, row->path, pathLength) == 0 && path[pathLength] == '/') stillInside++;
            else break;
        }
        while (depth > stillInside) fileRows[folderRows[--depth]].endRow = fileRowCount;
        if (i == viewFileCount) break;
        /* new folders on the way to this file */
        const char *segment = path;
        for (int level = 0; level < stillInside; level++) segment = strchr(segment, '/') + 1;
        for (const char *slash = strchr(segment, '/'); slash && depth < 64; slash = strchr(segment, '/')) {
            fileRows = GrowArray(fileRows, &fileRowCapacity, fileRowCount + 2, sizeof(FileRow));
            if (fileRowCount + 2 > fileRowCapacity) return;
            snprintf(folderPath, sizeof folderPath, "%.*s", (int)(slash - path), path);
            FileRow *row = &fileRows[fileRowCount];
            memset(row, 0, sizeof *row);
            row->name = segment; row->nameLength = (int)(slash - segment); row->depth = depth; row->viewIndex = i; row->isFolder = true;
            row->path = InternPath(folderPath);
            if (!row->path) return;
            folderRows[depth++] = fileRowCount++;
            segment = slash + 1;
        }
        fileRows = GrowArray(fileRows, &fileRowCapacity, fileRowCount + 1, sizeof(FileRow));
        if (fileRowCount + 1 > fileRowCapacity) return;
        FileRow *row = &fileRows[fileRowCount++];
        memset(row, 0, sizeof *row);
        row->name = segment; row->nameLength = (int)strlen(segment); row->depth = depth; row->viewIndex = i;
        /* folders show the "strongest" change inside them: conflict, then any change */
        char status = viewFiles[i].status;
        for (int level = 0; level < depth && status; level++) {
            FileRow *folder = &fileRows[folderRows[level]];
            if (folder->status != 'C') folder->status = status == 'C' ? 'C' : 'M';
        }
    }
}

/* File list on the left: the latest commit, plus your own changes if you can edit. */
void DrawFileList(Repo *repo, Rectangle area)
{
    bool canEdit = CanEdit(repo);
    BuildView(repo, canEdit ? currentUserId : -1);
    if (openFoldersRepo != repo) { openFoldersRepo = repo; openFolderCount = 0; }
    BuildFileRows();
    for (int i = 0; i < fileRowCount; i++)           /* open folders with your changes in them */
        if (fileRows[i].isFolder && fileRows[i].status) OpenFolder(fileRows[i].path);
    if (selectedFile[0]) RevealPath(selectedFile);

    /* which rows show: everything inside a closed folder is hidden */
    static int *visibleRows;
    static int visibleCapacity;
    visibleRows = GrowArray(visibleRows, &visibleCapacity, fileRowCount + 1, sizeof(int));
    int visibleCount = 0;
    for (int i = 0; i < fileRowCount && visibleCapacity > fileRowCount; i++) {
        visibleRows[visibleCount++] = i;
        if (fileRows[i].isFolder && FolderOpenIndex(fileRows[i].path) < 0) i = fileRows[i].endRow - 1;
    }

    float rowHeight = 30;
    ScrollArea(area, &fileListScroll, visibleCount * rowHeight);
    if (!viewFileCount) DrawText(canEdit ? "No files yet" : "Nothing committed yet", (int)area.x + 14, (int)area.y + 6, FONT_SIZE, UI_MUTED);
    BeginScissorMode((int)area.x, (int)area.y, (int)area.width, (int)area.height);
    for (int k = 0; k < visibleCount; k++) {
        FileRow *fileRow = &fileRows[visibleRows[k]];
        Rectangle row = {area.x, area.y + k * rowHeight - fileListScroll, area.width, rowHeight};
        if (row.y > area.y + area.height || row.y + rowHeight < area.y) continue;
        bool hovered = IsHovered(area) && IsHovered(row);
        int x = (int)row.x + 10 + fileRow->depth * 14;
        const char *label = TextFormat("%.*s", fileRow->nameLength, fileRow->name);
        if (fileRow->isFolder) {
            bool isOpen = FolderOpenIndex(fileRow->path) >= 0;
            DrawHoverFill(row, AnimationKey(fileRow->path, 1, 0), hovered);
            if (hovered) wantedCursor = MOUSE_CURSOR_POINTING_HAND;
            float cx = (float)x + 7, cy = row.y + 15;
            if (isOpen) DrawTriangle((Vector2){cx - 5, cy - 3}, (Vector2){cx, cy + 4}, (Vector2){cx + 5, cy - 3}, UI_MUTED);
            else      DrawTriangle((Vector2){cx - 3, cy - 5}, (Vector2){cx - 3, cy + 5}, (Vector2){cx + 4, cy}, UI_MUTED);
            DrawTextFit(label, x + 20, (int)row.y + 5, FONT_SIZE, (int)(row.x + row.width) - x - 56, UI_TEXT);
            if (fileRow->status) DrawCircle((int)(row.x + row.width - 22), (int)row.y + 15, 4, fileRow->status == 'C' ? UI_ORANGE : UI_YELLOW);
            if (IsHovered(area) && WasClicked(row)) ToggleFolder(fileRow->path);
            continue;
        }
        ViewFile *viewFile = &viewFiles[fileRow->viewIndex];
        if (strcmp(viewFile->name, selectedFile) == 0) DrawRectangleRec(row, (Color){56, 139, 253, 40});
        else DrawHoverFill(row, AnimationKey(viewFile->name, 2, 0), hovered);
        if (hovered) wantedCursor = MOUSE_CURSOR_POINTING_HAND;
        bool deleted = ViewDeleted(viewFile);
        int lang = deleted ? LANG_TEXT : ViewFileLang(repo, viewFile, NULL);
        if (lang >= 0 && lang < LANG_FIRST_NONCODE) DrawCircle(x + 7, (int)row.y + 15, 5, LangColor(lang));
        else DrawCircleLines(x + 7, (int)row.y + 15, 4, UI_BORDER);         /* not code */
        int maxWidth = (int)(row.x + row.width) - x - 60;
        DrawTextFit(label, x + 20, (int)row.y + 5, FONT_SIZE, maxWidth, deleted ? UI_MUTED : UI_TEXT);
        if (deleted) { int labelWidth = MeasureText(label, FONT_SIZE); DrawLine(x + 20, (int)row.y + 15, x + 20 + (labelWidth < maxWidth ? labelWidth : maxWidth), (int)row.y + 15, UI_MUTED); }
        if (viewFile->status) {
            const char *statusText = viewFile->status == 'C' ? "!" : TextFormat("%c", viewFile->status);
            Color statusColor = viewFile->status == 'A' ? UI_GREEN_TEXT : viewFile->status == 'M' ? UI_YELLOW : viewFile->status == 'D' ? UI_RED_TEXT : UI_ORANGE;
            DrawTextBold(statusText, (int)(row.x + row.width - 28), (int)row.y + 5, FONT_SIZE, statusColor);
        }
        if (hovered && MeasureText(label, FONT_SIZE) > maxWidth) SetTooltip(viewFile->name, NULL, NULL);
        if (IsHovered(area) && WasClicked(row)) SelectFileName(viewFile->name);
    }
    EndScissorMode();
    DrawScrollbar(area, fileListScroll, visibleCount * rowHeight);
}
