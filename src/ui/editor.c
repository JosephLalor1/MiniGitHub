/*
 * editor.c - the built-in text editor
 *
 * Typing, moving the cursor, scrolling, line numbers, and one-click ways
 * to settle merge conflicts.
 */

#include "editor.h"
#include "app_state.h"
#include "byte_buffer.h"
#include "model.h"
#include "ui_core.h"
#include "work_file.h"

/* ------------------------------------------------------------------ */
/* Text editor                                                        */
/* ------------------------------------------------------------------ */
/* Where the line holding position pos starts. */
int LineStart(const char *text, int pos)        { while (pos > 0 && text[pos - 1] != '\n') pos--; return pos; }
static int LineEnd(const char *text, int length, int pos) { while (pos < length && text[pos] != '\n') pos++; return pos; }
/* Which line (from 0) position pos is on. */
int LineNumberAt(const char *text, int pos)           { int line = 0; for (int i = 0; i < pos; i++) if (text[i] == '\n') line++; return line; }

static int ColumnAtX(const char *line, int lineLength, float x)
{
    int bestColumn = 0;
    float bestDistance = 1e9f;
    for (int column = 0; column <= lineLength; column++) {
        float columnX = (float)MeasureTextPrefix(line, column, FONT_SIZE);
        float distance = columnX - x;
        if (distance < 0) distance = -distance;
        if (distance < bestDistance) { bestDistance = distance; bestColumn = column; }
        if (columnX > x + 30) break;
    }
    return bestColumn;
}

/* The Code tab remembers the open file by name, so it stays open while files
   are added, deleted or committed around it. "" means "open the first one". */
void SelectFileName(const char *name)
{
    snprintf(selectedFile, PATH_LEN, "%.*s", PATH_LEN - 1, name ? name : "");
    editor.cursor = 0; editor.scrollX = editor.scrollY = 0;
    editor.focused = false;
}

/* Draws and runs the editor. Returns true if the text was changed.
   In read-only mode you can still click, scroll and move the cursor. */
/* Letters like "e" with an accent take 2-4 bytes in UTF-8 text. The extra
   bytes all look like 10xxxxxx, so the cursor skips over those to always
   land at the start of a whole letter. */
static bool IsUtf8Continuation(char ch) { return ((unsigned char)ch & 0xC0) == 0x80; }
static int  PrevChar(const char *text, int pos)          { if (pos > 0) pos--; while (pos > 0 && IsUtf8Continuation(text[pos])) pos--; return pos; }
static int  NextChar(const char *text, int length, int pos) { if (pos < length) pos++; while (pos < length && IsUtf8Continuation(text[pos])) pos++; return pos; }
static int  SnapToChar(const char *text, int length, int pos) { while (pos > 0 && pos < length && IsUtf8Continuation(text[pos])) pos--; return pos; }

/* git's conflict marker lines: "<<<<<<< yours", "=======", ">>>>>>> theirs" */
static bool LineStartsWith(const char *text, int lineStart, int lineEnd, const char *prefix)
{
    int length = (int)strlen(prefix);
    return lineEnd - lineStart >= length && memcmp(text + lineStart, prefix, (size_t)length) == 0;
}
static bool IsMidMarker(const char *text, int lineStart, int lineEnd)
{
    int length = lineEnd - lineStart;
    if (length > 0 && text[lineEnd - 1] == '\r') length--;
    return length == 7 && memcmp(text + lineStart, "=======", 7) == 0;
}
/* Settles one clash (the one whose "<<<<<<<" line starts at start):
   which = 0 keeps your lines, 1 theirs, 2 both (yours first). */
static bool ResolveConflict(WorkFile *file, int start, int which)
{
    const char *text = file->text;
    int length = file->length, firstEnd = LineEnd(text, length, start), middle = -1, end = -1;
    for (int pos = firstEnd + 1; pos < length; ) {
        int lineEnd = LineEnd(text, length, pos);
        if (middle < 0 && IsMidMarker(text, pos, lineEnd)) middle = pos;
        else if (middle >= 0 && LineStartsWith(text, pos, lineEnd, ">>>>>>>")) { end = pos; break; }
        else if (LineStartsWith(text, pos, lineEnd, "<<<<<<<")) return false;  /* a broken block */
        if (lineEnd >= length) break;
        pos = lineEnd + 1;
    }
    if (middle < 0 || end < 0 || firstEnd >= length) return false;
    int middleEnd = LineEnd(text, length, middle), lastEnd = LineEnd(text, length, end);
    int cutFrom = lastEnd < length ? lastEnd + 1 : lastEnd;
    int oursStart = firstEnd + 1, oursEnd = middle, theirsStart = middleEnd + 1, theirsEnd = end;
    ByteBuffer merged = {0};
    BufferAppend(&merged, text, (size_t)start);
    if (which != 1) BufferAppend(&merged, text + oursStart, (size_t)(oursEnd - oursStart));
    if (which != 0) BufferAppend(&merged, text + theirsStart, (size_t)(theirsEnd - theirsStart));
    BufferAppend(&merged, text + cutFrom, (size_t)(length - cutFrom));
    if (merged.outOfMemory) { BufferFree(&merged); return false; }
    WorkFileSetBytes(file, merged.data ? (const char *)merged.data : "", (int)merged.length);
    BufferFree(&merged);
    return true;
}

/* Draws and runs the text editor for a file. Returns true if the text was changed. */
bool EditorWidget(WorkFile *file, Rectangle area, bool readOnly)
{
    bool changed = false, moved = false;
    float gutter = 58, textX = area.x + gutter + 12, top = area.y + 8;

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) editor.focused = IsHovered(area);
    if (IsHovered(area)) {
        wantedCursor = MOUSE_CURSOR_IBEAM;
        editor.scrollY -= GetMouseWheelMove() * LINE_HEIGHT * 3;
    }
    if (editor.cursor > file->length) editor.cursor = file->length;
    editor.cursor = SnapToChar(file->text, file->length, editor.cursor);

    if (editor.focused) {
        int ch;
        while ((ch = GetCharPressed()) > 0)
            if (!readOnly && ch >= 32 && ch < 127) { char typed = (char)ch; WorkFileInsert(file, editor.cursor, &typed, 1); editor.cursor++; changed = true; }

        if (!readOnly && (KeyPressedOrRepeat(KEY_ENTER) || KeyPressedOrRepeat(KEY_KP_ENTER))) {  /* new line, keeping indentation */
            int lineStart = LineStart(file->text, editor.cursor), indent = 0;
            while (lineStart + indent < editor.cursor && file->text[lineStart + indent] == ' ' && indent < 100) indent++;
            char newline[128];
            newline[0] = '\n';
            memset(newline + 1, ' ', (size_t)indent);
            WorkFileInsert(file, editor.cursor, newline, indent + 1);
            editor.cursor += indent + 1;
            changed = true;
        }
        if (!readOnly && KeyPressedOrRepeat(KEY_TAB))             { WorkFileInsert(file, editor.cursor, "    ", 4); editor.cursor += 4; changed = true; }
        if (!readOnly && KeyPressedOrRepeat(KEY_BACKSPACE) && editor.cursor > 0) {
            int pos = PrevChar(file->text, editor.cursor);
            WorkFileDelete(file, pos, editor.cursor - pos); editor.cursor = pos; changed = true;
        }
        if (!readOnly && KeyPressedOrRepeat(KEY_DELETE) && editor.cursor < file->length) {
            WorkFileDelete(file, editor.cursor, NextChar(file->text, file->length, editor.cursor) - editor.cursor); changed = true;
        }
        if (KeyPressedOrRepeat(KEY_LEFT) && editor.cursor > 0)        { editor.cursor = PrevChar(file->text, editor.cursor); moved = true; }
        if (KeyPressedOrRepeat(KEY_RIGHT) && editor.cursor < file->length)  { editor.cursor = NextChar(file->text, file->length, editor.cursor); moved = true; }
        if (IsKeyPressed(KEY_HOME))                   { editor.cursor = LineStart(file->text, editor.cursor); moved = true; }
        if (IsKeyPressed(KEY_END))                    { editor.cursor = LineEnd(file->text, file->length, editor.cursor); moved = true; }
        if (KeyPressedOrRepeat(KEY_UP)) {
            int lineStart = LineStart(file->text, editor.cursor), column = editor.cursor - lineStart;
            if (lineStart > 0) {
                int prevLineStart = LineStart(file->text, lineStart - 1), prevLineLength = (lineStart - 1) - prevLineStart;
                editor.cursor = SnapToChar(file->text, file->length, prevLineStart + (column < prevLineLength ? column : prevLineLength));
            }
            moved = true;
        }
        if (KeyPressedOrRepeat(KEY_DOWN)) {
            int lineStart = LineStart(file->text, editor.cursor), column = editor.cursor - lineStart;
            int lineEnd = LineEnd(file->text, file->length, editor.cursor);
            if (lineEnd < file->length) {
                int nextLineStart = lineEnd + 1, nextLineLength = LineEnd(file->text, file->length, nextLineStart) - nextLineStart;
                editor.cursor = SnapToChar(file->text, file->length, nextLineStart + (column < nextLineLength ? column : nextLineLength));
            }
            moved = true;
        }
    }

    /* click to place the cursor */
    if (editor.focused && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && IsHovered(area)) {
        Vector2 mouse = GetMousePosition();
        int line = (int)((mouse.y - top + editor.scrollY) / LINE_HEIGHT);
        if (line < 0) line = 0;
        int pos = 0, linesPassed = 0;
        while (linesPassed < line && pos < file->length) { if (file->text[pos] == '\n') linesPassed++; pos++; }
        int lineStart = LineStart(file->text, pos), lineEnd = LineEnd(file->text, file->length, lineStart);
        editor.cursor = SnapToChar(file->text, file->length, lineStart + ColumnAtX(file->text + lineStart, lineEnd - lineStart, mouse.x - textX + editor.scrollX));
    }

    /* keep the cursor on screen */
    int totalLines = LineNumberAt(file->text, file->length) + 1;
    if (changed || moved) {
        float cursorY = (float)LineNumberAt(file->text, editor.cursor) * LINE_HEIGHT, viewHeight = area.height - 16;
        if (cursorY < editor.scrollY) editor.scrollY = cursorY;
        if (cursorY + LINE_HEIGHT > editor.scrollY + viewHeight) editor.scrollY = cursorY + LINE_HEIGHT - viewHeight;
        int lineStart = LineStart(file->text, editor.cursor);
        float cursorX = (float)MeasureTextPrefix(file->text + lineStart, editor.cursor - lineStart, FONT_SIZE), viewWidth = area.width - gutter - 30;
        if (cursorX - editor.scrollX > viewWidth) editor.scrollX = cursorX - viewWidth + 40;
        if (cursorX < editor.scrollX) editor.scrollX = cursorX - 40;
        if (editor.scrollX < 0) editor.scrollX = 0;
    }
    float maxScroll = totalLines * LINE_HEIGHT - (area.height - 16);
    if (maxScroll < 0) maxScroll = 0;
    if (editor.scrollY > maxScroll) editor.scrollY = maxScroll;
    if (editor.scrollY < 0) editor.scrollY = 0;

    /* draw */
    DrawRectangleRec(area, UI_BASE);
    BeginScissorMode((int)area.x, (int)area.y, (int)area.width, (int)area.height);
    DrawRectangle((int)area.x, (int)area.y, (int)gutter, (int)area.height, (Color){22, 27, 34, 255});
    const char *text = file->text;
    int pos = 0, line = 0, cursorLine = LineNumberAt(text, editor.cursor);
    int clashSide = 0;                  /* 0 normal, 1 inside "your" side of a clash, 2 inside "their" side */
    int fixAt = -1, fixHow = 0;         /* a "Keep..." link clicked: done after drawing */
    for (;;) {
        int lineEnd = LineEnd(text, file->length, pos);
        float y = top + line * LINE_HEIGHT - editor.scrollY;
        if (y > area.y + area.height) break;
        /* conflict markers colour the lines between them (worked out for every line, even off screen) */
        int marker = 0;                 /* 1 start, 2 middle, 3 end */
        if (LineStartsWith(text, pos, lineEnd, "<<<<<<<")) { marker = 1; }
        else if (clashSide == 1 && IsMidMarker(text, pos, lineEnd)) marker = 2;
        else if (clashSide == 2 && LineStartsWith(text, pos, lineEnd, ">>>>>>>")) marker = 3;
        int side = marker ? 0 : clashSide;
        if (marker == 1) clashSide = 1; else if (marker == 2) clashSide = 2; else if (marker == 3) clashSide = 0;
        if (y + LINE_HEIGHT >= area.y) {
            if (marker)    DrawRectangle((int)(area.x + gutter), (int)y - 2, (int)(area.width - gutter), LINE_HEIGHT, (Color){247, 129, 102, 45});
            else if (side) DrawRectangle((int)(area.x + gutter), (int)y - 2, (int)(area.width - gutter), LINE_HEIGHT,
                                         side == 1 ? (Color){56, 139, 253, 32} : (Color){46, 160, 67, 32});
            if (line == cursorLine && editor.focused)
                DrawRectangle((int)(area.x + gutter), (int)y - 2, (int)(area.width - gutter), LINE_HEIGHT, (Color){255, 255, 255, 10});

            /* light colouring: headings / #include lines, and // comments */
            int firstNonSpace = pos;
            while (firstNonSpace < lineEnd && text[firstNonSpace] == ' ') firstNonSpace++;
            Color color = UI_TEXT;
            if (marker) color = UI_ORANGE;
            else if (firstNonSpace < lineEnd && text[firstNonSpace] == '#') color = UI_ORANGE;
            else if (firstNonSpace + 1 < lineEnd && text[firstNonSpace] == '/' && text[firstNonSpace + 1] == '/') color = UI_MUTED;
            DrawText(DisplaySlice(text + pos, lineEnd - pos), (int)(textX - editor.scrollX), (int)y, FONT_SIZE, color);
            if (marker == 1 && !readOnly) {                  /* one-click ways to settle this clash */
                const char *options[3] = {"Keep yours", "Keep theirs", "Keep both"};
                int linkX = (int)(area.x + area.width) - 16;
                for (int option = 2; option >= 0; option--) {
                    int optionWidth = MeasureText(options[option], FONT_SIZE);
                    linkX -= optionWidth;
                    Rectangle hit = {(float)linkX - 6, y - 2, (float)optionWidth + 12, LINE_HEIGHT};
                    bool linkHovered = IsHovered(hit) && IsHovered(area);
                    DrawRectangleRec(hit, linkHovered ? (Color){56, 139, 253, 90} : (Color){13, 17, 23, 230});
                    DrawText(options[option], linkX, (int)y, FONT_SIZE, option == 0 ? (Color){120, 180, 255, 255} : option == 1 ? UI_GREEN_TEXT : UI_TEXT);
                    if (linkHovered) wantedCursor = MOUSE_CURSOR_POINTING_HAND;
                    if (linkHovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) { fixAt = pos; fixHow = option; clickHandled = true; }
                    linkX -= 20;
                }
            }

            if (line == cursorLine && editor.focused && ((int)(GetTime() * 2) % 2) == 0) {
                int cursorX = (int)(textX - editor.scrollX) + MeasureTextPrefix(text + pos, editor.cursor - pos, FONT_SIZE);
                DrawRectangle(cursorX, (int)y - 2, 2, FONT_SIZE + 4, UI_TEXT);
            }
            /* line numbers, drawn over any text that scrolled left */
            DrawRectangle((int)area.x, (int)y - 2, (int)gutter, LINE_HEIGHT, (Color){22, 27, 34, 255});
            const char *numberText = TextFormat("%d", line + 1);
            DrawText(numberText, (int)(area.x + gutter - 10) - MeasureText(numberText, FONT_SIZE), (int)y, FONT_SIZE,
                     line == cursorLine ? UI_TEXT : UI_MUTED);
        }
        if (lineEnd >= file->length) break;
        pos = lineEnd + 1;
        line++;
    }
    EndScissorMode();
    if (fixAt >= 0 && ResolveConflict(file, fixAt, fixHow)) {
        changed = true;
        editor.cursor = fixAt <= file->length ? fixAt : file->length;
    }
    return changed;
}
