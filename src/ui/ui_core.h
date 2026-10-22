/*
 * ui_core.h - colours and basic drawing helpers
 *
 * The interface's colours (GitHub's dark theme), and the mouse and text
 * helpers everything else is built on.
 */

#ifndef MINIGITHUB_UI_CORE_H
#define MINIGITHUB_UI_CORE_H

#include "common.h"

extern const Color UI_BASE;  /* the window background, always solid */
extern Color UI_BG;          /* these three turn slightly see-through */
extern Color UI_PANEL;       /* when a theme is showing behind them */
extern Color UI_HOVER;
extern const Color UI_BORDER;
extern const Color UI_TEXT;
extern const Color UI_MUTED;
extern const Color UI_LINK;
extern const Color UI_BUTTON;
extern const Color UI_BLUE;
extern const Color UI_GREEN;
extern const Color UI_GREEN_TEXT;
extern const Color UI_RED_TEXT;
extern const Color UI_DANGER;
extern const Color UI_YELLOW;
extern const Color UI_PURPLE;
extern const Color UI_ORANGE;

/* Notes that something changed and needs saving (it's saved once changes pause for a second). */
void MarkDirty(void);

/* Called once a frame: true when it's time to save - the changes have stopped for a
   second, or the oldest unsaved change is ten seconds old. */
bool SaveIsDue(float frameSeconds);

/* Shows a short message at the bottom of the window for a few seconds. */
void Toast(const char *message);

/* Was the key pressed this frame, or is it repeating because it's held down? */
bool KeyPressedOrRepeat(int key);
extern Rectangle hoverBlockedArea;
extern bool hoverBlocked;

/* Is the mouse over this rectangle (and not over an open menu covering it)? */
bool IsHovered(Rectangle rect);

/* Was this rectangle clicked this frame? Each click is only used once. */
bool WasClicked(Rectangle rect);

/* The first length bytes of text, ready to draw (tabs as spaces, no \r). Uses a shared buffer. */
const char *DisplaySlice(const char *text, int length);

/* Width in pixels of the first length characters (where the cursor would sit). */
int MeasureTextPrefix(const char *text, int length, int fontSize);

/* Draws text twice, one pixel apart, so it looks bold. */
void DrawTextBold(const char *text, int x, int y, int fontSize, Color color);

/* Draw text, cutting it short with "..." if it is too wide. */
void DrawTextFit(const char *text, int x, int y, int fontSize, int maxWidth, Color color);

/* Overlap of two rectangles (used so theme previews stay inside a scrolling list). */
Rectangle IntersectRects(Rectangle a, Rectangle b);

#endif
