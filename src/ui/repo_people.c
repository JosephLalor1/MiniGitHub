/*
 * repo_people.c - the People tab
 *
 * Who has access, invitations, and the pie chart of who did how much.
 */

#include "repo_people.h"
#include "app_state.h"
#include "avatars.h"
#include "contributions.h"
#include "editor.h"
#include "model.h"
#include "navigation.h"
#include "ui_core.h"
#include "users.h"
#include "util.h"
#include "widgets.h"

/* ------------------------------------------------------------------ */
/* Contributions per person (the pie chart) and the People tab         */
/* ------------------------------------------------------------------ */

/* DisplaySlice colours, in a fixed order. They were checked with a colour-blindness
   simulator to stay tellable apart (including the last and first, which touch
   where the circle closes) and to stand out against the dark background.
   A person's colour follows their place in the list (owner first, then
   collaborators in the order they joined), never how much they've done,
   so colours don't jump around as commits come in. Past 6 people, the rest
   share one grey "Others" slice. */
static const Color PIE_COLORS[6] = {
    {0x39, 0x87, 0xe5, 255}, {0xd9, 0x59, 0x26, 255}, {0x19, 0x9e, 0x70, 255},
    {0xc9, 0x85, 0x00, 255}, {0xd5, 0x51, 0x81, 255}, {0x00, 0x83, 0x00, 255}
};
static const Color PIE_OTHER = {0x6e, 0x76, 0x81, 255};

/* lines = lines added + lines removed: every changed line counts as work, so removing
   14 lines counts the same as adding 14. A slice's size comes from lines. */
typedef struct { int userId; int commits; int adds, dels, lines; Color color; int peopleCount; } Share;   /* user -2 = "Others" */
static Share shares[6];
static int   shareCount, shareTotal;    /* shareTotal = all lines changed, by everyone */

/* Owner, collaborators, then everyone else who has committed - former
   collaborators, deleted accounts ("ghost") and people who aren't Mini GitHub
   users - biggest share first. More than 6: the smallest go in "Others". */
static void BuildShares(Repo *repo)
{
    int accountSlots = nextUserId + 1, slotCount = accountSlots + 1 + outsiderCount;  /* accounts, ghost, outsiders */
    int *totals = calloc((size_t)slotCount * 3, sizeof(int)), *order = malloc((size_t)slotCount * sizeof(int));
    bool *picked = calloc((size_t)slotCount, 1);
    shareCount = shareTotal = 0;
    if (!totals || !order || !picked) { free(totals); free(order); free(picked); return; }
    #define SLOT_OF(id) ((id) >= 0 ? ((id) < accountSlots ? (id) : accountSlots) : IsOutsider(id) ? accountSlots + 1 + (-(id) - OUTSIDER_BASE) : accountSlots)
    #define ID_OF_SLOT(slot)  ((slot) < accountSlots ? (slot) : (slot) == accountSlots ? -1 : -(OUTSIDER_BASE + (slot) - accountSlots - 1))
    for (int k = 0; k < repo->commitCount; k++) {
        int slot = SLOT_OF(repo->commits[k].author);
        CommitStats(repo, k);
        totals[slot * 3]++; totals[slot * 3 + 1] += repo->commits[k].statAdds; totals[slot * 3 + 2] += repo->commits[k].statDels;
    }
    int count = 0;
    order[count++] = SLOT_OF(repo->owner); picked[order[0]] = true;  /* the people with access first */
    for (int i = 0; i < repo->collaboratorCount; i++) { int slot = SLOT_OF(repo->collaborators[i]); if (!picked[slot]) { picked[slot] = true; order[count++] = slot; } }
    int fixedCount = count;
    for (int slot = 0; slot < slotCount; slot++) if (!picked[slot] && totals[slot * 3]) order[count++] = slot;
    for (int i = fixedCount; i < count; i++)                         /* everyone else: most lines first */
        for (int j = i + 1; j < count; j++)
            if (totals[order[j] * 3 + 1] + totals[order[j] * 3 + 2] > totals[order[i] * 3 + 1] + totals[order[i] * 3 + 2]) { int swap = order[i]; order[i] = order[j]; order[j] = swap; }
    for (int i = 0; i < count; i++) {
        int slot = order[i], commitCount = totals[slot * 3], added = totals[slot * 3 + 1], removed = totals[slot * 3 + 2];
        shareTotal += added + removed;
        if (count <= 6 || i < 5) shares[shareCount++] = (Share){ID_OF_SLOT(slot), commitCount, added, removed, added + removed, PIE_COLORS[i], 1};
        else {
            if (shareCount == 5) shares[shareCount++] = (Share){-2, 0, 0, 0, 0, PIE_OTHER, 0};
            shares[5].commits += commitCount; shares[5].adds += added; shares[5].dels += removed; shares[5].lines += added + removed; shares[5].peopleCount++;
        }
    }
    #undef SLOT_OF
    #undef ID_OF_SLOT
    free(totals); free(order); free(picked);
}

/* A repository counts as "multi-user" once it has a collaborator, or commits by more than one person. */
static bool IsMultiUser(Repo *repo)
{
    if (repo->collaboratorCount) return true;
    for (int k = 1; k < repo->commitCount; k++) if (repo->commits[k].author != repo->commits[0].author) return true;
    return false;
}

static const char *ShareName(Share *share)
{
    return share->userId != -2 ? AuthorLabel(share->userId) : TextFormat("Others (%d people)", share->peopleCount);
}

static int Percent(int part, int total) { return total ? (part * 100 + total / 2) / total : 0; }

/* Draws the pie for the shares worked out by BuildShares. Slices start at
   the top and go clockwise; a thin gap separates them. Returns the slice
   under the mouse (it's drawn slightly bigger), or -1. */
static int DrawPie(Vector2 center, float radius)
{
    if (shareTotal == 0) {                      /* no lines changed yet: an empty ring */
        DrawRing(center, radius - 3, radius, 0, 360, 48, UI_BORDER);
        return -1;
    }
    Vector2 mouse = GetMousePosition();
    float dx = mouse.x - center.x, dy = mouse.y - center.y;
    bool inside = dx * dx + dy * dy <= (radius + 4) * (radius + 4);
    float mouseAngle = atan2f(dy, dx) * RAD2DEG;  /* same direction as raylib: clockwise from the right */
    if (mouseAngle < -90) mouseAngle += 360;      /* so it runs -90 (top) ... 270 */

    int hovered = -1, sliceCount = 0;
    float angle = -90;
    for (int i = 0; i < shareCount; i++) {
        if (!shares[i].lines) continue;
        sliceCount++;
        float sweep = 360.0f * shares[i].lines / shareTotal;
        if (inside && mouseAngle >= angle && mouseAngle < angle + sweep) hovered = i;
        angle += sweep;
    }
    angle = -90;
    for (int i = 0; i < shareCount; i++) {
        if (!shares[i].lines) continue;
        float sweep = 360.0f * shares[i].lines / shareTotal;
        float sliceRadius = i == hovered ? radius + 4 : radius;
        DrawCircleSector(center, sliceRadius, angle, angle + sweep, (int)(sweep / 3) + 4, shares[i].color);
        angle += sweep;
    }
    if (sliceCount > 1) {                        /* the gaps between slices */
        Color gapColor = {22, 27, 34, 255};
        angle = -90;
        for (int i = 0; i < shareCount; i++) {
            if (!shares[i].lines) continue;
            Vector2 edge = {center.x + cosf(angle * DEG2RAD) * (radius + 6), center.y + sinf(angle * DEG2RAD) * (radius + 6)};
            DrawLineEx(center, edge, 2, gapColor);
            angle += 360.0f * shares[i].lines / shareTotal;
        }
    }
    return hovered;
}

static void ShareTip(int index)
{
    Share *share = &shares[index];
    SetTooltip(ShareName(share),
           TextFormat("%d line%s changed  (%d%%)", share->lines, share->lines == 1 ? "" : "s", Percent(share->lines, shareTotal)),
           TextFormat("+%d added, -%d removed, in %d commit%s", share->adds, share->dels, share->commits, share->commits == 1 ? "" : "s"));
}

/* The little pie in a repository's header. Returns true if it was clicked. */
bool HeaderPie(Repo *repo, float rightX, float centerY)
{
    if (!IsMultiUser(repo)) return false;
    BuildShares(repo);
    int memberCount = 1 + repo->collaboratorCount, workerCount = 0;  /* people with access, or everyone who worked on it (forks) */
    for (int i = 0; i < shareCount; i++) if (shares[i].commits) workerCount += shares[i].userId != -2 ? 1 : shares[i].peopleCount;
    if (workerCount > memberCount) memberCount = workerCount;
    const char *label = TextFormat("%d %s", memberCount, memberCount == 1 ? "person" : "people");
    Vector2 center = {rightX - 18, centerY};
    int labelWidth = MeasureText(label, FONT_SIZE);
    Rectangle hit = {rightX - 36 - 12 - labelWidth, centerY - 20, (float)labelWidth + 48, 40};
    bool hovered = IsHovered(hit);
    DrawText(label, (int)(rightX - 36 - 12 - labelWidth), (int)centerY - 10, FONT_SIZE, hovered ? UI_LINK : UI_MUTED);
    int hoveredSlice = DrawPie(center, 16);
    if (hovered) {
        wantedCursor = MOUSE_CURSOR_POINTING_HAND;
        if (hoveredSlice >= 0) ShareTip(hoveredSlice);
        else SetTooltip("Contributions", "Click to see who did what", NULL);
    }
    return WasClicked(hit);
}

/* "That's me": an outsider's email (or name, if their commits had none) becomes
   one of yours, so their commits count as yours - everywhere, from now on. */
static bool ClaimIdentity(int userId, int outsiderId)
{
    User *user = FindUserById(userId);
    Outsider *outsiderInfo = OutsiderOf(outsiderId);
    if (!user || !outsiderInfo) return false;
    if (user->claimCount >= 8) { Toast("You can add up to 8 other emails - remove one in Settings > Git first"); return false; }
    char name[NAME_LEN];
    snprintf(name, sizeof name, "%s", outsiderInfo->name);
    snprintf(user->claims[user->claimCount++], TEXT_LEN, "%s", outsiderInfo->email[0] ? outsiderInfo->email : outsiderInfo->name);
    RecomputeAuthors();
    MarkDirty();
    Toast(TextFormat("Commits by %s now count as yours (undo it in Settings > Git)", name));
    return true;
}

/* ---------------- the People tab ---------------- */

/* Draws a repository's People tab. */
void TabPeople(Repo *repo)
{
    bool isOwner = IsMine(repo), isInvited = IsInvited(repo, currentUserId);
    float top = 186;
    float leftWidth = ((float)screenWidth - 60) * 0.55f;
    float x = 20;

    /* an invitation waiting for you */
    if (isInvited) {
        Rectangle inviteBox = {x, top, leftWidth, 60};
        DrawRectangleRec(inviteBox, (Color){20, 50, 90, 255});
        DrawRectangleLinesEx(inviteBox, 1, UI_LINK);
        DrawTextFit(TextFormat("%s invited you to collaborate on %s", UserName(repo->owner), repo->name),
                (int)inviteBox.x + 16, (int)inviteBox.y + 19, FONT_SIZE, (int)inviteBox.width - 290, UI_TEXT);
        if (Button((Rectangle){inviteBox.x + inviteBox.width - 260, inviteBox.y + 12, 120, 36}, "Accept", UI_GREEN)) {
            AcceptInvite(repo, currentUserId); MarkDirty();
            Toast(TextFormat("You can now edit %s", repo->name));
            return;
        }
        if (Button((Rectangle){inviteBox.x + inviteBox.width - 132, inviteBox.y + 12, 120, 36}, "Decline", UI_BUTTON)) {
            RemoveInvite(repo, currentUserId); MarkDirty();
            Toast("Invitation declined");
            return;
        }
        top += 76;
    }

    /* ---- left: who has access ---- */
    DrawTextBold("People with access", (int)x, (int)top + 4, FONT_SIZE, UI_TEXT);
    float listTop = top + 38, listBottom = (float)screenHeight - 20 - (isOwner ? 150 : 0);
    Rectangle listArea = {x, listTop, leftWidth, listBottom - listTop};
    int rowCount = 1 + repo->collaboratorCount + repo->inviteCount;
    float rowHeight = 62;
    ScrollArea(listArea, &peopleTabScroll, rowCount * rowHeight - 6);
    int action = 0, actionUser = -1;              /* 1 remove, 2 cancel invite, 3 leave */
    BeginScissorMode((int)listArea.x, (int)listArea.y, (int)listArea.width, (int)listArea.height);
    for (int i = 0; i < rowCount; i++) {
        int userId = i == 0 ? repo->owner : i <= repo->collaboratorCount ? repo->collaborators[i - 1] : repo->invites[i - 1 - repo->collaboratorCount];
        bool pending = i > repo->collaboratorCount;
        Rectangle row = {listArea.x, listArea.y + i * rowHeight - peopleTabScroll, listArea.width - 12, rowHeight - 6};
        if (row.y > listArea.y + listArea.height || row.y + row.height < listArea.y) continue;
        DrawRectangleRec(row, UI_PANEL);
        DrawRectangleLinesEx(row, 1, UI_BORDER);
        DrawAvatar(userId, (int)row.x + 34, (int)(row.y + row.height / 2), 20);
        const char *name = UserName(userId);
        Rectangle nameHit = {row.x + 66, row.y + 8, (float)MeasureText(name, FONT_SIZE), 24};
        bool nameHovered = IsHovered(listArea) && IsHovered(nameHit);
        DrawTextBold(name, (int)row.x + 66, (int)row.y + 9, FONT_SIZE, UI_LINK);
        if (nameHovered) { wantedCursor = MOUSE_CURSOR_POINTING_HAND; DrawLine((int)nameHit.x, (int)row.y + 31, (int)(nameHit.x + nameHit.width), (int)row.y + 31, UI_LINK); }
        if (nameHovered && WasClicked(nameHit)) { EndScissorMode(); OpenProfile(userId); return; }
        const char *role = i == 0 ? "Owner" : pending ? TextFormat("Invited %s - waiting for an answer", TimeAgo(repo->inviteTime[i - 1 - repo->collaboratorCount]))
                                                      : "Collaborator";
        DrawTextFit(TextFormat("%s%s", role, userId == currentUserId ? "  (you)" : ""), (int)row.x + 66, (int)row.y + 32, FONT_SIZE, (int)row.width - 230, UI_MUTED);

        Rectangle button = {row.x + row.width - 170, row.y + 12, 158, 32};
        if (isOwner && i > 0 && !pending && IsHovered(listArea) && ConfirmButton(button, "Remove", "Sure?", 100 + i)) { action = 1; actionUser = userId; }
        if (isOwner && pending && IsHovered(listArea) && ConfirmButton(button, "Cancel invite", "Sure?", 100 + i)) { action = 2; actionUser = userId; }
        if (!isOwner && !pending && i > 0 && userId == currentUserId && IsHovered(listArea) && ConfirmButton(button, "Leave", "Really leave?", 100 + i)) { action = 3; actionUser = userId; }
    }
    EndScissorMode();
    DrawScrollbar(listArea, peopleTabScroll, rowCount * rowHeight - 6);
    if (action) {
        const char *who = UserName(actionUser);
        char whoCopy[NAME_LEN];
        snprintf(whoCopy, NAME_LEN, "%s", who);
        RemoveMember(repo, actionUser);
        MarkDirty();
        if (action == 1) Toast(TextFormat("%s can no longer edit this repository", whoCopy));
        if (action == 2) Toast(TextFormat("Invitation to %s cancelled", whoCopy));
        if (action == 3) { Toast(TextFormat("You've left %s", repo->name)); SelectFileName(""); }
        return;
    }

    /* ---- owner: invite someone ---- */
    if (isOwner) {
        float inviteY = (float)screenHeight - 20 - 140;
        DrawLine((int)x, (int)inviteY, (int)(x + leftWidth), (int)inviteY, UI_BORDER);
        DrawTextBold("Invite a collaborator", (int)x, (int)inviteY + 12, FONT_SIZE, UI_TEXT);
        bool enter = TextFieldWidget(&inviteField, (Rectangle){x, inviteY + 42, leftWidth - 130, 40}, "their username");
        if (Button((Rectangle){x + leftWidth - 120, inviteY + 42, 120, 40}, "Invite", UI_GREEN) || enter) {
            const char *error = InviteUser(repo, inviteField.text);
            if (error) Toast(error);
            else {
                Toast(TextFormat("Invited %s - they'll see it on their profile", inviteField.text));
                TextFieldClear(&inviteField);
                MarkDirty();
            }
        }
        /* everyone who could be invited, as quick picks */
        float chipX = x + MeasureText("People here:", FONT_SIZE) + 12;
        DrawText("People here:", (int)x, (int)inviteY + 98, FONT_SIZE, UI_MUTED);
        int shown = 0;
        for (int i = 0; i < userCount; i++) {
            User *user = &users[i];
            if (user->id == repo->owner || IsCollaborator(repo, user->id) || IsInvited(repo, user->id)) continue;
            int chipWidth = MeasureText(user->name, FONT_SIZE) + 20;
            if (chipX + chipWidth > x + leftWidth) break;
            Rectangle chip = {chipX, inviteY + 94, (float)chipWidth, 28};
            bool hovered = IsHovered(chip);
            DrawRectangleRounded(chip, 0.5f, 6, hovered ? UI_HOVER : UI_BUTTON);
            DrawText(user->name, (int)chip.x + 10, (int)chip.y + 4, FONT_SIZE, UI_LINK);
            if (hovered) wantedCursor = MOUSE_CURSOR_POINTING_HAND;
            if (WasClicked(chip)) { snprintf(inviteField.text, TEXT_LEN, "%s", user->name); inviteField.length = (int)strlen(inviteField.text); }
            chipX += chipWidth + 8;
            shown++;
        }
        if (!shown) DrawText("everyone is already here", (int)chipX, (int)inviteY + 98, FONT_SIZE, UI_MUTED);
    }

    /* ---- right: contributions ---- */
    float rightX = x + leftWidth + 20, rightWidth = (float)screenWidth - 20 - rightX;
    Rectangle card = {rightX, 186, rightWidth, (float)screenHeight - 206};
    DrawRectangleRec(card, UI_PANEL);
    DrawRectangleLinesEx(card, 1, UI_BORDER);
    DrawTextBold("Contributions", (int)rightX + 18, (int)card.y + 16, FONT_SIZE, UI_TEXT);
    if (!IsMultiUser(repo)) {
        DrawTextFit(TextFormat("Only %s has worked on this repository so far.", UserName(repo->owner)),
                (int)rightX + 18, (int)card.y + 52, FONT_SIZE, (int)rightWidth - 36, UI_MUTED);
        DrawTextFit(isOwner ? "Invite a collaborator to see who did what here." : "Once more people take part, a breakdown appears here.",
                (int)rightX + 18, (int)card.y + 78, FONT_SIZE, (int)rightWidth - 36, UI_MUTED);
        return;
    }
    BuildShares(repo);
    DrawTextFit(TextFormat("by lines changed (added + removed)  |  %d in total", shareTotal), (int)rightX + 18, (int)card.y + 42, FONT_SIZE, (int)rightWidth - 36, UI_MUTED);
    float radius = fminf(rightWidth * 0.3f, 100);
    Vector2 center = {rightX + rightWidth / 2, card.y + 84 + radius};
    int hoveredSlice = DrawPie(center, radius);
    if (hoveredSlice >= 0) ShareTip(hoveredSlice);

    /* the key: every person with their colour, commits, share and lines */
    float keyY = center.y + radius + 26;
    int claimed = -1;
    for (int i = 0; i < shareCount && keyY + 24 < card.y + card.height; i++) {
        Share *share = &shares[i];
        bool isOutsider = IsOutsider(share->userId);
        Rectangle row = {rightX + 10, keyY - 4, rightWidth - 20, 28};
        bool hovered = IsHovered(row);
        if (hovered && isOutsider) DrawRectangleRec(row, UI_HOVER);
        DrawRectangleRounded((Rectangle){rightX + 18, keyY + 3, 14, 14}, 0.3f, 4, share->color);
        const char *valueText = TextFormat("%d line%s  %d%%", share->lines, share->lines == 1 ? "" : "s", Percent(share->lines, shareTotal));
        int valueWidth = MeasureText(valueText, FONT_SIZE);
        if (isOutsider && hovered && currentUserId >= 0) {  /* one of your other email addresses? */
            Rectangle button = {rightX + rightWidth - 18 - 130, keyY - 3, 130, 26};
            if (ConfirmButton(button, "That's me", "Sure? Click", 200 + i)) claimed = share->userId;
            valueWidth = 130;
        } else DrawText(valueText, (int)(rightX + rightWidth - 18) - valueWidth, (int)keyY, FONT_SIZE, UI_MUTED);
        int nameWidth = (int)(rightWidth - 18 - valueWidth - 16 - 42);
        if (isOutsider) {                                  /* the name, then the tag in grey */
            const char *name = UserName(share->userId);
            DrawTextFit(name, (int)rightX + 42, (int)keyY, FONT_SIZE, nameWidth, UI_TEXT);
            int usedWidth = MeasureText(name, FONT_SIZE);
            if (usedWidth + 40 < nameWidth) DrawTextFit("(not a Mini GitHub user)", (int)rightX + 42 + usedWidth + 8, (int)keyY, FONT_SIZE, nameWidth - usedWidth - 8, UI_MUTED);
            if (hovered) {
                Outsider *outsider = OutsiderOf(share->userId);
                SetTooltip(outsider->name, outsider->email[0] ? outsider->email : "(no email in their commits)",
                       currentUserId >= 0 ? "Not a Mini GitHub user. If this is you (another email of yours), press That's me" : "Not a Mini GitHub user");
            }
        } else DrawTextFit(ShareName(share), (int)rightX + 42, (int)keyY, FONT_SIZE, nameWidth, UI_TEXT);
        keyY += 30;
    }
    if (claimed != -1 && ClaimIdentity(currentUserId, claimed)) return;
}
