/*
 * uploads.c - uploading files from your computer
 *
 * Files are staged first so you can check what each one adds or replaces,
 * then committed together.
 */

#include "uploads.h"
#include "app_state.h"
#include "drafts.h"
#include "git_commit.h"
#include "languages.h"
#include "model.h"
#include "navigation.h"
#include "repo.h"
#include "snapshot.h"
#include "ui_core.h"
#include "util.h"
#include "widgets.h"
#include "work_file.h"

/* ------------------------------------------------------------------ */
/* Uploading files                                                    */
/*                                                                    */
/* Files dropped onto the window (or picked by typing their path) are */
/* "staged": held in memory and listed so you can check them first.   */
/* "Commit changes" then saves them all as one commit, built on top   */
/* of the latest commit. Anything else you were in the middle of      */
/* editing stays as it was, still waiting for its own commit.         */
/* ------------------------------------------------------------------ */

StagedFile stagedFiles[MAX_STAGED];
int    stagedFileCount;
float  uploadScroll;

/* Forgets every staged file. */
void ClearStagedFiles(void)
{
    for (int i = 0; i < stagedFileCount; i++) free(stagedFiles[i].data);
    memset(stagedFiles, 0, sizeof stagedFiles);
    stagedFileCount = 0;
}

static int FindStagedFile(const char *name)
{
    for (int i = 0; i < stagedFileCount; i++) if (strcmp(stagedFiles[i].name, name) == 0) return i;
    return -1;
}

/* "512 bytes", "12.3 KB", "4.5 MB" */
const char *FormatSize(int bytes)
{
    static char texts[4][32];
    static int which;
    which = (which + 1) % 4;
    if (bytes < 1024)             snprintf(texts[which], 32, "%d byte%s", bytes, bytes == 1 ? "" : "s");
    else if (bytes < 1024 * 1024) snprintf(texts[which], 32, "%.1f KB", bytes / 1024.0);
    else                      snprintf(texts[which], 32, "%.1f MB", bytes / (1024.0 * 1024.0));
    return texts[which];
}

/* How many lines a text has (a last line without a newline counts too). */
int CountLines(const char *text, int length)
{
    if (length == 0) return 0;
    int newlines = 0;
    for (int i = 0; i < length; i++) if (text[i] == '\n') newlines++;
    return text[length - 1] == '\n' ? newlines : newlines + 1;
}

/* Removes quotes and spaces around a typed or pasted path
   ("Copy as path" on Windows adds quotes). */
static void CleanPath(const char *raw, char *path, int capacity)
{
    while (*raw == ' ' || *raw == '"' || *raw == '\'') raw++;
    snprintf(path, (size_t)capacity, "%s", raw);
    int length = (int)strlen(path);
    while (length > 0 && (path[length - 1] == ' ' || path[length - 1] == '"' || path[length - 1] == '\'')) path[--length] = 0;
}

/* Reads one file from your computer into the staging list.
   Returns NULL if it worked, or a message saying why not. */
const char *StageFile(const char *typedPath)
{
    static char message[TEXT_LEN];
    char path[1024];
    CleanPath(typedPath, path, sizeof path);
    if (!path[0])           return "Type or paste a file's path first";
    if (DirectoryExists(path)) return "Folders can't be uploaded - open the folder and drag the files inside";
    if (!FileExists(path))  return "Couldn't find a file at that path";
    const char *fileName = GetFileName(path);
    int size = GetFileLength(path);
    if (size < 0 || size > MAX_UPLOAD) {
        snprintf(message, sizeof message, "%.60s is too big - files can be up to %s", fileName, FormatSize(MAX_UPLOAD));
        return message;
    }

    char name[NAME_LEN];
    SanitizeName(fileName, name);
    if (!name[0]) snprintf(name, NAME_LEN, "file");
    int slot = FindStagedFile(name);
    if (slot < 0 && stagedFileCount >= MAX_STAGED) return "That's as many files as one upload can hold";

    char *data = NULL;
    int length = 0;
    if (size == 0) data = calloc(1, 1);                          /* an empty file */
    else {
        int bytesRead = 0;
        unsigned char *fileBytes = LoadFileData(path, &bytesRead);
        if (!fileBytes || bytesRead <= 0) { snprintf(message, sizeof message, "Couldn't open %.60s", fileName); return message; }
        data = malloc((size_t)bytesRead + 1);
        if (data) { memcpy(data, fileBytes, (size_t)bytesRead); data[bytesRead] = 0; length = bytesRead; }
        UnloadFileData(fileBytes);
    }
    if (!data) return "Out of memory";

    bool isBinary = LooksBinary(data, length);  /* kept exactly as it is, byte for byte, like git does */
    if (slot < 0) slot = stagedFileCount++;     /* same name again: replace it */
    else free(stagedFiles[slot].data);
    snprintf(stagedFiles[slot].name, PATH_LEN, "%s", name);
    stagedFiles[slot].data = data;
    stagedFiles[slot].length = length;
    stagedFiles[slot].binary = isBinary;
    LangCache cache;
    FillLangCache(&cache, data, length, isBinary);
    stagedFiles[slot].lang = LangFromCache(&cache, name);
    return NULL;
}

/* 'A' = a new file, 'M' = replaces a different version, '=' = same as the latest commit */
char StagedKind(Repo *repo, StagedFile *file)
{
    Commit *head = HeadCommit(repo);
    int index = head ? FindInCommit(head, file->name) : -1;
    if (index < 0) return 'A';
    Blob *blob = &repo->blobs[head->blobs[index]];
    return (blob->length == file->length && memcmp(blob->data, file->data, (size_t)file->length) == 0) ? '=' : 'M';
}

/* True if you've edited this file since the last commit and the upload would replace those edits. */
bool StagedReplacesEdits(Repo *repo, StagedFile *staged)
{
    WorkFile *file = DraftFind(repo, currentUserId, staged->name);
    if (!file || !DraftStatus(repo, file)) return false;
    return file->deleted || !(file->length == staged->length && memcmp(file->text, staged->data, (size_t)staged->length) == 0);
}

/* Saves the staged files as one commit. Returns NULL or a message. */
const char *DoUploadCommit(Repo *repo, const char *message)
{
    if (!stagedFileCount) return "Add some files first";
    Commit *head = HeadCommit(repo);
    int changedCount = 0, newFileCount = 0;
    for (int i = 0; i < stagedFileCount; i++) {
        char kind = StagedKind(repo, &stagedFiles[i]);
        if (kind != '=') changedCount++;
        if (kind == 'A') newFileCount++;
    }
    if (!changedCount)                      return "Nothing to commit - these are the same as the latest versions";
    if (repo->commitCount >= MAX_COMMITS) return "This repository has reached its commit limit";
    if ((head ? head->fileCount : 0) + newFileCount > MAX_FILES)
        return TextFormat("A repository can hold up to %d files", MAX_FILES);

    /* start from the latest commit, then put the uploaded files in */
    Snapshot snap;
    if (!SnapshotFromCommit(&snap, head)) return "Out of memory";
    for (int i = 0; i < stagedFileCount; i++) {
        StagedFile *file = &stagedFiles[i];
        if (StagedKind(repo, file) == '=') continue;
        int blob = StoreBlob(repo, file->data, file->length);
        if (blob < 0 || !SnapshotSet(&snap, file->name, blob, -1)) { SnapshotFree(&snap); return "This repository is out of storage space"; }
    }
    const char *clash = PathClash(snap.names, snap.count);
    if (clash) { SnapshotFree(&snap); return TextFormat("There's already a folder called %s", clash); }
    if (!FinishCommit(repo, &snap, message[0] ? message : "Add files via upload", -1)) return "Couldn't save the commit (out of memory?)";
    MarkDirty();

    /* the uploaded versions replace any uncommitted edits you had to those files */
    for (int i = 0; i < stagedFileCount; i++) DraftDropName(repo, currentUserId, stagedFiles[i].name);
    return NULL;
}

/* ---------------- the upload page ---------------- */
/* Opens the upload page for the open repository. */
void OpenUpload(void)
{
    Unfocus();
    uploadScroll = 0;
    TextFieldClear(&uploadPathField);
    TextFieldClear(&uploadMessageField);
    screen = SCREEN_UPLOAD;
}

/* Stages every path in a list (from a drop), and says how it went. */
void StagePaths(char **paths, int count)
{
    int added = 0;
    char error[TEXT_LEN] = "";
    for (int i = 0; i < count; i++) {
        const char *problem = StageFile(paths[i]);
        if (problem) snprintf(error, sizeof error, "%s", problem);
        else added++;
    }
    if (error[0] && added) Toast(TextFormat("Added %d file%s, but: %.100s", added, added == 1 ? "" : "s", error));
    else if (error[0])  Toast(error);
}

/* Draws the outline of a rectangle as dashes. */
void DrawDashedRect(Rectangle rect, Color color)
{
    for (float x = rect.x; x < rect.x + rect.width; x += 16) {
        float dashEnd = fminf(x + 8, rect.x + rect.width);
        DrawLineEx((Vector2){x, rect.y}, (Vector2){dashEnd, rect.y}, 2, color);
        DrawLineEx((Vector2){x, rect.y + rect.height}, (Vector2){dashEnd, rect.y + rect.height}, 2, color);
    }
    for (float y = rect.y; y < rect.y + rect.height; y += 16) {
        float dashEnd = fminf(y + 8, rect.y + rect.height);
        DrawLineEx((Vector2){rect.x, y}, (Vector2){rect.x, dashEnd}, 2, color);
        DrawLineEx((Vector2){rect.x + rect.width, y}, (Vector2){rect.x + rect.width, dashEnd}, 2, color);
    }
}

/* Goes back to the open repository's Code tab. */
void BackToCode(void) { Unfocus(); screen = SCREEN_REPO; repoTab = TAB_CODE; }
