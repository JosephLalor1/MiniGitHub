/*
 * previews.h - previews of pictures and other non-text files
 *
 * A few decoded pictures are kept ready to draw; other binary files get a
 * simple description.
 */

#ifndef MINIGITHUB_PREVIEWS_H
#define MINIGITHUB_PREVIEWS_H

#include "common.h"

/* Up to 4 pictures are kept ready to draw; the one used longest ago is replaced first. */
#define PREVIEW_SLOTS 4

typedef struct {
    const char *data; int length, generation, state, width, height;  /* state: 1 = ready, -1 = couldn't be read */
    Texture2D texture;
    unsigned long long lastUsed;
} PreviewSlot;

extern PreviewSlot previewSlots[PREVIEW_SLOTS];

/* Do these bytes start like a PNG, JPG, GIF or QOI picture? */
bool IsPictureBytes(const char *data, int length);

/* Returns which slot holds this picture, or -1 if it isn't a picture we can show. */
int GetPreviewSlot(const char *data, int length);

/* Draws the picture in this slot as big as fits in box (small pictures up to 3 times bigger), on a
   chequered background so see-through parts are visible. */
void DrawPicture(int slot, Rectangle box);

/* What the Code tab shows instead of the editor for a non-text file. */
void DrawBinaryView(const char *data, int length, Rectangle area);

#endif
