/*
 * editor.h - the built-in text editor
 *
 * Typing, moving the cursor, scrolling, line numbers, and one-click ways
 * to settle merge conflicts.
 */

#ifndef MINIGITHUB_EDITOR_H
#define MINIGITHUB_EDITOR_H

#include "common.h"
#include "model.h"

/* Where the line holding position pos starts. */
int LineStart(const char *text, int pos);

/* Which line (from 0) position pos is on. */
int LineNumberAt(const char *text, int pos);

/* The Code tab remembers the open file by name, so it stays open while files are added, deleted or
   committed around it. "" means "open the first one". */
void SelectFileName(const char *name);

/* Draws and runs the text editor for a file. Returns true if the text was changed. */
bool EditorWidget(WorkFile *file, Rectangle area, bool readOnly);

#endif
