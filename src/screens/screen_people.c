/*
 * screen_people.c - the People screen
 *
 * Everyone with an account on this computer, with a Follow button for each.
 */

#include "screen_people.h"
#include "animation.h"
#include "app_state.h"
#include "avatars.h"
#include "model.h"
#include "navigation.h"
#include "themes.h"
#include "ui_core.h"
#include "users.h"
#include "util.h"
#include "widgets.h"

/* ---------------- everyone on this computer ---------------- */
/* Draws the list of everyone with an account on this computer. */
void ScreenPeople(void)
{
    DrawTextBold("People on this computer", 20, 80, 30, UI_TEXT);
    DrawTextFit("Click someone to see their profile. Follow people to see their repositories under Following on your home page.", 20, 118, FONT_SIZE, screenWidth - 40, UI_MUTED);

    Rectangle listArea = {20, 160, (float)screenWidth - 40, (float)screenHeight - 180};
    float rowHeight = 104;
    ScrollArea(listArea, &peopleScroll, userCount * rowHeight - 10);
    BeginScissorMode((int)listArea.x, (int)listArea.y, (int)listArea.width, (int)listArea.height);
    for (int i = 0; i < userCount; i++) {
        User *user = &users[i];
        Rectangle card = {listArea.x, listArea.y + i * rowHeight - peopleScroll, listArea.width - 12, rowHeight - 10};
        if (card.y > listArea.y + listArea.height || card.y + card.height < listArea.y) continue;
        bool hovered = IsHovered(listArea) && IsHovered(card);
        if (hovered) wantedCursor = MOUSE_CURSOR_POINTING_HAND;
        DrawRectangleRec(card, CardColor(AnimationKey("person card", (float)user->id, 0), hovered));

        /* a strip of their theme along the right-hand side */
        float stripWidth = card.width * 0.3f;
        Rectangle strip = {card.x + card.width - stripWidth, card.y, stripWidth, card.height};
        Rectangle visible = IntersectRects(strip, listArea);
        if (visible.height > 0) {
            DrawRectangleRec(visible, UI_BASE);
            DrawThemeClip(UserTheme(user->id), strip, visible, 1.0f);  /* this ends the list's clipping... */
            BeginScissorMode((int)listArea.x, (int)listArea.y, (int)listArea.width, (int)listArea.height);  /* ...so turn it back on */
        }
        DrawRectangleLinesEx(card, 1, UI_BORDER);

        DrawAvatar(user->id, (int)card.x + 50, (int)(card.y + card.height / 2), 32);
        bool isYou = user->id == currentUserId;
        float followWidth = isYou ? 0 : 130;                 /* room for the Follow button */
        int textX = (int)card.x + 100, textWidth = (int)(card.width - stripWidth - followWidth) - 120;
        DrawTextBold(user->name, textX, (int)card.y + 12, FONT_SIZE, UI_LINK);
        const char *nameNote = isYou ? "(you)" : IsFollowing(user->id, currentUserId) ? "follows you" : NULL;
        if (nameNote) DrawText(nameNote, textX + MeasureText(user->name, FONT_SIZE) + 12, (int)card.y + 12, FONT_SIZE, UI_MUTED);
        if (!isYou) {
            bool following = IsFollowing(currentUserId, user->id);
            Rectangle followButton = {card.x + card.width - stripWidth - followWidth, card.y + (card.height - 36) / 2, followWidth - 16, 36};
            if (ButtonEnabled(followButton, following ? "Unfollow" : "Follow", following ? UI_BUTTON : UI_GREEN, IsHovered(listArea))) {
                ToggleFollow(currentUserId, user->id);
                MarkDirty();
                Toast(following ? TextFormat("You've stopped following %s", user->name) : TextFormat("Following %s", user->name));
            }
        }
        DrawTextFit(user->bio[0] ? user->bio : "No bio yet", textX, (int)card.y + 38, FONT_SIZE, textWidth, user->bio[0] ? UI_TEXT : UI_MUTED);
        int ownedCount = 0;
        for (int repoIndex = 0; repoIndex < repoCount; repoIndex++) if (repos[repoIndex].owner == user->id && CanSee(&repos[repoIndex], currentUserId)) ownedCount++;
        int followers = FollowerCount(user->id);
        DrawTextFit(TextFormat("%d repositor%s   |   %d follower%s   |   joined %s   |   theme: %s", ownedCount, ownedCount == 1 ? "y" : "ies",
                           followers, followers == 1 ? "" : "s", TimeAgo(user->created), THEMES[UserTheme(user->id)].name),
                textX, (int)card.y + 64, FONT_SIZE, textWidth, UI_MUTED);
        if (IsHovered(listArea) && WasClicked(card)) { int userId = user->id; EndScissorMode(); OpenProfile(userId); return; }
    }
    EndScissorMode();
    DrawScrollbar(listArea, peopleScroll, userCount * rowHeight - 10);
}
