/*
 * git_paths.h - file paths as git sees them
 *
 * One shared copy of each path (so commits can compare paths by pointer),
 * the rules for a usable path, and git's file modes.
 */

#ifndef MINIGITHUB_GIT_PATHS_H
#define MINIGITHUB_GIT_PATHS_H

#include "common.h"

/* The one shared copy of this path (made the first time), or NULL if out of memory. */
const char *InternPath(const char *path);

/* Is this a sensible path for a file: "a/b.txt" yes; "", "/a", "a//b", "a/../b", "a/" no. */
bool IsValidPath(const char *path);
enum { MODE_FILE, MODE_EXEC, MODE_LINK };

/* The mode git writes for a file: "100644", "100755" or "120000". */
const char *GitModeText(int mode);

#endif
