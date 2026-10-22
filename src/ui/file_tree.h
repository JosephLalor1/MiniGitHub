/*
 * file_tree.h - the file list on the Code tab
 *
 * Files shown as folders that open and close.
 */

#ifndef MINIGITHUB_FILE_TREE_H
#define MINIGITHUB_FILE_TREE_H

#include "common.h"
#include "model.h"

/* File list on the left: the latest commit, plus your own changes if you can edit. */
void DrawFileList(Repo *repo, Rectangle area);

#endif
