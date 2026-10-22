/*
 * screen_settings.h - the settings screen
 *
 * Profile (picture, colour, bio, theme), password & account, and git
 * (server status, email).
 */

#ifndef MINIGITHUB_SCREEN_SETTINGS_H
#define MINIGITHUB_SCREEN_SETTINGS_H

#include "common.h"
#include "model.h"

/* Makes the picture at path the user's profile picture, and says how it went. */
void UseAvatarPath(User *user, const char *path);

/* Draws the settings screen (whichever tab is chosen). */
void ScreenSettings(void);

#endif
