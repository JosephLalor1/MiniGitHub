/*
 * widgets.c - reusable pieces of the interface
 *
 * Buttons, confirm-twice buttons, checkboxes, text boxes (with show / hide
 * for passwords), scrolling, wrapped text, tooltips and toasts.
 */

#include "widgets.h"
#include "animation.h"
#include "app_state.h"
#include "themes.h"
#include "ui_core.h"

/* ------------------------------------------------------------------ */
/* Widgets                                                            */
/* ------------------------------------------------------------------ */
/* Draws a button that can be switched off (greyed, not clickable). Returns true when clicked. */
bool ButtonEnabled(Rectangle rect, const char *label, Color color, bool enabled)
{
    bool hovered = enabled && IsHovered(rect);
    if (hovered) wantedCursor = MOUSE_CURSOR_POINTING_HAND;
    float glow = HoverAmount(RectKey(rect), hovered);      /* brightens smoothly as the mouse arrives */
    DrawRectangleRounded(rect, 0.25f, 6, glow > 0 ? MixColors(color, ColorBrightness(color, 0.15f), glow) : color);
    int textWidth = MeasureText(label, FONT_SIZE);
    DrawText(label, (int)(rect.x + (rect.width - (float)textWidth) / 2), (int)(rect.y + (rect.height - FONT_SIZE) / 2), FONT_SIZE, UI_TEXT);
    return enabled && WasClicked(rect);
}
/* Draws a button. Returns true when it is clicked. */
bool Button(Rectangle rect, const char *label, Color color) { return ButtonEnabled(rect, label, color, true); }

/* Fills a row with the hover colour, fading in and out as the mouse comes and goes.
   key: what the row shows (so it keeps its fade while the list scrolls). */
void DrawHoverFill(Rectangle rect, unsigned key, bool hovered)
{
    float glow = HoverAmount(key, hovered);
    if (glow > 0) DrawRectangleRec(rect, WithAlpha(UI_HOVER, glow));
}

/* A card's background: the panel colour, easing to the hover colour while it's hovered. */
Color CardColor(unsigned key, bool hovered) { return MixColors(UI_PANEL, UI_HOVER, HoverAmount(key, hovered)); }

/* First click arms it (turns red), second click within 3 seconds does it. */
bool ConfirmButton(Rectangle rect, const char *label, const char *confirmLabel, int id)
{
    bool isArmed = (armedButtonId == id);
    if (Button(rect, isArmed ? confirmLabel : label, isArmed ? UI_DANGER : UI_BUTTON)) {
        if (isArmed) { armedButtonId = 0; return true; }
        armedButtonId = id;
        armedSeconds  = 3.0f;
    }
    return false;
}

/* Empties a text box. */
void TextFieldClear(TextField *field) { field->text[0] = 0; field->length = 0; }

/* Tab / Shift+Tab moves between the text boxes on a form. */
void CycleFocusOnTab(TextField **fields, int count)
{
    if (!IsKeyPressed(KEY_TAB)) return;
    int current = -1;
    for (int i = 0; i < count; i++) { if (fields[i]->active) current = i; fields[i]->active = false; }
    int direction = (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) ? -1 : 1;
    fields[current < 0 ? 0 : (current + direction + count) % count]->active = true;
}

/* Draws a checkbox with a label; clicking either flips *value. */
void Checkbox(bool *value, int x, int y, const char *label)
{
    Rectangle hitRow = {(float)x, (float)y - 3, (float)(34 + MeasureText(label, FONT_SIZE)), 28};
    Rectangle box  = {(float)x, (float)y, 22, 22};
    if (IsHovered(hitRow)) wantedCursor = MOUSE_CURSOR_POINTING_HAND;
    DrawRectangleRec(box, *value ? UI_GREEN : UI_BG);
    DrawRectangleLinesEx(box, 1, *value ? UI_GREEN : UI_BORDER);
    if (*value) {
        DrawLineEx((Vector2){box.x + 5, box.y + 11}, (Vector2){box.x + 9, box.y + 16}, 3, UI_TEXT);
        DrawLineEx((Vector2){box.x + 9, box.y + 16}, (Vector2){box.x + 17, box.y + 6}, 3, UI_TEXT);
    }
    DrawText(label, x + 34, y + 1, FONT_SIZE, UI_TEXT);
    if (WasClicked(hitRow)) *value = !*value;
}

/* An eye; with a line through it when "slashed". */
static void DrawEye(float centerX, float centerY, bool slashed, Color color, Color background)
{
    DrawEllipse((int)centerX, (int)centerY, 11, 7, color);
    DrawEllipse((int)centerX, (int)centerY, 9, 5, background);
    DrawCircle((int)centerX, (int)centerY, 3.5f, color);
    if (slashed) {
        DrawLineEx((Vector2){centerX - 11, centerY + 9}, (Vector2){centerX + 11, centerY - 9}, 5, background);
        DrawLineEx((Vector2){centerX - 11, centerY + 9}, (Vector2){centerX + 11, centerY - 9}, 2, color);
    }
}

/* One-line text box. Returns true when Enter is pressed in it.
   Password boxes get an eye button on the right to show / hide the text. */
bool TextFieldWidget(TextField *field, Rectangle rect, const char *placeholder)
{
    bool pressedEnter = false;
    float eyeWidth = field->secret ? 42.0f : 0.0f;
    Rectangle eyeRect = {rect.x + rect.width - eyeWidth, rect.y, eyeWidth, rect.height};
    bool eyeHovered = field->secret && IsHovered(eyeRect);
    if (field->secret && WasClicked(eyeRect)) field->reveal = !field->reveal;

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) field->active = IsHovered(rect);
    if (eyeHovered)       wantedCursor = MOUSE_CURSOR_POINTING_HAND;
    else if (IsHovered(rect)) wantedCursor = MOUSE_CURSOR_IBEAM;
    if (field->active) {
        int ch;
        while ((ch = GetCharPressed()) > 0)
            if (ch >= 32 && ch < 127 && field->length < TEXT_LEN - 1) { field->text[field->length++] = (char)ch; field->text[field->length] = 0; }
        if (KeyPressedOrRepeat(KEY_BACKSPACE) && field->length > 0) field->text[--field->length] = 0;
        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) pressedEnter = true;
        bool ctrlDown = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL) ||
                    IsKeyDown(KEY_LEFT_SUPER) || IsKeyDown(KEY_RIGHT_SUPER);
        if (ctrlDown && IsKeyPressed(KEY_V)) {               /* Ctrl+V pastes */
            const char *clipboard = GetClipboardText();
            for (; clipboard && *clipboard && field->length < TEXT_LEN - 1; clipboard++)
                if (*clipboard >= 32 && *clipboard < 127) { field->text[field->length++] = *clipboard; field->text[field->length] = 0; }
        }
    }
    DrawRectangleRec(rect, UI_BG);
    DrawRectangleLinesEx(rect, field->active ? 2.0f : 1.0f, field->active ? UI_LINK : UI_BORDER);

    /* password boxes show one * per character, unless the eye is open */
    const char *shown = field->text;
    char masked[TEXT_LEN];
    if (field->secret && !field->reveal) { memset(masked, '*', (size_t)field->length); masked[field->length] = 0; shown = masked; }

    int textX = (int)rect.x + 10, textY = (int)(rect.y + (rect.height - FONT_SIZE) / 2);
    int textWidth = MeasureText(shown, FONT_SIZE);
    int room  = (int)(rect.width - eyeWidth) - 30;
    int scrollOffset = textWidth > room ? textWidth - room : 0;
    BeginScissorMode((int)rect.x + 2, (int)rect.y, (int)(rect.width - eyeWidth) - 4, (int)rect.height);
    if (field->length == 0) DrawText(placeholder, textX, textY, FONT_SIZE, UI_MUTED);
    else             DrawText(shown, textX - scrollOffset, textY, FONT_SIZE, UI_TEXT);
    if (field->active && ((int)(GetTime() * 2) % 2) == 0)
        DrawRectangle(textX - scrollOffset + textWidth + (field->length ? 2 : 0), textY - 2, 2, FONT_SIZE + 4, UI_TEXT);
    EndScissorMode();
    if (field->secret) {
        Rectangle eyeHit = {eyeRect.x + 4, eyeRect.y + 4, eyeRect.width - 8, eyeRect.height - 8};
        if (eyeHovered) DrawRectangleRounded(eyeHit, 0.3f, 6, UI_HOVER);
        /* eye = "show password"; eye with a line = "hide it again" */
        DrawEye(eyeRect.x + eyeRect.width / 2, eyeRect.y + eyeRect.height / 2, field->reveal,
                eyeHovered ? UI_TEXT : UI_MUTED, eyeHovered ? UI_HOVER : UI_BG);
    }
    return pressedEnter;
}

/* Smooth scrolling: the wheel moves where each list is heading, and the list
   glides there over the next few frames. One slot per scroll position in use. */
#define SCROLL_SLOTS 32
typedef struct { float *scroll; float target, shown; double lastTime; } ScrollSlot;
static ScrollSlot scrollSlots[SCROLL_SLOTS];

/* The slot for this scroll position (the least recently used one is taken over for a new one). */
static ScrollSlot *FindScrollSlot(float *scroll)
{
    ScrollSlot *oldest = &scrollSlots[0];
    for (int i = 0; i < SCROLL_SLOTS; i++) {
        if (scrollSlots[i].scroll == scroll) return &scrollSlots[i];
        if (scrollSlots[i].lastTime < oldest->lastTime) oldest = &scrollSlots[i];
    }
    *oldest = (ScrollSlot){scroll, *scroll, *scroll, GetTime()};
    return oldest;
}

/* Scrolls *scroll with the mouse wheel while the mouse is over area, keeping it in range. */
void ScrollArea(Rectangle area, float *scroll, float contentHeight)
{
    float maxScroll = contentHeight - area.height;
    if (maxScroll < 0) maxScroll = 0;
    ScrollSlot *slot = FindScrollSlot(scroll);
    double now = GetTime();
    float elapsed = (float)(now - slot->lastTime);
    if (elapsed > 0.1f) elapsed = 0.1f;
    slot->lastTime = now;
    if (*scroll != slot->shown) slot->target = *scroll;   /* moved by something else (a reset, a jump): go along with it */
    if (IsHovered(area)) slot->target -= GetMouseWheelMove() * 60.0f;
    if (slot->target > maxScroll) slot->target = maxScroll;
    if (slot->target < 0)         slot->target = 0;
    *scroll += (slot->target - *scroll) * (1 - expf(-16.0f * elapsed));
    if (fabsf(slot->target - *scroll) < 0.5f) *scroll = slot->target;
    if (*scroll > maxScroll) *scroll = maxScroll;
    if (*scroll < 0)         *scroll = 0;
    slot->shown = *scroll;
}
/* Draws a scrollbar along the right of area (only if the content doesn't fit). */
void DrawScrollbar(Rectangle area, float scroll, float contentHeight)
{
    if (contentHeight <= area.height) return;
    float thumbHeight = area.height * area.height / contentHeight;
    float thumbY = area.y + (scroll / contentHeight) * area.height;
    DrawRectangleRounded((Rectangle){area.x + area.width - 7, thumbY, 5, thumbHeight}, 1.0f, 4, UI_BORDER);
}

/* Word-wrapped text. Returns how many lines it used. */
int DrawWrappedText(const char *text, int x, int y, int maxWidth, int maxLines, Color color)
{
    char line[256];
    int lineCount = 0;
    const char *pos = text;
    while (*pos && lineCount < maxLines) {
        while (*pos == ' ') pos++;
        int fitLength = 0, wordEnd = 0;
        for (;;) {                                       /* take as many whole words as fit */
            while (pos[wordEnd] && pos[wordEnd] != ' ') wordEnd++;
            if (wordEnd > 250) wordEnd = 250;
            if (MeasureText(DisplaySlice(pos, wordEnd), FONT_SIZE) > maxWidth) break;
            fitLength = wordEnd;
            if (!pos[wordEnd] || wordEnd >= 250) break;
            wordEnd++;
        }
        if (fitLength == 0) {                            /* one very long word: cut it */
            fitLength = 1;
            while (pos[fitLength] && MeasureText(DisplaySlice(pos, fitLength + 1), FONT_SIZE) <= maxWidth) fitLength++;
        }
        bool isLast = lineCount == maxLines - 1 && pos[fitLength];
        snprintf(line, sizeof line, "%.*s%s", fitLength, pos, isLast ? "..." : "");
        if (isLast) DrawTextFit(line, x, y + lineCount * 24, FONT_SIZE, maxWidth, color);
        else      DrawText(line, x, y + lineCount * 24, FONT_SIZE, color);
        lineCount++;
        pos += fitLength;
    }
    return lineCount;
}

/* ---------------- sign in / create account ---------------- */
/* Draws text centred across the window. */
void DrawTextCentered(const char *text, int y, int fontSize, Color color) { DrawText(text, screenWidth / 2 - MeasureText(text, fontSize) / 2, y, fontSize, color); }

/* A grey box under the form with "question  link". Returns true when the link is clicked. */
bool SwitchBox(float x, float y, float width, const char *question, const char *link)
{
    Rectangle box = {x, y, width, 56};
    DrawRectangleRec(box, UI_BG);
    DrawRectangleLinesEx(box, 1, UI_BORDER);
    int questionWidth = MeasureText(question, FONT_SIZE), linkWidth = MeasureText(link, FONT_SIZE);
    int startX = (int)(x + width / 2) - (questionWidth + linkWidth) / 2;
    DrawText(question, startX, (int)y + 18, FONT_SIZE, UI_MUTED);
    DrawText(link, startX + questionWidth, (int)y + 18, FONT_SIZE, UI_LINK);
    Rectangle hit = {(float)(startX + questionWidth), y + 12, (float)linkWidth, 32};
    if (IsHovered(hit)) { wantedCursor = MOUSE_CURSOR_POINTING_HAND; DrawLine(startX + questionWidth, (int)y + 40, startX + questionWidth + linkWidth, (int)y + 40, UI_LINK); }
    return WasClicked(hit);
}

/* Draws text broken into lines no wider than maxWidth (breaking at spaces), any length.
   With draw = false it only measures. Returns the height used. */
int WrapText(const char *text, int x, int y, int maxWidth, int fontSize, Color color, bool draw)
{
    char line[300] = "", trial[300];
    int  lineCount = 0, lineStep = fontSize + 8;
    const char *pos = text;
    while (*pos) {
        const char *word = pos;
        while (*pos && *pos != ' ') pos++;
        int wordLength = (int)(pos - word);
        if (wordLength > 120) wordLength = 120;                  /* keeps every line inside the buffers */
        if (line[0]) snprintf(trial, sizeof trial, "%.170s %.*s", line, wordLength, word);
        else         snprintf(trial, sizeof trial, "%.*s", wordLength, word);
        if (line[0] && MeasureText(trial, fontSize) > maxWidth) {  /* doesn't fit: start a new line */
            if (draw) DrawText(line, x, y + lineCount * lineStep, fontSize, color);
            lineCount++;
            snprintf(line, sizeof line, "%.*s", wordLength, word);
        } else snprintf(line, sizeof line, "%s", trial);
        while (*pos == ' ') pos++;
    }
    if (line[0]) { if (draw) DrawText(line, x, y + lineCount * lineStep, fontSize, color); lineCount++; }
    return lineCount * lineStep;
}

/* Blue clickable text. rightAlign: x is where the text ends. */
bool LinkText(const char *text, float x, float y, bool rightAlign)
{
    int width = MeasureText(text, FONT_SIZE);
    if (rightAlign) x -= (float)width;
    Rectangle hit = {x, y - 4, (float)width, FONT_SIZE + 8};
    bool hovered = IsHovered(hit);
    if (hovered) { wantedCursor = MOUSE_CURSOR_POINTING_HAND; DrawLine((int)x, (int)y + FONT_SIZE + 1, (int)x + width, (int)y + FONT_SIZE + 1, UI_LINK); }
    DrawText(text, (int)x, (int)y, FONT_SIZE, UI_LINK);
    return WasClicked(hit);
}

/* A tooltip that's drawn last, on top of everything else on screen. */
static char tooltipText[3][TEXT_LEN];
static int  tooltipLines;
/* Sets the tooltip to show this frame: a bold title and up to two more lines (NULL for none). */
void SetTooltip(const char *title, const char *line2, const char *line3)
{
    snprintf(tooltipText[0], TEXT_LEN, "%s", title ? title : "");
    snprintf(tooltipText[1], TEXT_LEN, "%s", line2 ? line2 : "");
    snprintf(tooltipText[2], TEXT_LEN, "%s", line3 ? line3 : "");
    tooltipLines = line3 ? 3 : line2 ? 2 : 1;
}
/* Draws this frame's tooltip (if any) next to the mouse, on top of everything. */
void DrawTooltip(void)
{
    if (!tooltipLines) return;
    int width = 0;
    for (int i = 0; i < tooltipLines; i++) { int lineWidth = MeasureText(tooltipText[i], FONT_SIZE); if (lineWidth > width) width = lineWidth; }
    int boxWidth = width + 32, boxHeight = tooltipLines * 26 + 18;
    Vector2 mouse = GetMousePosition();
    int x = (int)mouse.x + 18, y = (int)mouse.y + 20;
    if (x + boxWidth > screenWidth - 8) x = (int)mouse.x - 18 - boxWidth;
    if (y + boxHeight > screenHeight - 8) y = (int)mouse.y - 12 - boxHeight;
    Rectangle box = {(float)x, (float)y, (float)boxWidth, (float)boxHeight};
    DrawRectangleRounded((Rectangle){box.x + 3, box.y + 4, box.width, box.height}, 0.1f, 6, (Color){0, 0, 0, 120});
    DrawRectangleRounded(box, 0.1f, 6, (Color){36, 41, 49, 255});
    DrawRectangleRoundedLinesEx(box, 0.1f, 6, 1, UI_BORDER);
    for (int i = 0; i < tooltipLines; i++)
        if (i == 0) DrawTextBold(tooltipText[i], x + 16, y + 10, FONT_SIZE, UI_TEXT);
        else        DrawText(tooltipText[i], x + 16, y + 10 + i * 26, FONT_SIZE, UI_MUTED);
    tooltipLines = 0;
}

/* Draws the current message (if any), fading out at the end. */
void DrawToast(void)
{
    if (toastSeconds <= 0) return;
    float shownFor = 3.0f - toastSeconds;                       /* it slides up and fades in, then fades out */
    float arrive = EaseInOut(shownFor / 0.25f);
    float alpha = toastSeconds < 0.4f ? toastSeconds / 0.4f : arrive;
    int width = MeasureText(toastText, FONT_SIZE) + 44;
    Rectangle box = {(float)(screenWidth - width) / 2, (float)screenHeight - 130 + 16 * (1 - arrive), (float)width, 46};
    DrawRectangleRounded(box, 0.3f, 8, Fade((Color){48, 54, 61, 255}, alpha));
    DrawText(toastText, (int)box.x + 22, (int)box.y + 13, FONT_SIZE, Fade(UI_TEXT, alpha));
}
