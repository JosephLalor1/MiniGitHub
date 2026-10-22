/*
 * avatars.h - profile pictures
 *
 * Importing a PNG or JPG, cropping it to a round 192x192 picture kept in
 * the save file as base64, and drawing it - or a coloured circle with a
 * letter.
 */

#ifndef MINIGITHUB_AVATARS_H
#define MINIGITHUB_AVATARS_H

#include "common.h"
#include "model.h"

/* Lets go of the drawable copy of a user's picture (made again when next needed). */
void FreeAvatarTexture(User *user);

/* Opens a PNG, JPG, GIF or QOI picture held in memory. If it can't, returns an empty Image and
   points *error at the reason. */
Image LoadAnyImage(const unsigned char *data, int size, const char **error);

/* Makes the picture file at a typed path the user's profile picture. NULL on success, or what went
   wrong. */
const char *ImportAvatar(User *user, const char *typedPath);

/* Removes the user's profile picture. */
void RemoveAvatar(User *user);
extern const Color AVATAR_COLORS[8];

/* The user's picture, or a coloured circle with the first letter of their name. */
void DrawAvatar(int userId, int centerX, int centerY, int radius);

/* The theme a user has chosen (THEME_CLASSIC for an unknown user). */
int UserTheme(int userId);

#endif
