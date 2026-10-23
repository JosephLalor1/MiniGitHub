/*
 * tab_settings.c - the Settings tab
 *
 * Only the owner sees it: who can see the repository (public / private),
 * and deleting it.
 */

#include "tab_settings.h"
#include "app_state.h"
#include "navigation.h"
#include "repo.h"
#include "ui_core.h"
#include "users.h"
#include "widgets.h"

/* Makes a repository private, or public again. People who can no longer see it
   don't keep a star on it. */
static void FlipVisibility(Repo *repo)
{
    repo->isPrivate = !repo->isPrivate;
    if (repo->isPrivate)
        for (int i = repo->starCount - 1; i >= 0; i--) if (!CanSee(repo, repo->starredBy[i])) RemoveStar(repo, repo->starredBy[i]);
    MarkDirty();
    Toast(repo->isPrivate ? "Now private: only you, collaborators and people you invite can see it"
                          : "Now public: everyone can see it and fork it");
}

/* Draws a repository's Settings tab (anyone but the owner is sent to the Code tab). */
void TabSettings(Repo *repo)
{
    if (!IsMine(repo)) { repoTab = TAB_CODE; return; }
    float width = (float)screenWidth - 40 < 980 ? (float)screenWidth - 40 : 980;
    float x = 20, y = 196, textWidth = width - 48 - 220;

    /* ---- who can see it ---- */
    Rectangle visibilityPanel = {x, y, width, 132};
    DrawRectangleRec(visibilityPanel, UI_PANEL);
    DrawRectangleLinesEx(visibilityPanel, 1, UI_BORDER);
    DrawTextBold("Visibility", (int)x + 24, (int)y + 20, FONT_SIZE, UI_TEXT);
    const char *nowText = repo->isPrivate ? "This repository is private." : "This repository is public.";
    const char *meaning = repo->isPrivate ? "Only you, your collaborators and people you invite can see it."
                                          : "Everyone on this computer can see it, star it and fork it.";
    DrawTextFit(nowText, (int)x + 24, (int)y + 54, FONT_SIZE, (int)textWidth, repo->isPrivate ? UI_YELLOW : UI_TEXT);
    DrawTextFit(meaning, (int)x + 24, (int)y + 80, FONT_SIZE, (int)textWidth, UI_MUTED);
    Rectangle visibilityButton = {x + width - 24 - 200, y + 46, 200, 40};
    if (ConfirmButton(visibilityButton, repo->isPrivate ? "Make public" : "Make private", "Sure? Click again", 95)) FlipVisibility(repo);

    /* ---- deleting it ---- */
    y += 132 + 20;
    Rectangle dangerPanel = {x, y, width, 132};
    DrawRectangleRec(dangerPanel, UI_PANEL);
    DrawRectangleLinesEx(dangerPanel, 1, UI_RED_TEXT);
    DrawTextBold("Danger zone", (int)x + 24, (int)y + 20, FONT_SIZE, UI_RED_TEXT);
    DrawTextFit("Delete this repository", (int)x + 24, (int)y + 54, FONT_SIZE, (int)textWidth, UI_TEXT);
    DrawTextFit("Its files, history, branches and issues are removed for good.", (int)x + 24, (int)y + 80, FONT_SIZE, (int)textWidth, UI_MUTED);
    if (ConfirmButton((Rectangle){x + width - 24 - 200, y + 46, 200, 40}, "Delete repository", "Really delete?", 1)) {
        int index = currentRepo;
        GoHome();
        DeleteRepo(index);
        MarkDirty();
        Toast("Repository deleted");
    }
}
