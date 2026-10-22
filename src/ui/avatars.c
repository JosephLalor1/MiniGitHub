/*
 * avatars.c - profile pictures
 *
 * Importing a PNG or JPG, cropping it to a round 192x192 picture kept in
 * the save file as base64, and drawing it - or a coloured circle with a
 * letter.
 */

#include "avatars.h"
#include "jpeg.h"
#include "model.h"
#include "themes.h"
#include "ui_core.h"
#include "users.h"

/* ------------------------------------------------------------------ */
/* Profile pictures                                                   */
/*                                                                    */
/* A picture is cropped to a square, shrunk to 192x192, given a round */
/* see-through edge, saved as PNG, and kept inside minigithub.dat as  */
/* base64 text (a way of writing any bytes using only letters, digits */
/* and + /). So there are no extra files to lose.                     */
/* ------------------------------------------------------------------ */
#define AVATAR_SIZE 192

static const char BASE64_DIGITS[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static char *ToBase64(const uint8_t *data, int length)
{
    char *out = malloc((size_t)(length + 2) / 3 * 4 + 1);
    if (!out) return NULL;
    int outLength = 0;
    for (int i = 0; i < length; i += 3) {
        uint32_t triple = (uint32_t)data[i] << 16 | (i + 1 < length ? data[i + 1] << 8 : 0) | (i + 2 < length ? data[i + 2] : 0);
        out[outLength++] = BASE64_DIGITS[triple >> 18 & 63];
        out[outLength++] = BASE64_DIGITS[triple >> 12 & 63];
        out[outLength++] = i + 1 < length ? BASE64_DIGITS[triple >> 6 & 63] : '=';
        out[outLength++] = i + 2 < length ? BASE64_DIGITS[triple & 63] : '=';
    }
    out[outLength] = 0;
    return out;
}

static uint8_t *FromBase64(const char *text, int *outLength)
{
    int length = (int)strlen(text), written = 0, bits = 0;
    uint32_t accumulated = 0;
    uint8_t *out = malloc((size_t)length * 3 / 4 + 3);
    if (!out) return NULL;
    for (int i = 0; i < length; i++) {
        const char *digit = strchr(BASE64_DIGITS, text[i]);
        if (!digit || !text[i]) continue;                /* skips '=' and anything odd */
        accumulated = accumulated << 6 | (uint32_t)(digit - BASE64_DIGITS);
        bits += 6;
        if (bits >= 8) { bits -= 8; out[written++] = (uint8_t)(accumulated >> bits); }
    }
    *outLength = written;
    return out;
}

/* Lets go of the drawable copy of a user's picture (made again when next needed). */
void FreeAvatarTexture(User *user)
{
    if (user->avatarState == 1) UnloadTexture(user->avatarTexture);
    user->avatarState = 0;
}

/* The picture as something raylib can draw, made the first time it's needed. */
static bool LoadAvatarTexture(User *user)
{
    if (!user->avatar || user->avatarState == -1) return false;
    if (user->avatarState == 1) return true;
    int pngLength = 0;
    uint8_t *png = FromBase64(user->avatar, &pngLength);
    Image image = png ? LoadImageFromMemory(".png", png, pngLength) : (Image){0};
    free(png);
    if (!image.data || image.width <= 0) { user->avatarState = -1; return false; }
    user->avatarTexture = LoadTextureFromImage(image);
    UnloadImage(image);
    GenTextureMipmaps(&user->avatarTexture);             /* keeps it smooth when drawn small */
    SetTextureFilter(user->avatarTexture, TEXTURE_FILTER_TRILINEAR);
    user->avatarState = 1;
    return true;
}

/* Returns NULL on success, or a message saying what went wrong. */
/* Opens a PNG, JPG, GIF or QOI picture held in memory. If it can't,
   returns an empty Image and points *error at the reason. */
Image LoadAnyImage(const unsigned char *data, int size, const char **error)
{
    Image image = {0};
    *error = NULL;
    if (size > 8 && memcmp(data, "\x89PNG", 4) == 0)                    image = LoadImageFromMemory(".png", data, size);
    else if (size > 3 && data[0] == 0xFF && data[1] == 0xD8)               image = DecodeJpg(data, size, error);
    else if (size > 6 && memcmp(data, "GIF8", 4) == 0)                  image = LoadImageFromMemory(".gif", data, size);
    else if (size > 4 && memcmp(data, "qoif", 4) == 0)                  image = LoadImageFromMemory(".qoi", data, size);
    else if (size > 12 && memcmp(data, "RIFF", 4) == 0 && memcmp(data + 8, "WEBP", 4) == 0)
        *error = "WEBP pictures aren't supported - save it as PNG or JPG first";
    else if (size > 12 && memcmp(data + 4, "ftyp", 4) == 0)
        *error = "HEIC pictures (from iPhones) aren't supported - save it as PNG or JPG first";
    else *error = "that isn't a picture this program can read - use PNG or JPG";
    if (*error && image.data) { UnloadImage(image); image = (Image){0}; }
    if (!*error && (!image.data || image.width <= 0 || image.height <= 0)) {
        if (image.data) UnloadImage(image);
        image = (Image){0};
        *error = "the picture data is damaged";
    }
    return image;
}

/* Makes the picture file at a typed path the user's profile picture. NULL on success, or what went
   wrong. */
const char *ImportAvatar(User *user, const char *typedPath)
{
    /* tidy the path: "Copy as path" on Windows adds quotes */
    char path[1024];
    while (*typedPath == ' ' || *typedPath == '"' || *typedPath == '\'') typedPath++;
    snprintf(path, sizeof path, "%s", typedPath);
    int length = (int)strlen(path);
    while (length > 0 && (path[length - 1] == ' ' || path[length - 1] == '"' || path[length - 1] == '\'')) path[--length] = 0;
    if (!length)                return "Type or paste the picture's file path first";
    if (!FileExists(path)) return "Couldn't find a file at that path";

    int size = 0;
    unsigned char *data = LoadFileData(path, &size);
    if (!data || size <= 0) return "Couldn't open that file";
    if (size > 40 * 1024 * 1024) { UnloadFileData(data); return "That picture is too big (over 40 MB)"; }

    const char *error = NULL;
    Image image = LoadAnyImage(data, size, &error);
    UnloadFileData(data);
    if (error) return TextFormat("Couldn't use it: %s", error);

    /* square crop from the middle, shrink, round edge */
    ImageFormat(&image, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    int side = image.width < image.height ? image.width : image.height;
    ImageCrop(&image, (Rectangle){(float)((image.width - side) / 2), (float)((image.height - side) / 2), (float)side, (float)side});
    ImageResize(&image, AVATAR_SIZE, AVATAR_SIZE);
    Color *pixels = (Color *)image.data;
    float center = (AVATAR_SIZE - 1) / 2.0f, radius = AVATAR_SIZE / 2.0f;
    for (int y = 0; y < AVATAR_SIZE; y++)
        for (int x = 0; x < AVATAR_SIZE; x++) {
            float dist = sqrtf((x - center) * (x - center) + (y - center) * (y - center));
            float alpha = radius - dist;                /* 1 inside, 0 outside, smooth in between */
            if (alpha < 0) alpha = 0;
            if (alpha > 1) alpha = 1;
            pixels[y * AVATAR_SIZE + x].a = (unsigned char)(pixels[y * AVATAR_SIZE + x].a * alpha);
        }

    int pngSize = 0;
    unsigned char *png = ExportImageToMemory(image, ".png", &pngSize);
    UnloadImage(image);
    if (!png || pngSize <= 0) return "Couldn't convert the picture";
    char *base64 = ToBase64(png, pngSize);
    MemFree(png);
    if (!base64) return "Out of memory";

    FreeAvatarTexture(user);
    free(user->avatar);
    user->avatar = base64;
    MarkDirty();
    return NULL;
}

/* Removes the user's profile picture. */
void RemoveAvatar(User *user)
{
    FreeAvatarTexture(user);
    free(user->avatar);
    user->avatar = NULL;
    MarkDirty();
}

/* Colours for people without a picture. 0 in avatarColor means "pick one for me". */
const Color AVATAR_COLORS[8] = {
    {137, 87, 229, 255}, { 31, 111, 235, 255}, { 35, 134,  54, 255}, {191, 135,  0, 255},
    {207,  34,  46, 255}, {219,  97, 162, 255}, { 20, 150, 160, 255}, {110, 118, 129, 255}
};
static Color AvatarColor(int userId)
{
    User *user = FindUserById(userId);
    if (user && user->avatarColor >= 1 && user->avatarColor <= 8) return AVATAR_COLORS[user->avatarColor - 1];
    return AVATAR_COLORS[(userId < 0 ? -userId : userId) % 6];
}

/* The user's picture, or a coloured circle with the first letter of their name. */
void DrawAvatar(int userId, int centerX, int centerY, int radius)
{
    if (IsOutsider(userId)) {              /* not a Mini GitHub user: a grey circle with a dotted edge */
        const char *name = UserName(userId);
        char letter[2] = {(char)toupper((unsigned char)name[0]), 0};
        int fontSize = radius >= 40 ? 60 : radius >= 24 ? 30 : 20;
        DrawCircle(centerX, centerY, (float)radius, (Color){60, 66, 74, 255});
        for (int angle = 0; angle < 360; angle += 30) DrawCircleSector((Vector2){(float)centerX, (float)centerY}, (float)radius, (float)angle, (float)angle + 12, 4, (Color){110, 118, 129, 255});
        DrawCircle(centerX, centerY, (float)radius - 2, (Color){60, 66, 74, 255});
        if (radius >= 9) DrawText(letter, centerX - MeasureText(letter, fontSize) / 2, centerY - fontSize / 2 + 1, fontSize, (Color){200, 205, 212, 255});
        return;
    }
    User *user = FindUserById(userId);
    if (user && LoadAvatarTexture(user)) {
        DrawTexturePro(user->avatarTexture, (Rectangle){0, 0, (float)user->avatarTexture.width, (float)user->avatarTexture.height},
                       (Rectangle){(float)(centerX - radius), (float)(centerY - radius), (float)radius * 2, (float)radius * 2},
                       (Vector2){0, 0}, 0, WHITE);
        return;
    }
    const char *name = UserName(userId);
    char letter[2] = {(char)toupper((unsigned char)name[0]), 0};
    int fontSize = radius >= 40 ? 60 : radius >= 24 ? 30 : 20;
    DrawCircle(centerX, centerY, (float)radius, AvatarColor(userId));
    DrawText(letter, centerX - MeasureText(letter, fontSize) / 2, centerY - fontSize / 2 + 1, fontSize, UI_TEXT);
}

/* The theme a user has chosen (THEME_CLASSIC for an unknown user). */
int UserTheme(int userId)
{
    User *user = FindUserById(userId);
    return user && user->theme >= 0 && user->theme < THEME_COUNT ? user->theme : THEME_CLASSIC;
}
