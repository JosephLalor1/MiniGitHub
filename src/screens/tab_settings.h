/*
 * tab_settings.h - the Settings tab
 *
 * Only the owner sees it: who can see the repository (public / private),
 * and deleting it.
 */

#ifndef MINIGITHUB_TAB_SETTINGS_H
#define MINIGITHUB_TAB_SETTINGS_H

#include "common.h"
#include "model.h"

/* Draws a repository's Settings tab (anyone but the owner is sent to the Code tab). */
void TabSettings(Repo *repo);

#endif
