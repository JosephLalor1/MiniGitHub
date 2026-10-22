/*
 * common.h - settings shared by every file
 *
 * The platform setup, the standard headers and raylib, and the program's
 * limits (how many users, files, commits...). Every header includes it first.
 */

#ifndef MINIGITHUB_COMMON_H
#define MINIGITHUB_COMMON_H

/* On Windows, rand_s() gives cryptographically secure random numbers.
   This must be defined before <stdlib.h> is included. */
#ifdef _WIN32
#define _CRT_RAND_S
#endif

#include "raylib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <ctype.h>
#include <time.h>
#include <math.h>

/* ------------------------------------------------------------------ */
/* Limits                                                             */
/* ------------------------------------------------------------------ */
#define MAX_USERS    32
#define MAX_REPOS    48
#define MAX_FILES    5000     /* files in one commit                           */
#define MAX_COMMITS  100000   /* commits in one repository                     */
#define MAX_BLOBS    2000000  /* different file versions in one repository     */
#define MAX_DRAFT    500      /* files one person can change before committing */
#define MAX_ISSUES   100
#define NAME_LEN     64
#define PATH_LEN     256      /* longest file path, e.g. "src/ui/button.c"     */
#define TEXT_LEN     160
#define MAX_UPLOAD   (10 * 1024 * 1024)   /* biggest file you can upload: 10 MB */
#define SAVE_FILE    "minigithub.dat"

#define BIO_LEN      150     /* longest bio allowed                           */
#define MIN_PASS     8       /* shortest password allowed                     */
#define PBKDF2_ITER  60000   /* hashing rounds: makes password guessing slow   */

#define FONT_SIZE 20    /* normal font size                 */
#define LINE_HEIGHT 24  /* row height in the editor / diffs */

#endif
