/*
 * tab_code.h - the Code tab
 *
 * The file list, the editor (or a viewer), and committing your changes.
 */

#ifndef MINIGITHUB_TAB_CODE_H
#define MINIGITHUB_TAB_CODE_H

#include "common.h"
#include "model.h"

/* Throws away the editor's private copy of a committed file (after the commits change). */
void ForgetScratchFile(void);

/* Draws a repository's Code tab. */
void TabCode(Repo *repo);

#endif
