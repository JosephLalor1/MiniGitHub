/*
 * screen_load_fail.c - when the save file can't be read
 *
 * Nothing is overwritten until you choose what to do.
 */

#include "screen_load_fail.h"
#include "app_state.h"
#include "git_server.h"
#include "model.h"
#include "navigation.h"
#include "save_file.h"
#include "ui_core.h"
#include "users.h"
#include "widgets.h"

/* ---------- when the save file can't be read ----------
   Nothing is deleted or saved over: the file stays exactly as it is until
   you choose. Starting empty first moves it aside under a new name (never
   replacing another file), so it can always be brought back. */
bool quitNow;

/* "minigithub.dat.unreadable-2026-10-21-1103", or "...-2" etc. if that's taken */
static const char *SetAsideName(void)
{
    static char name[128];
    time_t now = time(NULL);
    char stamp[32];
    strftime(stamp, sizeof stamp, "%Y-%m-%d-%H%M%S", localtime(&now));
    snprintf(name, sizeof name, "%s.unreadable-%s", SAVE_FILE, stamp);
    for (int i = 2; FileExists(name) && i < 1000; i++) snprintf(name, sizeof name, "%s.unreadable-%s-%d", SAVE_FILE, stamp, i);
    return name;
}

/* Gets going after the save file loads: works out commit authors, starts the git server and shows
   sign-in (or create-account, if there are no accounts). */
void AfterLoad(void)
{
    loadFailed = false;
    RecomputeAuthors();
    GitServerStart();
    if (userCount == 0) {
        GoToCreateAccount();
        if (OrphanCount() > 0) Toast("Create an account to take over your existing repositories");
    } else GoToSignIn();
}

/* Draws the screen explaining that the save file couldn't be read, with the ways forward. */
void ScreenLoadFail(void)
{
    float width = screenWidth - 120 > 900 ? 900 : (float)screenWidth - 120;
    Rectangle panel = {((float)screenWidth - width) / 2, 100, width, (float)screenHeight - 160};
    DrawRectangleRec(panel, UI_PANEL);
    DrawRectangleLinesEx(panel, 1, UI_ORANGE);
    int x = (int)panel.x + 32, y = (int)panel.y + 28, textWidth = (int)width - 64;
    DrawTextBold("Mini GitHub couldn't read its save file", x, y, 30, UI_ORANGE);
    y += 56;
    y += WrapText("Nothing has been deleted, and nothing will be saved over it. The file is still there, exactly as it was:", x, y, textWidth, FONT_SIZE, UI_TEXT, true) + 8;
    y += WrapText(TextFormat("%s   (in %s)", SAVE_FILE, GetWorkingDirectory()), x + 20, y, textWidth - 20, FONT_SIZE, UI_LINK, true) + 16;
    y += WrapText(TextFormat("What went wrong: %s%s%s.", loadReason, loadContext[0] ? ", in " : "", loadContext), x, y, textWidth, FONT_SIZE, UI_MUTED, true) + 16;
    bool hasLastGood = FileExists(SAVE_FILE ".last-good");
    if (hasLastGood)
        y += WrapText("There's also a copy of your data from the last time Mini GitHub opened without problems (minigithub.dat.last-good).",
                      x, y, textWidth, FONT_SIZE, UI_MUTED, true) + 16;
    y += WrapText("If this file came from a newer version of Mini GitHub, quit and open it with that version instead.", x, y, textWidth, FONT_SIZE, UI_MUTED, true);

    float buttonY = panel.y + panel.height - 70, buttonX = panel.x + 32;
    if (Button((Rectangle){buttonX, buttonY, 140, 44}, "Quit", UI_BUTTON)) quitNow = true;
    buttonX += 156;
    if (hasLastGood) {
        if (ConfirmButton((Rectangle){buttonX, buttonY, 330, 44}, "Use the last good copy", "Click again to use it", 70)) {
            const char *aside = SetAsideName();
            if (rename(SAVE_FILE, aside) != 0) { Toast("Couldn't move the unreadable file aside"); return; }
            bool ok = CopyFileBytes(SAVE_FILE ".last-good", SAVE_FILE);
            loadFailed = false;
            if (ok && Load()) { AfterLoad(); Toast(TextFormat("Restored the last good copy - the unreadable file is kept as %s", GetFileName(aside))); }
            else { rename(aside, SAVE_FILE); loadFailed = true; Toast("That copy couldn't be read either - nothing was changed"); }
            return;
        }
        buttonX += 346;
    }
    if (ConfirmButton((Rectangle){buttonX, buttonY, 300, 44}, "Start empty", "Click again to start empty", 71)) {
        const char *aside = SetAsideName();
        if (rename(SAVE_FILE, aside) != 0) { Toast("Couldn't move the unreadable file aside - nothing was changed"); return; }
        AfterLoad();
        Toast(TextFormat("Started empty - your old file is kept as %s", GetFileName(aside)));
    }
}
