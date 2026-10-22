/*
 * header_bar.c - the bar across the top of every screen
 *
 * The logo, the Back button, your name and picture, and Sign out.
 */

#include "header_bar.h"
#include "app_state.h"
#include "avatars.h"
#include "model.h"
#include "nav_history.h"
#include "navigation.h"
#include "ui_core.h"
#include "users.h"
#include "widgets.h"

/* The Back button in the top bar. */
static void DrawBackButton(void)
{
    if (screen == SCREEN_SIGN_IN || screen == SCREEN_LOAD_FAILED) return;  /* sign-in is where every chain ends */
    Rectangle button = {232, 10, 120, 36};
    if (screen == SCREEN_CREATE_ACCOUNT || screen == SCREEN_FORGOT_PASSWORD) {
        if (Button(button, "< Back", UI_BUTTON)) GoToSignIn();
        if (IsHovered(button)) SetTooltip("Back to sign in", NULL, NULL);
        return;
    }
    if (screen == SCREEN_RECOVERY_CODE) {                               /* the code has been shown: carry on */
        if (Button(button, "< Back", UI_BUTTON)) GoHome();
        if (IsHovered(button)) SetTooltip("Back to your profile", "Make sure you've saved the code first", NULL);
        return;
    }
    int target = NavTarget();
    if (target >= 0) {
        if (Button(button, "< Back", UI_BUTTON)) NavBack();
        else if (IsHovered(button)) SetTooltip(TextFormat("Back to %s", PageLabel(&navHistory[target])), NULL, NULL);
    } else {
        /* nothing earlier: back from here is signing out (asks first) */
        if (ConfirmButton(button, "< Back", "Sign out?", 90)) SignOut();
        else if (IsHovered(button)) SetTooltip("This is the first page since you signed in", "Back from here signs you out", NULL);
    }
}

/* ------------------------------------------------------------------ */
/* Screens                                                            */
/* ------------------------------------------------------------------ */
static void DrawLogo(int centerX, int centerY)
{
    DrawCircle(centerX, centerY, 18, UI_TEXT);
    DrawLineEx((Vector2){(float)centerX - 6, (float)centerY - 10}, (Vector2){(float)centerX - 6, (float)centerY + 10}, 3, UI_BG);
    DrawLineEx((Vector2){(float)centerX + 6, (float)centerY - 6},  (Vector2){(float)centerX - 6, (float)centerY + 4},  3, UI_BG);
    DrawCircle(centerX - 6, centerY - 10, 4, UI_BG);
    DrawCircle(centerX - 6, centerY + 10, 4, UI_BG);
    DrawCircle(centerX + 6, centerY - 6, 4, UI_BG);
}

/* Draws the bar across the top of the window and handles its buttons. */
void DrawHeader(void)
{
    DrawRectangle(0, 0, screenWidth, 56, UI_PANEL);
    DrawLine(0, 56, screenWidth, 56, UI_BORDER);
    DrawLogo(38, 28);
    DrawTextBold("Mini GitHub", 68, 18, FONT_SIZE, UI_TEXT);
    DrawBackButton();                      /* the same place on every page */
    if (currentUserId < 0) return;

    Rectangle homeArea = {18, 8, 200, 40};
    if (IsHovered(homeArea)) wantedCursor = MOUSE_CURSOR_POINTING_HAND;
    if (WasClicked(homeArea)) GoHome();

    if (Button((Rectangle){(float)screenWidth - 130, 10, 112, 36}, "Sign out", UI_BUTTON)) { SignOut(); return; }

    /* name + avatar: click to open your profile */
    const char *name = UserName(currentUserId);
    int nameWidth = MeasureText(name, FONT_SIZE);
    int accountWidth = nameWidth + 8 + 12 + 32 + 8;
    Rectangle accountArea = {(float)(screenWidth - 146 - accountWidth), 8, (float)accountWidth, 40};
    if (IsHovered(accountArea)) {
        wantedCursor = MOUSE_CURSOR_POINTING_HAND;
        DrawRectangleRounded(accountArea, 0.3f, 6, UI_HOVER);
    }
    DrawText(name, (int)accountArea.x + 8, 18, FONT_SIZE, UI_TEXT);
    DrawAvatar(currentUserId, (int)accountArea.x + 8 + nameWidth + 12 + 16, 28, 16);
    if (WasClicked(accountArea)) OpenProfile(currentUserId);
}
