/*
 * uploads.h - uploading files from your computer
 *
 * Files are staged first so you can check what each one adds or replaces,
 * then committed together.
 */

#ifndef MINIGITHUB_UPLOADS_H
#define MINIGITHUB_UPLOADS_H

#include "common.h"
#include "model.h"

#define MAX_STAGED 200
typedef struct { char name[PATH_LEN]; char *data; int length; bool binary; int lang; } StagedFile;
extern StagedFile stagedFiles[MAX_STAGED];
extern int stagedFileCount;
extern float uploadScroll;

/* Forgets every staged file. */
void ClearStagedFiles(void);

/* "512 bytes", "12.3 KB", "4.5 MB" */
const char *FormatSize(int bytes);

/* How many lines a text has (a last line without a newline counts too). */
int CountLines(const char *text, int length);

/* Reads one file from your computer into the staging list. Returns NULL if it worked, or a message
   saying why not. */
const char *StageFile(const char *typedPath);

/* 'A' = a new file, 'M' = replaces a different version, '=' = same as the latest commit */
char StagedKind(Repo *repo, StagedFile *file);

/* True if you've edited this file since the last commit and the upload would replace those edits. */
bool StagedReplacesEdits(Repo *repo, StagedFile *staged);

/* Saves the staged files as one commit. Returns NULL or a message. */
const char *DoUploadCommit(Repo *repo, const char *message);

/* Opens the upload page for the open repository. */
void OpenUpload(void);

/* Stages every path in a list (from a drop), and says how it went. */
void StagePaths(char **paths, int count);

/* Draws the outline of a rectangle as dashes. */
void DrawDashedRect(Rectangle rect, Color color);

/* Goes back to the open repository's Code tab. */
void BackToCode(void);

#endif
