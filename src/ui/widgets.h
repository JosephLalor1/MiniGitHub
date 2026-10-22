/*
 * widgets.h - reusable pieces of the interface
 *
 * Buttons, confirm-twice buttons, checkboxes, text boxes (with show / hide
 * for passwords), scrolling, wrapped text, tooltips and toasts.
 */

#ifndef MINIGITHUB_WIDGETS_H
#define MINIGITHUB_WIDGETS_H

#include "common.h"
#include "app_state.h"

/* Draws a button that can be switched off (greyed, not clickable). Returns true when clicked. */
bool ButtonEnabled(Rectangle rect, const char *label, Color color, bool enabled);

/* Draws a button. Returns true when it is clicked. */
bool Button(Rectangle rect, const char *label, Color color);

/* First click arms it (turns red), second click within 3 seconds does it. */
bool ConfirmButton(Rectangle rect, const char *label, const char *confirmLabel, int id);

/* Empties a text box. */
void TextFieldClear(TextField *field);

/* Tab / Shift+Tab moves between the text boxes on a form. */
void CycleFocusOnTab(TextField **fields, int count);

/* Draws a checkbox with a label; clicking either flips *value. */
void Checkbox(bool *value, int x, int y, const char *label);

/* One-line text box. Returns true when Enter is pressed in it. Password boxes get an eye button on
   the right to show / hide the text. */
bool TextFieldWidget(TextField *field, Rectangle rect, const char *placeholder);

/* Scrolls *scroll with the mouse wheel while the mouse is over area, keeping it in range. */
void ScrollArea(Rectangle area, float *scroll, float contentHeight);

/* Draws a scrollbar along the right of area (only if the content doesn't fit). */
void DrawScrollbar(Rectangle area, float scroll, float contentHeight);

/* Word-wrapped text. Returns how many lines it used. */
int DrawWrappedText(const char *text, int x, int y, int maxWidth, int maxLines, Color color);

/* Draws text centred across the window. */
void DrawTextCentered(const char *text, int y, int fontSize, Color color);

/* A grey box under the form with "question  link". Returns true when the link is clicked. */
bool SwitchBox(float x, float y, float width, const char *question, const char *link);

/* Draws text broken into lines no wider than maxWidth (breaking at spaces), any length. With draw
   = false it only measures. Returns the height used. */
int WrapText(const char *text, int x, int y, int maxWidth, int fontSize, Color color, bool draw);

/* Blue clickable text. rightAlign: x is where the text ends. */
bool LinkText(const char *text, float x, float y, bool rightAlign);

/* Sets the tooltip to show this frame: a bold title and up to two more lines (NULL for none). */
void SetTooltip(const char *title, const char *line2, const char *line3);

/* Draws this frame's tooltip (if any) next to the mouse, on top of everything. */
void DrawTooltip(void);

/* Draws the current message (if any), fading out at the end. */
void DrawToast(void);

#endif
