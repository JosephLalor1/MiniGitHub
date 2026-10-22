/*
 * ui_core.c - colours and basic drawing helpers
 *
 * The interface's colours (GitHub's dark theme), and the mouse and text
 * helpers everything else is built on.
 */

#include "ui_core.h"
#include "app_state.h"

/* ------------------------------------------------------------------ */
/* Colours (GitHub's dark theme)                                       */
/* ------------------------------------------------------------------ */
const Color UI_BASE   = { 13,  17,  23, 255};    /* the window background, always solid */
Color       UI_BG     = { 13,  17,  23, 255};    /* these three turn slightly see-through */
Color       UI_PANEL  = { 22,  27,  34, 255};    /* when a theme is showing behind them   */
Color       UI_HOVER  = { 30,  36,  44, 255};
const Color UI_BORDER = { 48,  54,  61, 255};
const Color UI_TEXT   = {230, 237, 243, 255};
const Color UI_MUTED  = {125, 133, 144, 255};
const Color UI_LINK   = { 88, 166, 255, 255};
const Color UI_BUTTON    = { 33,  38,  45, 255};
const Color UI_BLUE   = { 31, 111, 235, 255};
const Color UI_GREEN  = { 35, 134,  54, 255};
const Color UI_GREEN_TEXT = { 63, 185,  80, 255};
const Color UI_RED_TEXT   = {248,  81,  73, 255};
const Color UI_DANGER = {218,  54,  51, 255};
const Color UI_YELLOW = {210, 153,  34, 255};
const Color UI_PURPLE = {137,  87, 229, 255};
const Color UI_ORANGE = {247, 129, 102, 255};

/* ------------------------------------------------------------------ */
/* Small helpers                                                      */
/* ------------------------------------------------------------------ */

/* Notes that something changed and needs saving (it's saved once changes pause for a second). */
void MarkDirty(void) { unsavedChanges = true; changeCount++; }

/* Saving rewrites the whole save file, which takes a moment once there's a lot in
   it - and the window can't redraw meanwhile. Saving soon after every change meant
   a pause every second while typing, so this waits for the changes to stop instead:
   a second with nothing new, or at most ten seconds after the first unsaved change. */
static float    quietSeconds;               /* seconds since the last change */
static float    unsavedSeconds;             /* seconds since the first change that isn't saved yet */
static unsigned lastChangeCount;            /* changeCount the frame before */

bool SaveIsDue(float frameSeconds)
{
    quietSeconds = changeCount != lastChangeCount ? 0 : quietSeconds + frameSeconds;
    lastChangeCount = changeCount;
    if (!unsavedChanges) { unsavedSeconds = 0; return false; }
    unsavedSeconds += frameSeconds;
    if (quietSeconds < 1.0f && unsavedSeconds < 10.0f) return false;
    unsavedSeconds = 0;
    return true;
}
/* Shows a short message at the bottom of the window for a few seconds. */
void Toast(const char *message) { snprintf(toastText, sizeof toastText, "%s", message); toastSeconds = 3.0f; }

/* Was the key pressed this frame, or is it repeating because it's held down? */
bool KeyPressedOrRepeat(int key) { return IsKeyPressed(key) || IsKeyPressedRepeat(key); }
/* An open drop-down menu covers part of the page: things under it act as if the mouse weren't there. */
Rectangle hoverBlockedArea;
bool      hoverBlocked;
/* Is the mouse over this rectangle (and not over an open menu covering it)? */
bool IsHovered(Rectangle rect)
{
    Vector2 mouse = GetMousePosition();
    if (hoverBlocked && CheckCollisionPointRec(mouse, hoverBlockedArea)) return false;
    return CheckCollisionPointRec(mouse, rect);
}
/* Was this rectangle clicked this frame? Each click is only used once. */
bool WasClicked(Rectangle rect)
{
    if (!clickHandled && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && IsHovered(rect)) { clickHandled = true; return true; }
    return false;
}

/* The first length bytes of text, ready to draw. Tab characters are shown as
   spaces up to the next multiple of 4 columns (raylib can't draw a tab),
   and the \r of Windows line endings is left out. */
static char displaySliceBuffer[2048];
/* The first length bytes of text, ready to draw (tabs as spaces, no \r). Uses a shared buffer. */
const char *DisplaySlice(const char *text, int length)
{
    if (length < 0) length = 0;
    int outLength = 0;
    for (int i = 0; i < length && outLength < 2000; i++) {
        if (text[i] == '\t') { do displaySliceBuffer[outLength++] = ' '; while (outLength % 4 && outLength < 2000); }
        else if (text[i] == '\r') continue;
        else displaySliceBuffer[outLength++] = text[i];
    }
    displaySliceBuffer[outLength] = 0;
    return displaySliceBuffer;
}
/* Width in pixels of the first length characters (where the cursor would sit). */
int MeasureTextPrefix(const char *text, int length, int fontSize)
{
    if (length <= 0) return 0;
    return MeasureText(DisplaySlice(text, length), fontSize) + fontSize / 10;
}
/* Draws text twice, one pixel apart, so it looks bold. */
void DrawTextBold(const char *text, int x, int y, int fontSize, Color color)
{
    DrawText(text, x, y, fontSize, color);
    DrawText(text, x + 1, y, fontSize, color);
}
/* Draw text, cutting it short with "..." if it is too wide. */
void DrawTextFit(const char *text, int x, int y, int fontSize, int maxWidth, Color color)
{
    if (MeasureText(text, fontSize) <= maxWidth) { DrawText(text, x, y, fontSize, color); return; }
    char shortened[256];
    int keepChars = (int)strlen(text);
    if (keepChars > 240) keepChars = 240;
    while (keepChars > 0) {
        snprintf(shortened, sizeof shortened, "%.*s...", keepChars, text);
        if (MeasureText(shortened, fontSize) <= maxWidth) break;
        keepChars--;
    }
    DrawText(keepChars > 0 ? shortened : "...", x, y, fontSize, color);
}

/* ---------------- settings: profile, password and account ---------------- */

/* Overlap of two rectangles (used so theme previews stay inside a scrolling list). */
Rectangle IntersectRects(Rectangle a, Rectangle b)
{
    float x0 = a.x > b.x ? a.x : b.x, y0 = a.y > b.y ? a.y : b.y;
    float x1 = a.x + a.width < b.x + b.width ? a.x + a.width : b.x + b.width;
    float y1 = a.y + a.height < b.y + b.height ? a.y + a.height : b.y + b.height;
    return (Rectangle){x0, y0, x1 > x0 ? x1 - x0 : 0, y1 > y0 ? y1 - y0 : 0};
}
