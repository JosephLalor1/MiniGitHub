/*
 * screen_profile.c - a person's profile page
 *
 * Your own profile is also your home page. There, the column about you is
 * tucked away behind your picture to leave room for everything else:
 * hovering the picture slides it out, and clicking the picture keeps it open.
 */

#include "screen_profile.h"
#include "animation.h"
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
   see after signing in, with your repositories, the ones of people you
   follow, everyone's, a search box and the New repository button. Other
   people's profiles list their repositories, with a Follow button. */

#define DETAILS_WIDTH 290            /* the column about the person: picture, name, bio, numbers */
#define DETAILS_SLIDE 340            /* how far it slides to tuck away                           */

static bool      sidebarPinned;      /* your home page's column is kept open (click your picture) */
static Rectangle sidebarPanel;       /* where it was drawn last frame while sliding out over the page */
static float     sidebarSlide;       /* how far out it was last frame (0..1)                          */

/* The column about the person, drawn with its left edge at left: name, when they
   joined, followers, bio, Edit profile / Follow, numbers, and a link to everyone.
   Sets *bottom to where it ends. Returns true if it opened another page. */
static bool DrawProfileDetails(User *user, bool isOwnProfile, float left, float *bottom)
{
    int theme = UserTheme(user->id);
    int x = (int)left;
    DrawTextFit(user->name, x, 264, 30, DETAILS_WIDTH, UI_TEXT);
    DrawText(TextFormat("Joined %s", TimeAgo(user->created)), x, 298, FONT_SIZE, UI_MUTED);
    int followers = FollowerCount(user->id);
    const char *followText = TextFormat("%d follower%s   |   %d following%s", followers, followers == 1 ? "" : "s", user->followingCount,
                                        !isOwnProfile && IsFollowing(user->id, currentUserId) ? "   |   follows you" : "");
    DrawTextFit(followText, x, 324, FONT_SIZE, DETAILS_WIDTH, UI_MUTED);
    int y = 356;
    if (user->bio[0]) y += DrawWrappedText(user->bio, x, y, DETAILS_WIDTH, screenHeight >= 760 ? 4 : 2, UI_TEXT) * 24 + 8;  /* shorter on small windows */
    if (isOwnProfile) {
        if (Button((Rectangle){left, (float)y, 280, 36}, "Edit profile", UI_BUTTON)) { OpenSettings(); return true; }
        y += 48;
    } else {
        bool following = IsFollowing(currentUserId, user->id);
        if (Button((Rectangle){left, (float)y, 280, 36}, following ? "Unfollow" : "Follow", following ? UI_BUTTON : UI_GREEN)) {
            ToggleFollow(currentUserId, user->id);
            MarkDirty();
            Toast(following ? TextFormat("You've stopped following %s", user->name)
                            : TextFormat("Following %s - their repositories show under Following on your home page", user->name));
        }
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
    DrawLine(x, y, x + 280, y, UI_BORDER);
    y += 14;
    DrawText(TextFormat("%d repositor%s",     repoTotal,   repoTotal == 1 ? "y" : "ies"), x, y, FONT_SIZE, UI_TEXT); y += 28;
    DrawText(TextFormat("%d commit%s",        commitTotal, commitTotal == 1 ? "" : "s"),  x, y, FONT_SIZE, UI_TEXT); y += 28;
    DrawText(TextFormat("%d star%s received", starTotal,   starTotal == 1 ? "" : "s"),    x, y, FONT_SIZE, UI_TEXT); y += 28;
    if (isOwnProfile) {
        DrawText(TextFormat("%d repositor%s starred", starredTotal, starredTotal == 1 ? "y" : "ies"), x, y, FONT_SIZE, UI_TEXT); y += 28;
        DrawTextFit(TextFormat("%d open issue%s in your repos", openIssueTotal, openIssueTotal == 1 ? "" : "s"), x, y, FONT_SIZE, 300, UI_TEXT); y += 28;
    } else {
        DrawText(TextFormat("%d issue%s opened", issueTotal, issueTotal == 1 ? "" : "s"), x, y, FONT_SIZE, UI_TEXT); y += 28;
    }
    y += 12;
    const char *peopleText = TextFormat("%d user%s on this computer", userCount, userCount == 1 ? "" : "s");
    Rectangle peopleHit = {left, (float)y - 4, (float)MeasureText(peopleText, FONT_SIZE), 28};
    bool peopleHovered = IsHovered(peopleHit);
    DrawText(peopleText, x, y, FONT_SIZE, peopleHovered ? UI_LINK : UI_MUTED);
    if (peopleHovered) { wantedCursor = MOUSE_CURSOR_POINTING_HAND; DrawLine(x, y + 22, x + (int)peopleHit.width, y + 22, UI_LINK); }
    if (WasClicked(peopleHit)) { OpenPeople(); return true; }
    *bottom = (float)y + 24;
    if (y + 54 <= screenHeight) { DrawText(TextFormat("Theme: %s", THEMES[theme].name), x, y + 30, FONT_SIZE, THEMES[theme].accent); *bottom = (float)y + 54; }  /* only if there's room */
    return false;
}

/* Draws the profile page of profileUserId. */
void ScreenProfile(void)
{
    User *user = FindUserById(profileUserId);
    if (!user) { GoHome(); return; }
    bool isOwnProfile = user->id == currentUserId;
    int theme = UserTheme(user->id);
    Color accent = THEMES[theme].accent;
    Vector2 mouse = GetMousePosition();

    /* ---- how open the column about the person is ----
       open: 0 = tucked away behind the picture, 1 = taking its place beside the page.
       slide: how far the column has slid out (it slides out over the page on hover). */
    float open = 1, slide = 1;
    Vector2 bubbleCenter = {96, 177};
    float bubbleRadius = 80;
    if (isOwnProfile) {
        open = Animate(AnimationKey("sidebar open", 0, 0), sidebarPinned ? 1.0f : 0.0f, 10);
        float shown = EaseInOut(open);
        bubbleCenter.x = 70 + 26 * shown;
        bubbleRadius = 46 + 34 * shown;
        float dx = mouse.x - bubbleCenter.x, dy = mouse.y - bubbleCenter.y;
        bool overBubble = dx * dx + dy * dy <= bubbleRadius * bubbleRadius;
        bool overPanel = sidebarSlide > 0.3f && CheckCollisionPointRec(mouse, sidebarPanel);   /* moved onto it once it's out */
        if (overBubble && !clickHandled && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {  /* click the picture: keep it open, or tuck it away */
            clickHandled = true;
            sidebarPinned = !sidebarPinned;
        }
        if (overBubble) {
            wantedCursor = MOUSE_CURSOR_POINTING_HAND;
            SetTooltip(sidebarPinned ? "Click to tuck your profile away" : "Click to keep your profile open", NULL, NULL);
        }
        slide = sidebarSlide = Animate(AnimationKey("sidebar slide", 0, 0), sidebarPinned || overBubble || overPanel ? 1.0f : 0.0f, 12);
        bubbleRadius += 8 * slide * (1 - shown);             /* the picture grows a little as the column comes out */
        /* while it lies over the page, nothing underneath it reacts to the mouse */
        if (open < 0.5f && slide > 0.05f) { hoverBlockedArea = sidebarPanel; hoverBlocked = true; }
    }

    Rectangle banner = {0, 57, (float)screenWidth, 120};
    DrawRectangleRec(banner, UI_BASE);
    DrawTheme(theme, banner, 1.0f);
    DrawLine(0, 177, screenWidth, 177, UI_BORDER);

    /* ---- left column: picture, name, bio, numbers (on others' profiles, and your pinned one) ---- */
    float detailsLeft = 20 - (1 - EaseInOut(slide)) * DETAILS_SLIDE, detailsBottom = 0;
    bool overlay = isOwnProfile && open < 0.999f;        /* it slides out over the page, on a panel of its own */
    if (!overlay && DrawProfileDetails(user, isOwnProfile, detailsLeft, &detailsBottom)) return;

    /* ---- right column: invitations waiting for you ---- */
    float rightX = 150 + (340 - 150) * EaseInOut(open), rightWidth = (float)screenWidth - rightX - 20, offsetY = 0;
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

    /* ---- right column: repositories ----
       On your home page: New repository on the left (away from the search box, so
       it isn't mistaken for a search button), then which repositories, then search. */
    float listTop = 367 + offsetY;
    if (isOwnProfile) {
        bool roomy = rightWidth >= 760;
        float newWidth = roomy ? 196 : 96;
        if (Button((Rectangle){rightX, listTop, newWidth, 40}, roomy ? "+ New repository" : "+ New", UI_GREEN)) {
            Unfocus();
            TextFieldClear(&repoNameField);
            TextFieldClear(&repoDescField);
            newRepoWithReadme = true;
            screen = SCREEN_NEW_REPO;
            repoNameField.active = true;
            return;
        }
        float gap = roomy ? 14 : 8, viewX = rightX + newWidth + gap;
        DrawLine((int)viewX, (int)listTop + 6, (int)viewX, (int)listTop + 34, UI_BORDER);   /* a divider between making and finding */
        viewX += gap;
        static const char *viewLabels[3] = {"Yours", "Everyone", "Following"};
        static const int viewOrder[3] = {0, 2, 1};           /* Yours, Following, Everyone */
        for (int k = 0; k < 3; k++) {
            int view = viewOrder[k];
            float width = (float)MeasureText(viewLabels[view], FONT_SIZE) + (roomy ? 30 : 20);
            if (ButtonEnabled((Rectangle){viewX, listTop, width, 40}, viewLabels[view], homeRepoView == view ? UI_BLUE : UI_BUTTON, true)) { homeRepoView = view; profileScroll = 0; }
            viewX += width + 6;
        }
        TextFieldWidget(&searchField, (Rectangle){viewX + 8, listTop, rightX + rightWidth - viewX - 8, 40}, "Find a repository...");
    } else {
        const char *title = TextFormat("%s's repositories", user->name);
        float searchWidth = rightWidth * 0.4f;
        DrawTextFit(title, (int)rightX, (int)listTop + 10, FONT_SIZE, (int)(rightWidth - searchWidth - 20), UI_TEXT);
        TextFieldWidget(&searchField, (Rectangle){rightX + rightWidth - searchWidth, listTop, searchWidth, 40}, "Find a repository...");
    }
    int view = isOwnProfile ? homeRepoView : 0;
    bool showEveryone = view == 1, showFollowing = view == 2;

    /* which repos to list: filter, then newest activity first */
    int order[MAX_REPOS], count = 0;
    for (int i = 0; i < repoCount; i++) {
        Repo *repo = &repos[i];
        if (!CanSee(repo, currentUserId)) continue;                                     /* private, and you're not in it */
        if (showFollowing) { if (!IsFollowing(user->id, repo->owner)) continue; }       /* people you follow: what they own */
        else if (!showEveryone && repo->owner != user->id && !(isOwnProfile && IsCollaborator(repo, user->id))) continue;  /* yours: owned + collaborating */
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
                      : showFollowing && !user->followingCount ? "You're not following anyone yet - open someone's profile and press Follow."
                      : showFollowing    ? "Nobody you follow has a repository you can see yet."
                      : showEveryone     ? "Nobody has made a repository yet."
                      : isOwnProfile     ? "You have no repositories yet - press New repository to make one."
                                         : TextFormat("%s hasn't made any repositories yet.", user->name);
        DrawTextFit(emptyMessage, (int)listArea.x, (int)listArea.y + 6, FONT_SIZE, (int)listArea.width, UI_MUTED);
    }

    BeginScissorMode((int)listArea.x, (int)listArea.y, (int)listArea.width, (int)listArea.height);
    for (int k = 0; k < count; k++) {
        Repo *repo = &repos[order[k]];
        Rectangle card = {listArea.x, listArea.y + k * rowHeight - profileScroll, listArea.width - 12, rowHeight - 10};
        if (card.y > listArea.y + listArea.height || card.y + card.height < listArea.y) continue;
        bool hovered = IsHovered(listArea) && IsHovered(card);
        if (hovered) wantedCursor = MOUSE_CURSOR_POINTING_HAND;
        DrawRectangleRec(card, CardColor(AnimationKey("repo card", (float)order[k], 0), hovered));
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

    /* ---- your column, sliding out over the page (when it isn't pinned open) ---- */
    if (overlay) {
        hoverBlocked = false;                                /* its own buttons work */
        if (slide > 0.01f) {
            float panelAlpha = 1 - EaseInOut(open);          /* the panel melts away as the column settles in beside the page */
            Rectangle panel = {detailsLeft - 16, 186, DETAILS_WIDTH + 30, (sidebarPanel.height > 0 ? sidebarPanel.height : 400)};
            DrawRectangleRounded((Rectangle){panel.x + 4, panel.y + 6, panel.width, panel.height}, 0.04f, 6, WithAlpha(BLACK, 0.35f * panelAlpha));
            DrawRectangleRounded(panel, 0.04f, 6, WithAlpha((Color){22, 27, 34, 255}, 0.97f * panelAlpha));
            DrawRectangleRoundedLinesEx(panel, 0.04f, 6, 1, WithAlpha(UI_BORDER, panelAlpha));
            if (DrawProfileDetails(user, isOwnProfile, detailsLeft, &detailsBottom)) return;
            sidebarPanel = (Rectangle){panel.x, panel.y, panel.width, detailsBottom - panel.y + 14};
        } else sidebarPanel = (Rectangle){0, 186, DETAILS_WIDTH + 30, sidebarPanel.height};
    }

    /* ---- the picture, on top of everything (on your home page it opens the column) ---- */
    DrawCircleV(bubbleCenter, bubbleRadius, UI_BASE);
    DrawCircleV(bubbleCenter, bubbleRadius - 3, accent);
    DrawAvatar(user->id, (int)bubbleCenter.x, (int)bubbleCenter.y, (int)(bubbleRadius - 8));
    if (isOwnProfile && sidebarPinned) DrawRing(bubbleCenter, bubbleRadius - 1, bubbleRadius + 2, 0, 360, 64, WithAlpha(accent, 0.7f));

    if (hoverDay) DrawDayTooltip(user->id, hoverDay);       /* last, so it sits on top of everything */
}
