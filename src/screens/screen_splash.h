/*
 * screen_splash.h - the splash screen shown when the app opens
 *
 * The logo pops in over one of the animated themes (picked at random each
 * time), with the app's name and a progress bar in the theme's colour. Start
 * loading the save file on another thread, call SplashIntro (which plays
 * until loading is done), then SplashFinish.
 */

#ifndef MINIGITHUB_SCREEN_SPLASH_H
#define MINIGITHUB_SCREEN_SPLASH_H

#include "common.h"
#include "background.h"

/* Plays the start of the splash (the logo and name appearing) while loading - a job
   already started on another thread - is going, until both are done. */
void SplashIntro(BackgroundTask *loading);

/* Plays the rest of the splash after loading, until it has run its course or is
   skipped with a click or a key, then fades into the app. */
void SplashFinish(void);

#endif
