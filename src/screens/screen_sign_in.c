/*
 * screen_sign_in.c - the sign-in screens
 *
 * Sign in, create an account, forgot password, and showing a new recovery
 * code.
 */

#include "screen_sign_in.h"
#include "account_actions.h"
#include "app_state.h"
#include "model.h"
#include "navigation.h"
#include "ui_core.h"
#include "users.h"
#include "widgets.h"

/* Draws the "Reset your password" screen (username, recovery code, new password). */
void ScreenForgot(void)
{
    TextField *fields[] = {&usernameField, &recoveryCodeField, &newPasswordField, &confirmNewPasswordField};
    CycleFocusOnTab(fields, 4);

    float panelWidth = 480, panelX = ((float)screenWidth - panelWidth) / 2;
    DrawTextCentered("Reset your password", 76, 30, UI_TEXT);

    Rectangle panel = {panelX, 124, panelWidth, 452};
    DrawRectangleRec(panel, UI_PANEL);
    DrawRectangleLinesEx(panel, 1, UI_BORDER);
    float x = panelX + 24, y = panel.y, fieldWidth = panelWidth - 48;

    WrapText("Use the recovery code you saved when you created your account.", (int)x, (int)y + 18, (int)fieldWidth, FONT_SIZE, UI_MUTED, true);

    DrawText("Username", (int)x, (int)y + 80, FONT_SIZE, UI_TEXT);
    bool userEnter = TextFieldWidget(&usernameField, (Rectangle){x, y + 104, fieldWidth, 40}, "your-username");

    DrawText("Recovery code", (int)x, (int)y + 156, FONT_SIZE, UI_TEXT);
    bool codeEnter = TextFieldWidget(&recoveryCodeField, (Rectangle){x, y + 180, fieldWidth, 40}, "XXXX-XXXX-XXXX-XXXX");

    DrawText("New password", (int)x, (int)y + 232, FONT_SIZE, UI_TEXT);
    const char *rule = TextFormat("at least %d characters", MIN_PASS);
    DrawText(rule, (int)(x + fieldWidth) - MeasureText(rule, FONT_SIZE), (int)y + 232, FONT_SIZE, newPasswordField.length >= MIN_PASS ? UI_GREEN_TEXT : UI_MUTED);
    bool passwordEnter = TextFieldWidget(&newPasswordField, (Rectangle){x, y + 256, fieldWidth, 40}, "");

    DrawText("Confirm new password", (int)x, (int)y + 308, FONT_SIZE, UI_TEXT);
    if (confirmNewPasswordField.length) {
        bool same = strcmp(newPasswordField.text, confirmNewPasswordField.text) == 0;
        const char *matchText = same ? "matches" : "doesn't match";
        DrawText(matchText, (int)(x + fieldWidth) - MeasureText(matchText, FONT_SIZE), (int)y + 308, FONT_SIZE, same ? UI_GREEN_TEXT : UI_RED_TEXT);
    }
    bool confirmEnter = TextFieldWidget(&confirmNewPasswordField, (Rectangle){x, y + 332, fieldWidth, 40}, "");

    if (signInPauseSeconds > 0) DrawText(TextFormat("Paused - try again in %d s", (int)signInPauseSeconds + 1), (int)x, (int)y + 378, FONT_SIZE, UI_RED_TEXT);
    bool resetPressed = Button((Rectangle){x, y + 392, fieldWidth, 44}, "Reset password", UI_GREEN);

    /* Enter moves to the next empty box, and resets once everything is filled in */
    TextField *firstEmpty = !usernameField.length ? &usernameField : !recoveryCodeField.length ? &recoveryCodeField : !newPasswordField.length ? &newPasswordField : !confirmNewPasswordField.length ? &confirmNewPasswordField : NULL;
    if ((userEnter || codeEnter || passwordEnter || confirmEnter) && firstEmpty) { Unfocus(); firstEmpty->active = true; }
    else if (resetPressed || userEnter || codeEnter || passwordEnter || confirmEnter) DoResetPassword();

    if (SwitchBox(panelX, 592, panelWidth, "Remembered it? ", "Back to sign in")) GoToSignIn();
}

/* Shown once after sign-up, after a reset, or when you make a new code. */
void ScreenRecovery(void)
{
    float panelWidth = 600, panelX = ((float)screenWidth - panelWidth) / 2;
    float x = panelX + 28, innerWidth = panelWidth - 56;  /* inner width: text never goes past it */
    const char *intro = codeAfterReset
        ? "Your old code has been used up, so here is a new one. Keep it somewhere safe, like the old one."
        : "If you ever forget your password, this code lets you choose a new one on the \"Forgot password?\" page.";
    const char *warn  = "This is the only time it will be shown.";
    const char *tip   = "Write it down or copy it into a safe place.";
    const char *later = "Lost it? You can make a new one any time in Account settings.";

    /* measure first, so the panel is exactly as tall as what's inside it */
    int introHeight = WrapText(intro, 0, 0, (int)innerWidth, FONT_SIZE, UI_TEXT, false);
    int warnHeight  = WrapText(warn,  0, 0, (int)innerWidth, FONT_SIZE, UI_TEXT, false);
    int tipHeight   = WrapText(tip,   0, 0, (int)innerWidth, FONT_SIZE, UI_TEXT, false);
    int laterHeight = WrapText(later, 0, 0, (int)innerWidth, FONT_SIZE, UI_TEXT, false);
    float panelHeight = 24 + introHeight + 16 + 80 + 20 + warnHeight + tipHeight + 20 + 44 + 20 + laterHeight + 16;

    DrawTextCentered("Save your recovery code", 90, 30, UI_TEXT);
    Rectangle panel = {panelX, 144, panelWidth, panelHeight};
    DrawRectangleRec(panel, UI_PANEL);
    DrawRectangleLinesEx(panel, 1, UI_YELLOW);

    float y = panel.y + 24;
    y += WrapText(intro, (int)x, (int)y, (int)innerWidth, FONT_SIZE, UI_TEXT, true) + 16;

    Rectangle box = {x, y, innerWidth, 80};
    DrawRectangleRec(box, UI_BG);
    DrawRectangleLinesEx(box, 1, UI_BORDER);
    int codeFontSize = 40;                                /* as big as fits in the box */
    while (codeFontSize > FONT_SIZE && MeasureText(shownRecoveryCode, codeFontSize) > (int)box.width - 40) codeFontSize -= 2;
    int codeWidth = MeasureText(shownRecoveryCode, codeFontSize), codeY = (int)(box.y + (box.height - (float)codeFontSize) / 2);
    DrawText(shownRecoveryCode, (int)(box.x + (box.width - (float)codeWidth) / 2), codeY, codeFontSize, UI_TEXT);
    DrawText(shownRecoveryCode, (int)(box.x + (box.width - (float)codeWidth) / 2) + 1, codeY, codeFontSize, UI_TEXT);
    y += 80 + 20;

    y += WrapText(warn, (int)x, (int)y, (int)innerWidth, FONT_SIZE, UI_YELLOW, true);
    y += WrapText(tip,  (int)x, (int)y, (int)innerWidth, FONT_SIZE, UI_MUTED,  true) + 20;

    if (Button((Rectangle){x, y, 200, 44}, "Copy code", UI_BUTTON)) {
        SetClipboardText(shownRecoveryCode);
        Toast("Recovery code copied to the clipboard");
    }
    bool done = Button((Rectangle){x + innerWidth - 280, y, 280, 44}, "I've saved it - continue", UI_GREEN);
    y += 44 + 20;
    WrapText(later, (int)x, (int)y, (int)innerWidth, FONT_SIZE, UI_MUTED, true);

    if (done || IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) GoHome();
}
/* Draws the sign-in screen. */
void ScreenSignIn(void)
{
    TextField *fields[] = {&usernameField, &passwordField};
    CycleFocusOnTab(fields, 2);

    float panelWidth = 440, panelX = ((float)screenWidth - panelWidth) / 2;
    DrawTextCentered("Sign in to Mini GitHub", 96, 30, UI_TEXT);

    Rectangle panel = {panelX, 150, panelWidth, 296};
    DrawRectangleRec(panel, UI_PANEL);
    DrawRectangleLinesEx(panel, 1, UI_BORDER);
    float x = panelX + 24, y = panel.y, fieldWidth = panelWidth - 48;

    DrawText("Username", (int)x, (int)y + 24, FONT_SIZE, UI_TEXT);
    bool userEnter = TextFieldWidget(&usernameField, (Rectangle){x, y + 50, fieldWidth, 40}, "your-username");
    DrawText("Password", (int)x, (int)y + 108, FONT_SIZE, UI_TEXT);
    if (LinkText("Forgot password?", x + fieldWidth, y + 108, true)) { GoToForgotPassword(); return; }
    bool passwordEnter = TextFieldWidget(&passwordField, (Rectangle){x, y + 134, fieldWidth, 40}, "");
    if (signInPauseSeconds > 0) DrawText(TextFormat("Paused - try again in %d s", (int)signInPauseSeconds + 1), (int)x, (int)y + 188, FONT_SIZE, UI_RED_TEXT);
    bool signInPressed = Button((Rectangle){x, y + 226, fieldWidth, 44}, "Sign in", UI_GREEN);

    if (userEnter && !passwordField.length) { Unfocus(); passwordField.active = true; }  /* Enter in username: jump to password */
    else if (signInPressed || userEnter || passwordEnter) DoSignIn();

    if (SwitchBox(panelX, 466, panelWidth, "New to Mini GitHub? ", "Create an account")) GoToCreateAccount();
    if (userCount == 0) DrawTextCentered("No accounts exist yet - create the first one.", 546, FONT_SIZE, UI_MUTED);
}

/* Draws the "Create your account" screen. */
void ScreenCreateAccount(void)
{
    TextField *fields[] = {&usernameField, &passwordField, &confirmPasswordField};
    CycleFocusOnTab(fields, 3);

    float panelWidth = 460, panelX = ((float)screenWidth - panelWidth) / 2;
    DrawTextCentered("Create your account", 80, 30, UI_TEXT);

    Rectangle panel = {panelX, 130, panelWidth, 404};
    DrawRectangleRec(panel, UI_PANEL);
    DrawRectangleLinesEx(panel, 1, UI_BORDER);
    float x = panelX + 24, y = panel.y, fieldWidth = panelWidth - 48;

    DrawText("Username", (int)x, (int)y + 22, FONT_SIZE, UI_TEXT);
    bool userEnter = TextFieldWidget(&usernameField, (Rectangle){x, y + 48, fieldWidth, 40}, "letters, numbers, - and _");

    DrawText("Password", (int)x, (int)y + 104, FONT_SIZE, UI_TEXT);
    const char *rule = TextFormat("at least %d characters", MIN_PASS);
    DrawText(rule, (int)(x + fieldWidth) - MeasureText(rule, FONT_SIZE), (int)y + 104, FONT_SIZE, passwordField.length >= MIN_PASS ? UI_GREEN_TEXT : UI_MUTED);
    bool passwordEnter = TextFieldWidget(&passwordField, (Rectangle){x, y + 130, fieldWidth, 40}, "");

    DrawText("Confirm password", (int)x, (int)y + 186, FONT_SIZE, UI_TEXT);
    if (confirmPasswordField.length) {
        bool same = strcmp(passwordField.text, confirmPasswordField.text) == 0;
        const char *matchText = same ? "matches" : "doesn't match";
        DrawText(matchText, (int)(x + fieldWidth) - MeasureText(matchText, FONT_SIZE), (int)y + 186, FONT_SIZE, same ? UI_GREEN_TEXT : UI_RED_TEXT);
    }
    bool confirmEnter = TextFieldWidget(&confirmPasswordField, (Rectangle){x, y + 212, fieldWidth, 40}, "");

    if (OrphanCount() > 0 && userCount == 0)
        DrawText("Your existing repositories will move here.", (int)x, (int)y + 272, FONT_SIZE, UI_YELLOW);
    else
        Checkbox(&withExampleRepo, (int)x, (int)y + 272, "Start with an example repository");

    bool createPressed = Button((Rectangle){x, y + 328, fieldWidth, 44}, "Create account", UI_GREEN);
    if (createPressed || userEnter || passwordEnter || confirmEnter) DoCreateAccount();

    if (userCount > 0 && SwitchBox(panelX, 554, panelWidth, "Already have an account? ", "Sign in")) GoToSignIn();
}
