/*
 * header_bar.c - the bar across the top of every screen
 *
 * The logo, the Back button, your name and picture, and Sign out.
 */

#include "header_bar.h"
#include "animation.h"
#include "app_state.h"
#include "avatars.h"
#include "model.h"
#include "nav_history.h"
#include "navigation.h"
#include "themes.h"
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
/* The logo - a circle with git's branch mark cut out of it - centred on
   (centerX, centerY), at scale times its normal size (a radius of 18). */
void DrawLogoScaled(float centerX, float centerY, float scale, Color body, Color cutout)
{
    DrawCircleV((Vector2){centerX, centerY}, 18 * scale, body);
    DrawLineEx((Vector2){centerX - 6 * scale, centerY - 10 * scale}, (Vector2){centerX - 6 * scale, centerY + 10 * scale}, 3 * scale, cutout);
    DrawLineEx((Vector2){centerX + 6 * scale, centerY - 6 * scale},  (Vector2){centerX - 6 * scale, centerY + 4 * scale},  3 * scale, cutout);
    DrawCircleV((Vector2){centerX - 6 * scale, centerY - 10 * scale}, 4 * scale, cutout);
    DrawCircleV((Vector2){centerX - 6 * scale, centerY + 10 * scale}, 4 * scale, cutout);
    DrawCircleV((Vector2){centerX + 6 * scale, centerY - 6 * scale}, 4 * scale, cutout);
}

/* The logo at its normal size, in the top bar. */
static void DrawLogo(int centerX, int centerY) { DrawLogoScaled((float)centerX, (float)centerY, 1.0f, UI_TEXT, UI_BG); }

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
    if (IsHovered(accountArea)) wantedCursor = MOUSE_CURSOR_POINTING_HAND;
    float glow = HoverAmount(AnimationKey("account", 0, 0), IsHovered(accountArea));
    if (glow > 0) DrawRectangleRounded(accountArea, 0.3f, 6, WithAlpha(UI_HOVER, glow));
    DrawText(name, (int)accountArea.x + 8, 18, FONT_SIZE, UI_TEXT);
    DrawAvatar(currentUserId, (int)accountArea.x + 8 + nameWidth + 12 + 16, 28, 16);
    if (WasClicked(accountArea)) OpenProfile(currentUserId);
}
