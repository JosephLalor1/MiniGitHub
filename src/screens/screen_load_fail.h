/*
 * screen_load_fail.h - when the save file can't be read
 *
 * Nothing is overwritten until you choose what to do.
 */

#ifndef MINIGITHUB_SCREEN_LOAD_FAIL_H
#define MINIGITHUB_SCREEN_LOAD_FAIL_H

#include "common.h"

extern bool quitNow;

/* Gets going after the save file loads: works out commit authors, starts the git server and shows
   sign-in (or create-account, if there are no accounts). */
void AfterLoad(void);

/* Draws the screen explaining that the save file couldn't be read, with the ways forward. */
void ScreenLoadFail(void);

#endif
