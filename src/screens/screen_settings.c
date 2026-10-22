/*
 * screen_settings.c - the settings screen
 *
 * Profile (picture, colour, bio, theme), password & account, and git
 * (server status, email).
 */

#include "screen_settings.h"
#include "account_actions.h"
#include "app_state.h"
#include "avatars.h"
#include "model.h"
#include "navigation.h"
#include "passwords.h"
#include "themes.h"
#include "ui_core.h"
#include "users.h"
#include "widgets.h"

/* Makes the picture at path the user's profile picture, and says how it went. */
void UseAvatarPath(User *user, const char *path)
{
    const char *error = ImportAvatar(user, path);
    if (error) Toast(error);
    else { TextFieldClear(&avatarPathField); avatarPathField.active = false; Toast("Profile picture updated"); }
}

static void SettingsProfileTab(User *user, float top)
{
    /* ---- left: picture, colour, bio ---- */
    Rectangle leftPanel = {20, top, 460, (float)screenHeight - 20 - top};
    DrawRectangleRec(leftPanel, UI_PANEL);
    DrawRectangleLinesEx(leftPanel, 1, UI_BORDER);
    float x = leftPanel.x + 24, y = top, fieldWidth = leftPanel.width - 48;

    DrawTextBold("Profile picture", (int)x, (int)y + 18, FONT_SIZE, UI_TEXT);
    DrawCircle((int)x + 56, (int)y + 112, 60, THEMES[UserTheme(user->id)].accent);
    DrawAvatar(user->id, (int)x + 56, (int)y + 112, 56);
    int textX = (int)x + 136;
    DrawText("Drag a PNG or JPG picture",  textX, (int)y + 62, FONT_SIZE, UI_MUTED);
    DrawText("onto this window, or paste", textX, (int)y + 86, FONT_SIZE, UI_MUTED);
    DrawText("its file path below.",       textX, (int)y + 110, FONT_SIZE, UI_MUTED);
    if (user->avatar && Button((Rectangle){(float)textX, y + 142, 180, 34}, "Remove picture", UI_BUTTON)) {
        RemoveAvatar(user);
        Toast("Profile picture removed");
    }
    bool enter = TextFieldWidget(&avatarPathField, (Rectangle){x, y + 190, fieldWidth - 100, 40}, "e.g. C:/Users/jo/Pictures/me.jpg");
    if (Button((Rectangle){x + fieldWidth - 90, y + 190, 90, 40}, "Use", UI_GREEN) || enter) UseAvatarPath(user, avatarPathField.text);

    DrawText("Circle colour (when there's no picture)", (int)x, (int)y + 248, FONT_SIZE, UI_TEXT);
    for (int i = 0; i <= 8; i++) {
        Vector2 swatch = {x + 16 + i * 44.0f, y + 292};
        Rectangle hit = {swatch.x - 18, swatch.y - 18, 36, 36};
        bool selected = user->avatarColor == i;
        if (selected) DrawCircleV(swatch, 19, UI_TEXT);
        if (i == 0) {                                   /* "Auto": the colour picked for you */
            DrawCircleV(swatch, 16, AVATAR_COLORS[user->id % 6]);
            DrawText("A", (int)swatch.x - MeasureText("A", FONT_SIZE) / 2, (int)swatch.y - 10, FONT_SIZE, UI_TEXT);
        } else DrawCircleV(swatch, 16, AVATAR_COLORS[i - 1]);
        if (IsHovered(hit)) wantedCursor = MOUSE_CURSOR_POINTING_HAND;
        if (WasClicked(hit)) { user->avatarColor = i; MarkDirty(); }
    }

    DrawText("Bio", (int)x, (int)y + 336, FONT_SIZE, UI_TEXT);
    const char *countText = TextFormat("%d / %d", bioField.length, BIO_LEN);
    DrawText(countText, (int)(x + fieldWidth) - MeasureText(countText, FONT_SIZE), (int)y + 336, FONT_SIZE, bioField.length > BIO_LEN ? UI_RED_TEXT : UI_MUTED);
    bool bioEnter = TextFieldWidget(&bioField, (Rectangle){x, y + 362, fieldWidth, 40}, "A few words about you");
    bool changed = strcmp(bioField.text, user->bio) != 0;
    if (ButtonEnabled((Rectangle){x, y + 414, 140, 38}, "Save bio", changed ? UI_GREEN : UI_BUTTON, changed) || (bioEnter && changed)) {
        if (bioField.length > BIO_LEN) Toast(TextFormat("Bios can be at most %d characters", BIO_LEN));
        else { snprintf(user->bio, sizeof user->bio, "%s", bioField.text); MarkDirty(); Toast("Bio saved"); }
    }
    if (changed) DrawText("not saved yet", (int)x + 156, (int)y + 423, FONT_SIZE, UI_YELLOW);

    /* ---- right: theme picker ---- */
    Rectangle rightPanel = {500, top, (float)screenWidth - 520, (float)screenHeight - 20 - top};
    DrawRectangleRec(rightPanel, UI_PANEL);
    DrawRectangleLinesEx(rightPanel, 1, UI_BORDER);
    DrawTextBold("Theme", (int)rightPanel.x + 24, (int)top + 18, FONT_SIZE, UI_TEXT);
    DrawTextFit("Shown behind the app while you're signed in, and on your profile page.",
            (int)rightPanel.x + 24, (int)top + 46, FONT_SIZE, (int)rightPanel.width - 48, UI_MUTED);

    Rectangle listArea = {rightPanel.x + 1, top + 84, rightPanel.width - 2, rightPanel.height - 86};
    float innerWidth = listArea.width - 46;
    int columnCount = (int)((innerWidth + 14) / (170 + 14));
    if (columnCount < 2) columnCount = 2;
    float cardWidth = (innerWidth - (columnCount - 1) * 14) / columnCount, cardHeight = cardWidth * 0.5f;
    if (cardHeight > 100) cardHeight = 100;
    float rowHeight = cardHeight + 66;
    int rowCount = (THEME_COUNT + columnCount - 1) / columnCount;
    ScrollArea(listArea, &settingsScroll, rowCount * rowHeight);

    for (int i = 0; i < THEME_COUNT; i++) {
        float cardX = listArea.x + 23 + (i % columnCount) * (cardWidth + 14), cardY = listArea.y + (i / columnCount) * rowHeight - settingsScroll;
        Rectangle card = {cardX, cardY, cardWidth, cardHeight};
        Rectangle visible = IntersectRects(card, listArea);
        if (visible.height <= 0) continue;
        DrawRectangleRec(visible, UI_BASE);
        DrawThemeClip(i, card, visible, 1.0f);
        BeginScissorMode((int)visible.x, (int)visible.y, (int)visible.width, (int)visible.height);
        bool selected = user->theme == i, hovered = IsHovered(listArea) && IsHovered(card);
        DrawRectangleLinesEx(card, selected ? 3.0f : 1.0f, selected ? THEMES[i].accent : hovered ? UI_TEXT : UI_BORDER);
        EndScissorMode();
        if (cardY + cardHeight + 50 <= listArea.y + listArea.height && cardY + cardHeight >= listArea.y) {
            DrawTextBold(THEMES[i].name, (int)cardX, (int)(cardY + cardHeight + 8), FONT_SIZE, selected ? THEMES[i].accent : UI_TEXT);
            DrawTextFit(THEMES[i].blurb, (int)cardX, (int)(cardY + cardHeight + 32), FONT_SIZE, (int)cardWidth, UI_MUTED);
        }
        if (hovered) wantedCursor = MOUSE_CURSOR_POINTING_HAND;
        if (IsHovered(listArea) && WasClicked(card) && !selected) {
            user->theme = i;
            MarkDirty();
            Toast(TextFormat("Theme changed to %s", THEMES[i].name));
        }
    }
    DrawScrollbar(listArea, settingsScroll, rowCount * rowHeight);
}

static void SettingsAccountTab(User *user, float top)
{
    TextField *fields[] = {&currentPasswordField, &newPasswordField, &confirmNewPasswordField, &deletePasswordField};
    CycleFocusOnTab(fields, 4);
    int owned = ReposOwnedBy(user->id);

    float panelWidth = ((float)screenWidth - 60) / 2;
    Rectangle passwordPanel = {20, top, panelWidth, 336}, deletePanel = {40 + panelWidth, top, panelWidth, 336};
    float fieldWidth = panelWidth - 48;

    /* change password */
    DrawRectangleRec(passwordPanel, UI_PANEL);
    DrawRectangleLinesEx(passwordPanel, 1, UI_BORDER);
    float x = passwordPanel.x + 24, y = passwordPanel.y;
    DrawTextBold("Change password", (int)x, (int)y + 18, FONT_SIZE, UI_TEXT);
    DrawText("Current password", (int)x, (int)y + 54, FONT_SIZE, UI_MUTED);
    bool currentEnter = TextFieldWidget(&currentPasswordField, (Rectangle){x, y + 78, fieldWidth, 40}, "");
    DrawText("New password", (int)x, (int)y + 126, FONT_SIZE, UI_MUTED);
    bool newEnter = TextFieldWidget(&newPasswordField, (Rectangle){x, y + 150, fieldWidth, 40}, "");
    DrawText("Confirm new password", (int)x, (int)y + 198, FONT_SIZE, UI_MUTED);
    bool confirmEnter = TextFieldWidget(&confirmNewPasswordField, (Rectangle){x, y + 222, fieldWidth, 40}, "");
    if (Button((Rectangle){x, y + 276, 220, 44}, "Update password", UI_GREEN) || currentEnter || newEnter || confirmEnter) DoChangePassword();

    /* delete account */
    DrawRectangleRec(deletePanel, UI_PANEL);
    DrawRectangleLinesEx(deletePanel, 1, UI_DANGER);
    x = deletePanel.x + 24; y = deletePanel.y;
    DrawTextBold("Delete account", (int)x, (int)y + 18, FONT_SIZE, UI_RED_TEXT);
    DrawText("This permanently deletes your account", (int)x, (int)y + 54, FONT_SIZE, UI_MUTED);
    DrawText(TextFormat("and all %d of your repositories.", owned), (int)x, (int)y + 80, FONT_SIZE, UI_MUTED);
    DrawText("This can't be undone.", (int)x, (int)y + 106, FONT_SIZE, UI_MUTED);
    DrawText("Type your password to confirm", (int)x, (int)y + 198, FONT_SIZE, UI_TEXT);
    TextFieldWidget(&deletePasswordField, (Rectangle){x, y + 222, fieldWidth, 40}, "");
    if (ConfirmButton((Rectangle){x, y + 276, 260, 44}, "Delete my account", "Click again to delete", 4)) { DoDeleteAccount(); return; }

    /* recovery code */
    bool hasCode = HasRecoveryCode(user);
    Rectangle codePanel = {20, top + 352, (float)screenWidth - 40, 88};
    Rectangle codeButton = {codePanel.x + codePanel.width - 24 - 340, codePanel.y + 22, 340, 44};
    int textWidth = (int)(codeButton.x - (codePanel.x + 24) - 24);  /* stop before the button */
    DrawRectangleRec(codePanel, UI_PANEL);
    DrawRectangleLinesEx(codePanel, 1, hasCode ? UI_BORDER : UI_YELLOW);
    DrawTextBold("Recovery code", (int)codePanel.x + 24, (int)codePanel.y + 14, FONT_SIZE, UI_TEXT);
    WrapText(hasCode ? "Use it if you ever forget your password."
                 : "None yet - you need one to reset a lost password.",
             (int)codePanel.x + 24, (int)codePanel.y + 44, textWidth, FONT_SIZE, hasCode ? UI_MUTED : UI_YELLOW, true);
    bool makeCode = hasCode ? ConfirmButton(codeButton, "Make a new code", "Click again - old code stops working", 5)
                    : Button(codeButton, "Create recovery code", UI_GREEN);
    if (makeCode) ShowRecoveryCode(user, false);
}
/* ---------- Settings > Git ---------- */
/* An email git can use: one @, no spaces or < > (they'd break git's author line). "" is fine (= none). */
static bool UsableEmail(const char *email)
{
    if (!email[0]) return true;
    const char *at = strchr(email, '@');
    if (!at || at == email || !at[1] || strchr(at + 1, '@')) return false;
    for (const char *ch = email; *ch; ch++) if (*ch == ' ' || *ch == '<' || *ch == '>' || (unsigned char)*ch < 32) return false;
    return true;
}

static void SettingsGitTab(User *user, float top)
{
    /* the server */
    Rectangle serverPanel = {20, top, (float)screenWidth - 40, 222};
    DrawRectangleRec(serverPanel, UI_PANEL);
    DrawRectangleLinesEx(serverPanel, 1, UI_BORDER);
    float x = serverPanel.x + 24, y = serverPanel.y, fieldWidth = serverPanel.width - 48;
    DrawTextBold("Using real git", (int)x, (int)y + 18, FONT_SIZE, UI_TEXT);
    if (gitPort) {
        DrawCircle((int)x + 6, (int)y + 60, 6, UI_GREEN_TEXT);
        DrawTextFit(TextFormat("Mini GitHub's git server is running at http://localhost:%d  (only this computer can reach it)", gitPort),
                (int)x + 22, (int)y + 50, FONT_SIZE, (int)fieldWidth - 22, UI_TEXT);
    } else {
        DrawCircle((int)x + 6, (int)y + 60, 6, UI_RED_TEXT);
        DrawTextFit(TextFormat("The git server isn't running: %s", gitProblem), (int)x + 22, (int)y + 50, FONT_SIZE, (int)fieldWidth - 22, UI_ORANGE);
    }
    DrawTextFit("1.  Open a repository, press Code, and copy its address.", (int)x, (int)y + 88, FONT_SIZE, (int)fieldWidth, UI_MUTED);
    DrawTextFit("2.  In a terminal, type  git clone  and paste the address.", (int)x, (int)y + 114, FONT_SIZE, (int)fieldWidth, UI_MUTED);
    DrawTextFit("3.  Change files, git commit, then git push. When git asks, use your Mini GitHub username and password.",
            (int)x, (int)y + 140, FONT_SIZE, (int)fieldWidth, UI_MUTED);
    DrawTextFit("To bring in a project you already have: make a new repository without a README, then git push to its address.",
            (int)x, (int)y + 176, FONT_SIZE, (int)fieldWidth, UI_MUTED);

    /* the email */
    Rectangle emailPanel = {20, top + 238, (float)screenWidth - 40, 186};
    DrawRectangleRec(emailPanel, UI_PANEL);
    DrawRectangleLinesEx(emailPanel, 1, UI_BORDER);
    x = emailPanel.x + 24; y = emailPanel.y;
    DrawTextBold("Your email on commits", (int)x, (int)y + 18, FONT_SIZE, UI_TEXT);
    DrawTextFit("Commits you make here show it in git. Pushed commits with this email count as yours.",
            (int)x, (int)y + 46, FONT_SIZE, (int)fieldWidth, UI_MUTED);
    char placeholder[TEXT_LEN];
    snprintf(placeholder, sizeof placeholder, "%s@minigithub.local (if you leave it empty)", user->name);
    bool enter = TextFieldWidget(&emailField, (Rectangle){x, y + 84, fieldWidth - 150, 40}, placeholder);
    bool changed = strcmp(emailField.text, user->email) != 0;
    if (ButtonEnabled((Rectangle){x + fieldWidth - 140, y + 84, 140, 40}, "Save email", changed ? UI_GREEN : UI_BUTTON, changed) || (enter && changed)) {
        if (!UsableEmail(emailField.text)) Toast("That doesn't look like an email address");
        else { snprintf(user->email, sizeof user->email, "%s", emailField.text); RecomputeAuthors(); MarkDirty(); Toast(user->email[0] ? "Email saved" : "Email removed"); }
    }
    /* other emails you've said are you ("That's me" on a repository's People tab) */
    int chipX = (int)x, chipY = (int)y + 142;
    if (!user->claimCount) DrawTextFit("Other emails of yours: none yet. Use \"That's me\" on a repository's People tab to add one.", chipX, chipY, FONT_SIZE, (int)fieldWidth, UI_MUTED);
    else {
        DrawText("Also you:", chipX, chipY, FONT_SIZE, UI_MUTED);
        chipX += MeasureText("Also you:", FONT_SIZE) + 12;
        int removeIndex = -1;
        for (int k = 0; k < user->claimCount; k++) {
            int textWidth = MeasureText(user->claims[k], FONT_SIZE);
            Rectangle chip = {(float)chipX, (float)chipY - 3, (float)textWidth + 44, 28};
            if (chip.x + chip.width > x + fieldWidth) { DrawText(TextFormat("+%d more", user->claimCount - k), chipX, chipY, FONT_SIZE, UI_MUTED); break; }
            DrawRectangleRounded(chip, 0.5f, 6, UI_BUTTON);
            DrawText(user->claims[k], chipX + 10, chipY, FONT_SIZE, UI_TEXT);
            Rectangle removeRect = {chip.x + chip.width - 28, chip.y + 2, 24, 24};
            bool removeHovered = IsHovered(removeRect);
            if (removeHovered) { wantedCursor = MOUSE_CURSOR_POINTING_HAND; SetTooltip("Not me", "Their commits stop counting as yours", NULL); }
            DrawText("x", (int)removeRect.x + 7, chipY, FONT_SIZE, removeHovered ? UI_RED_TEXT : UI_MUTED);
            if (WasClicked(removeRect)) removeIndex = k;
            chipX += (int)chip.width + 8;
        }
        if (removeIndex >= 0) {
            for (int k = removeIndex; k + 1 < user->claimCount; k++) memcpy(user->claims[k], user->claims[k + 1], TEXT_LEN);
            user->claimCount--;
            RecomputeAuthors();
            MarkDirty();
            Toast("Removed - those commits no longer count as yours");
        }
    }
}

/* Draws the settings screen (whichever tab is chosen). */
void ScreenSettings(void)
{
    User *user = FindUserById(currentUserId);
    if (!user) { SignOut(); return; }

    DrawAvatar(user->id, 60, 112, 40);
    DrawTextBold(user->name, 120, 84, 30, UI_TEXT);
    DrawText("Your settings", 120, 120, FONT_SIZE, UI_MUTED);

    int tabX = 20;
    const char *labels[3] = {"Profile", "Password & account", "Git"};
    for (int tabIndex = 0; tabIndex < 3; tabIndex++) {
        int tabWidth = MeasureText(labels[tabIndex], FONT_SIZE) + 28;
        Rectangle tabRect = {(float)tabX, 156, (float)tabWidth, 38};
        bool selected = settingsTab == tabIndex;
        if (IsHovered(tabRect)) {
            wantedCursor = MOUSE_CURSOR_POINTING_HAND;
            if (!selected) DrawRectangleRounded((Rectangle){(float)tabX, 158, (float)tabWidth, 30}, 0.3f, 6, UI_HOVER);
        }
        DrawText(labels[tabIndex], tabX + 14, 164, FONT_SIZE, selected ? UI_TEXT : UI_MUTED);
        if (selected) DrawRectangle(tabX, 191, tabWidth, 3, accentColor);
        if (WasClicked(tabRect) && !selected) { Unfocus(); ClearSecrets(); settingsTab = tabIndex; }
        tabX += tabWidth + 6;
    }
    DrawLine(0, 194, screenWidth, 194, UI_BORDER);

    if (settingsTab == 0)      SettingsProfileTab(user, 210);
    else if (settingsTab == 1) SettingsAccountTab(user, 210);
    else                       SettingsGitTab(user, 210);
}
