/*
 * screen_profile.c - a person's profile page
 *
 * Your own profile is also your home page.
 */

#include "screen_profile.h"
#include "app_state.h"
#include "avatars.h"
#include "contributions.h"
#include "forks.h"
#include "languages.h"
#include "model.h"
#include "navigation.h"
#include "repo.h"
#include "tech_stack.h"
#include "themes.h"
#include "ui_core.h"
#include "users.h"
#include "util.h"
#include "widgets.h"

/* ---------------- profile pages ----------------
   Everyone has one. Your own is also your home page: the first thing you
   see after signing in, with your repositories, everyone else's, a search
   box and the New button. Other people's profiles list their repositories. */

/* Draws the profile page of profileUserId. */
void ScreenProfile(void)
{
    User *user = FindUserById(profileUserId);
    if (!user) { GoHome(); return; }
    bool isOwnProfile = user->id == currentUserId;
    int theme = UserTheme(user->id);
    Color accent = THEMES[theme].accent;

    Rectangle banner = {0, 57, (float)screenWidth, 120};
    DrawRectangleRec(banner, UI_BASE);
    DrawTheme(theme, banner, 1.0f);
    DrawLine(0, 177, screenWidth, 177, UI_BORDER);

    /* ---- left column: picture, name, bio, numbers ---- */
    int avatarX = 96, avatarY = 177;
    DrawCircle(avatarX, avatarY, 80, UI_BASE);
    DrawCircle(avatarX, avatarY, 77, accent);
    DrawAvatar(user->id, avatarX, avatarY, 72);
    DrawTextFit(user->name, 20, 264, 30, 290, UI_TEXT);
    DrawText(TextFormat("Joined %s", TimeAgo(user->created)), 20, 298, FONT_SIZE, UI_MUTED);
    int y = 330;
    if (user->bio[0]) y += DrawWrappedText(user->bio, 20, y, 290, screenHeight >= 760 ? 4 : 3, UI_TEXT) * 24 + 8;  /* shorter on small windows */
    if (isOwnProfile) {
        if (Button((Rectangle){20, (float)y, 280, 36}, "Edit profile", UI_BUTTON)) { OpenSettings(); return; }
        y += 48;
    }

    int repoTotal = 0, commitTotal = 0, starTotal = 0, issueTotal = 0, starredTotal = 0, openIssueTotal = 0;
    for (int i = 0; i < repoCount; i++) {
        Repo *repo = &repos[i];
        if (!CanSee(repo, currentUserId)) continue;       /* private, and you're not in it */
        if (repo->owner == user->id) { repoTotal++; starTotal += repo->starCount; openIssueTotal += OpenIssueCount(repo); }
        if (HasStarred(repo, user->id)) starredTotal++;
        for (int k = 0; k < repo->commitCount; k++) if (repo->commits[k].author == user->id && !repo->commits[k].copied) commitTotal++;
        for (int k = 0; k < repo->issueCount;  k++) if (repo->issues[k].author  == user->id) issueTotal++;
    }
    DrawLine(20, y, 300, y, UI_BORDER);
    y += 14;
    DrawText(TextFormat("%d repositor%s",     repoTotal,   repoTotal == 1 ? "y" : "ies"), 20, y, FONT_SIZE, UI_TEXT); y += 28;
    DrawText(TextFormat("%d commit%s",        commitTotal, commitTotal == 1 ? "" : "s"),  20, y, FONT_SIZE, UI_TEXT); y += 28;
    DrawText(TextFormat("%d star%s received", starTotal,   starTotal == 1 ? "" : "s"),    20, y, FONT_SIZE, UI_TEXT); y += 28;
    if (isOwnProfile) {
        DrawText(TextFormat("%d repositor%s starred", starredTotal, starredTotal == 1 ? "y" : "ies"), 20, y, FONT_SIZE, UI_TEXT); y += 28;
        DrawTextFit(TextFormat("%d open issue%s in your repos", openIssueTotal, openIssueTotal == 1 ? "" : "s"), 20, y, FONT_SIZE, 300, UI_TEXT); y += 28;
    } else {
        DrawText(TextFormat("%d issue%s opened", issueTotal, issueTotal == 1 ? "" : "s"), 20, y, FONT_SIZE, UI_TEXT); y += 28;
    }
    y += 12;
    const char *peopleText = TextFormat("%d user%s on this computer", userCount, userCount == 1 ? "" : "s");
    Rectangle peopleHit = {20, (float)y - 4, (float)MeasureText(peopleText, FONT_SIZE), 28};
    bool peopleHovered = IsHovered(peopleHit);
    DrawText(peopleText, 20, y, FONT_SIZE, peopleHovered ? UI_LINK : UI_MUTED);
    if (peopleHovered) { wantedCursor = MOUSE_CURSOR_POINTING_HAND; DrawLine(20, y + 22, 20 + (int)peopleHit.width, y + 22, UI_LINK); }
    if (WasClicked(peopleHit)) { OpenPeople(); return; }
    if (y + 54 <= screenHeight) DrawText(TextFormat("Theme: %s", THEMES[theme].name), 20, y + 30, FONT_SIZE, accent);  /* only if there's room */

    /* ---- right column: invitations waiting for you ---- */
    float rightX = 340, rightWidth = (float)screenWidth - 360, offsetY = 0;
    if (isOwnProfile) {
        int shownInvites = 0, totalInvites = InvitationCount(currentUserId);
        for (int i = 0; i < repoCount && shownInvites < 2; i++) {
            Repo *invitingRepo = &repos[i];
            if (!IsInvited(invitingRepo, currentUserId)) continue;
            Rectangle inviteBox = {rightX, 191 + offsetY, rightWidth, 52};
            DrawRectangleRec(inviteBox, (Color){20, 50, 90, 255});
            DrawRectangleLinesEx(inviteBox, 1, UI_LINK);
            DrawAvatar(invitingRepo->owner, (int)inviteBox.x + 28, (int)inviteBox.y + 26, 16);
            const char *message = TextFormat("%s invited you to collaborate on %s", UserName(invitingRepo->owner), invitingRepo->name);
            DrawTextFit(message, (int)inviteBox.x + 54, (int)inviteBox.y + 15, FONT_SIZE, (int)inviteBox.width - 54 - 370, UI_TEXT);
            if (Button((Rectangle){inviteBox.x + inviteBox.width - 360, inviteBox.y + 9, 100, 34}, "View", UI_BUTTON)) { OpenRepo(i); repoTab = TAB_PEOPLE; return; }
            if (Button((Rectangle){inviteBox.x + inviteBox.width - 252, inviteBox.y + 9, 120, 34}, "Accept", UI_GREEN)) {
                AcceptInvite(invitingRepo, currentUserId); MarkDirty();
                Toast(TextFormat("You can now edit %s - it's in your list below", invitingRepo->name));
                return;
            }
            if (Button((Rectangle){inviteBox.x + inviteBox.width - 124, inviteBox.y + 9, 116, 34}, "Decline", UI_BUTTON)) {
                RemoveInvite(invitingRepo, currentUserId); MarkDirty();
                Toast("Invitation declined");
                return;
            }
            offsetY += 60;
            shownInvites++;
        }
        if (totalInvites > shownInvites) {
            DrawText(TextFormat("...and %d more invitation%s", totalInvites - shownInvites, totalInvites - shownInvites == 1 ? "" : "s"),
                     (int)rightX, (int)(191 + offsetY), FONT_SIZE, UI_MUTED);
            offsetY += 30;
        }
    }

    /* ---- right column: contributions ---- */
    long long hoverDay = 0;
    /* wide enough: the calendar and the tech stack side by side; otherwise the stack is a bar below */
    bool sideBySide = rightWidth >= 800;
    float calendarWidth = sideBySide ? 440 : rightWidth, calendarHeight = sideBySide ? 184 : 160;
    DrawContributions((Rectangle){rightX, 191 + offsetY, calendarWidth, calendarHeight}, user->id, &hoverDay, sideBySide);
    if (screen != SCREEN_PROFILE) return;                    /* a day was clicked */
    if (sideBySide) DrawTechStack((Rectangle){rightX + calendarWidth + 12, 191 + offsetY, rightWidth - calendarWidth - 12, calendarHeight}, user->id, false);
    else { DrawTechStack((Rectangle){rightX, 191 + offsetY + calendarHeight + 10, rightWidth, 96}, user->id, true); offsetY += 106; }
    if (sideBySide) offsetY += calendarHeight - 160;

    /* ---- right column: repositories ---- */
    float listTop = 367 + offsetY;
    if (isOwnProfile) {
        if (ButtonEnabled((Rectangle){rightX, listTop, 100, 40}, "Yours", homeRepoView == 0 ? UI_BLUE : UI_BUTTON, true))       { homeRepoView = 0; profileScroll = 0; }
        if (ButtonEnabled((Rectangle){rightX + 106, listTop, 120, 40}, "Everyone", homeRepoView == 1 ? UI_BLUE : UI_BUTTON, true)) { homeRepoView = 1; profileScroll = 0; }
        TextFieldWidget(&searchField, (Rectangle){rightX + 240, listTop, rightWidth - 240 - 130, 40}, "Find a repository...");
        if (Button((Rectangle){rightX + rightWidth - 120, listTop, 120, 40}, "New", UI_GREEN)) {
            Unfocus();
            TextFieldClear(&repoNameField);
            TextFieldClear(&repoDescField);
            newRepoWithReadme = true;
            screen = SCREEN_NEW_REPO;
            repoNameField.active = true;
            return;
        }
    } else {
        const char *title = TextFormat("%s's repositories", user->name);
        float searchWidth = rightWidth * 0.4f;
        DrawTextFit(title, (int)rightX, (int)listTop + 10, FONT_SIZE, (int)(rightWidth - searchWidth - 20), UI_TEXT);
        TextFieldWidget(&searchField, (Rectangle){rightX + rightWidth - searchWidth, listTop, searchWidth, 40}, "Find a repository...");
    }
    bool showEveryone = isOwnProfile && homeRepoView == 1;

    /* which repos to list: filter, then newest activity first */
    int order[MAX_REPOS], count = 0;
    for (int i = 0; i < repoCount; i++) {
        Repo *repo = &repos[i];
        if (!CanSee(repo, currentUserId)) continue;                                     /* private, and you're not in it */
        if (!showEveryone && repo->owner != user->id && !(isOwnProfile && IsCollaborator(repo, user->id))) continue;  /* yours: owned + collaborating */
        if (!ContainsIgnoreCase(repo->name, searchField.text) && !ContainsIgnoreCase(UserName(repo->owner), searchField.text)) continue;
        order[count++] = i;
    }
    for (int pos = 0; pos < count; pos++)
        for (int other = pos + 1; other < count; other++)
            if (LastActivity(&repos[order[other]]) > LastActivity(&repos[order[pos]])) { int swap = order[pos]; order[pos] = order[other]; order[other] = swap; }

    Rectangle listArea = {rightX, listTop + 54, rightWidth, (float)screenHeight - 20 - (listTop + 54)};
    float rowHeight = 100;
    ScrollArea(listArea, &profileScroll, count * rowHeight - 10);
    if (count == 0) {
        const char *emptyMessage = searchField.length ? "No repositories match your search."
                      : showEveryone     ? "Nobody has made a repository yet."
                      : isOwnProfile         ? "You have no repositories yet - press New to make one."
                                     : TextFormat("%s hasn't made any repositories yet.", user->name);
        DrawText(emptyMessage, (int)listArea.x, (int)listArea.y + 6, FONT_SIZE, UI_MUTED);
    }

    BeginScissorMode((int)listArea.x, (int)listArea.y, (int)listArea.width, (int)listArea.height);
    for (int k = 0; k < count; k++) {
        Repo *repo = &repos[order[k]];
        Rectangle card = {listArea.x, listArea.y + k * rowHeight - profileScroll, listArea.width - 12, rowHeight - 10};
        if (card.y > listArea.y + listArea.height || card.y + card.height < listArea.y) continue;
        bool hovered = IsHovered(listArea) && IsHovered(card);
        if (hovered) wantedCursor = MOUSE_CURSOR_POINTING_HAND;
        DrawRectangleRec(card, hovered ? UI_HOVER : UI_PANEL);
        DrawRectangleLinesEx(card, 1, UI_BORDER);

        int textX = (int)card.x + 18;
        if (showEveryone || repo->owner != user->id) {       /* show "owner / name" when it isn't theirs */
            const char *ownerPrefix = TextFormat("%s / ", UserName(repo->owner));
            DrawText(ownerPrefix, textX, (int)card.y + 12, FONT_SIZE, UI_LINK);
            textX += MeasureText(ownerPrefix, FONT_SIZE);
        }
        DrawTextBold(repo->name, textX, (int)card.y + 12, FONT_SIZE, UI_LINK);
        textX += MeasureText(repo->name, FONT_SIZE) + 16;
        if (IsCollaborator(repo, currentUserId)) { DrawText("collaborator", textX, (int)card.y + 12, FONT_SIZE, UI_MUTED); textX += MeasureText("collaborator", FONT_SIZE) + 16; }
        if (repo->isFork) { DrawText("fork", textX, (int)card.y + 12, FONT_SIZE, UI_MUTED); textX += MeasureText("fork", FONT_SIZE) + 16; }
        if (repo->isPrivate) {                                                             /* a "Private" label */
            Rectangle privatePill = {(float)textX, card.y + 11, (float)MeasureText("Private", FONT_SIZE) + 16, 24};
            DrawRectangleRounded(privatePill, 0.5f, 6, (Color){110, 80, 20, 120});
            DrawText("Private", textX + 8, (int)card.y + 13, FONT_SIZE, UI_YELLOW);
            textX += (int)privatePill.width + 12;
        }
        if (HasStarred(repo, currentUserId)) DrawText("starred", textX, (int)card.y + 12, FONT_SIZE, UI_YELLOW);

        DrawTextFit(repo->description[0] ? repo->description : "No description", (int)card.x + 18, (int)card.y + 38, FONT_SIZE, (int)card.width - 36, UI_MUTED);
        int statsX = (int)card.x + 18;
        BuildLanguages(repo, -1);                    /* main language of the latest commit */
        if (langPartCount && langParts[0].lang >= 0) {
            DrawCircle(statsX + 5, (int)card.y + 72, 5, langParts[0].color);
            DrawText(LANG_NAMES[langParts[0].lang], statsX + 16, (int)card.y + 62, FONT_SIZE, UI_TEXT);
            statsX += 16 + MeasureText(LANG_NAMES[langParts[0].lang], FONT_SIZE) + 24;
        }
        int forkCount = ForkCount(repo);
        DrawTextFit(TextFormat("%d commit%s   |   %d open issue%s   |   %d star%s   |   %d fork%s   |   updated %s",
                           repo->commitCount, repo->commitCount == 1 ? "" : "s",
                           OpenIssueCount(repo), OpenIssueCount(repo) == 1 ? "" : "s",
                           repo->starCount, repo->starCount == 1 ? "" : "s",
                           forkCount, forkCount == 1 ? "" : "s",
                           TimeAgo(LastActivity(repo))),
                statsX, (int)card.y + 62, FONT_SIZE, (int)(card.x + card.width) - 18 - statsX, UI_MUTED);
        if (IsHovered(listArea) && WasClicked(card)) { EndScissorMode(); OpenRepo(order[k]); return; }
    }
    EndScissorMode();
    DrawScrollbar(listArea, profileScroll, count * rowHeight - 10);

    if (hoverDay) DrawDayTooltip(user->id, hoverDay);       /* last, so it sits on top of everything */
}
