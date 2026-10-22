/*
 * previews.c - previews of pictures and other non-text files
 *
 * A few decoded pictures are kept ready to draw; other binary files get a
 * simple description.
 */

#include "previews.h"
#include "avatars.h"
#include "ui_core.h"
#include "uploads.h"
#include "work_file.h"

/* ---------------- previews of pictures and other non-text files ---------------- */

PreviewSlot previewSlots[PREVIEW_SLOTS];
static unsigned long long previewClock;

/* Do these bytes start like a PNG, JPG, GIF or QOI picture? */
bool IsPictureBytes(const char *data, int length)
{
    const unsigned char *bytes = (const unsigned char *)data;
    return (length > 8 && memcmp(data, "\x89PNG", 4) == 0) || (length > 3 && bytes[0] == 0xFF && bytes[1] == 0xD8) ||
           (length > 6 && memcmp(data, "GIF8", 4) == 0)    || (length > 4 && memcmp(data, "qoif", 4) == 0);
}

/* Returns which slot holds this picture, or -1 if it isn't a picture we can show. */
int GetPreviewSlot(const char *data, int length)
{
    if (!IsPictureBytes(data, length)) return -1;
    previewClock++;
    for (int i = 0; i < PREVIEW_SLOTS; i++)
        if (previewSlots[i].state && previewSlots[i].data == data && previewSlots[i].length == length && previewSlots[i].generation == fileContentsGeneration) {
            previewSlots[i].lastUsed = previewClock;
            return previewSlots[i].state == 1 ? i : -1;
        }
    int slot = 0;
    for (int i = 1; i < PREVIEW_SLOTS; i++) if (previewSlots[i].lastUsed < previewSlots[slot].lastUsed) slot = i;
    if (previewSlots[slot].state == 1) UnloadTexture(previewSlots[slot].texture);
    memset(&previewSlots[slot], 0, sizeof previewSlots[slot]);
    previewSlots[slot].data = data; previewSlots[slot].length = length; previewSlots[slot].generation = fileContentsGeneration; previewSlots[slot].lastUsed = previewClock;
    previewSlots[slot].state = -1;

    const char *error = NULL;
    Image image = LoadAnyImage((const unsigned char *)data, length, &error);
    if (error) return -1;
    previewSlots[slot].width = image.width;
    previewSlots[slot].height = image.height;
    int largestSide = image.width > image.height ? image.width : image.height;
    if (largestSide > 2048) ImageResize(&image, image.width * 2048 / largestSide, image.height * 2048 / largestSide);  /* keeps graphics cards happy */
    previewSlots[slot].texture = LoadTextureFromImage(image);
    UnloadImage(image);
    SetTextureFilter(previewSlots[slot].texture, TEXTURE_FILTER_BILINEAR);
    previewSlots[slot].state = 1;
    return slot;
}

/* Draws the picture in this slot as big as fits in box (small pictures up to 3 times bigger),
   on a chequered background so see-through parts are visible. */
void DrawPicture(int slot, Rectangle box)
{
    Texture2D texture = previewSlots[slot].texture;
    float scale = fminf(box.width / texture.width, box.height / texture.height);
    float maxScale = (texture.width < 200 && texture.height < 200) ? 3.0f : 1.0f;
    if (scale > maxScale) scale = maxScale;
    float width = texture.width * scale, height = texture.height * scale;
    Rectangle dest = {box.x + (box.width - width) / 2, box.y + (box.height - height) / 2, width, height};
    BeginScissorMode((int)dest.x, (int)dest.y, (int)dest.width + 1, (int)dest.height + 1);
    for (int y = 0; y * 12 < dest.height; y++)
        for (int x = 0; x * 12 < dest.width; x++)
            DrawRectangle((int)dest.x + x * 12, (int)dest.y + y * 12, 12, 12,
                          (x + y) % 2 ? (Color){60, 66, 74, 255} : (Color){44, 49, 56, 255});
    EndScissorMode();
    DrawTexturePro(texture, (Rectangle){0, 0, (float)texture.width, (float)texture.height}, dest, (Vector2){0, 0}, 0, WHITE);
}

/* What the Code tab shows instead of the editor for a non-text file. */
void DrawBinaryView(const char *data, int length, Rectangle area)
{
    DrawRectangleRec(area, UI_BASE);
    int slot = GetPreviewSlot(data, length);
    if (slot >= 0) {
        DrawPicture(slot, (Rectangle){area.x + 20, area.y + 20, area.width - 40, area.height - 60});
        const char *caption = TextFormat("%d x %d pixels   |   %s", previewSlots[slot].width, previewSlots[slot].height, FormatSize(length));
        DrawText(caption, (int)(area.x + area.width / 2) - MeasureText(caption, FONT_SIZE) / 2, (int)(area.y + area.height) - 32, FONT_SIZE, UI_MUTED);
        return;
    }
    /* a simple "document" shape with a folded corner */
    float cx = area.x + area.width / 2, cy = area.y + area.height / 2 - 50;
    Rectangle doc = {cx - 30, cy - 38, 60, 76};
    DrawRectangleLinesEx(doc, 2, UI_MUTED);
    DrawTriangle((Vector2){doc.x + 40, doc.y}, (Vector2){doc.x + 60, doc.y + 20}, (Vector2){doc.x + 60, doc.y}, UI_BASE);
    DrawLineEx((Vector2){doc.x + 40, doc.y}, (Vector2){doc.x + 60, doc.y + 20}, 2, UI_MUTED);
    DrawText("01", (int)cx - MeasureText("01", FONT_SIZE) / 2, (int)cy - 4, FONT_SIZE, UI_MUTED);
    const char *line1 = IsPictureBytes(data, length) ? TextFormat("Picture   |   %s", FormatSize(length))
                                               : TextFormat("Binary file   |   %s", FormatSize(length));
    const char *line2 = IsPictureBytes(data, length) ? "This kind of picture can't be previewed, but it's stored safely."
                                               : "This isn't a text file, so it can't be shown here - but it's stored safely.";
    DrawText(line1, (int)cx - MeasureText(line1, FONT_SIZE) / 2, (int)cy + 56, FONT_SIZE, UI_TEXT);
    DrawTextFit(line2, (int)fmaxf(area.x + 16, cx - MeasureText(line2, FONT_SIZE) / 2.0f), (int)cy + 86, FONT_SIZE, (int)area.width - 32, UI_MUTED);
}
