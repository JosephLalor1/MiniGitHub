/*
 * screen_new_repo.c - the "Create a new repository" form
 */

#include "screen_new_repo.h"
#include "app_state.h"
#include "drafts.h"
#include "model.h"
#include "nav_history.h"
#include "navigation.h"
#include "repo.h"
#include "ui_core.h"
#include "users.h"
#include "util.h"
#include "widgets.h"

static void TryCreateRepo(void)
{
    char name[NAME_LEN];
    SanitizeName(repoNameField.text, name);
    if (!name[0]) { Toast("Please give the repository a name"); return; }
    for (int i = 0; i < repoCount; i++)
        if (repos[i].owner == currentUserId && EqualsIgnoreCase(repos[i].name, name)) { Toast("You already have a repository with that name"); return; }
    Repo *repo = NewRepo(currentUserId, name, repoDescField.text);
    if (!repo) { Toast("This computer has reached the maximum number of repositories"); return; }
    repo->isPrivate = newRepoIsPrivate;
    if (newRepoWithReadme) {
        DraftSetText(repo, currentUserId, "README.md", TextFormat("# %s\n\n%s\n", name, repoDescField.text[0] ? repoDescField.text : "A new project."));
        DoCommit(repo, "Initial commit");
    }
    MarkDirty();
    OpenRepo(repoCount - 1);
    Toast(TextFormat("Created %s", name));
}

/* Draws the "Create a new repository" form. */
void ScreenNewRepo(void)
{
    TextField *fields[] = {&repoNameField, &repoDescField};
    CycleFocusOnTab(fields, 2);

    Rectangle panel = {(float)screenWidth / 2 - 310, 100, 620, 430};
    DrawRectangleRec(panel, UI_PANEL);
    DrawRectangleLinesEx(panel, 1, UI_BORDER);
    int x = (int)panel.x + 30, y = (int)panel.y;

    DrawTextBold("Create a new repository", x, y + 28, 30, UI_TEXT);
    DrawText(TextFormat("Owner: %s", UserName(currentUserId)), x, y + 70, FONT_SIZE, UI_MUTED);

    DrawText("Repository name", x, y + 116, FONT_SIZE, UI_TEXT);
    bool nameEnter = TextFieldWidget(&repoNameField, (Rectangle){(float)x, (float)y + 142, 560, 40}, "e.g. my-project");
    DrawText("Description (optional)", x, y + 198, FONT_SIZE, UI_TEXT);
    bool descEnter = TextFieldWidget(&repoDescField, (Rectangle){(float)x, (float)y + 224, 560, 40}, "What is this project about?");
    Checkbox(&newRepoWithReadme, x, y + 287, "Add a README file");
    /* who can see it */
    int visibilityX = x + 300;
    for (int option = 0; option < 2; option++) {
        Rectangle button = {(float)visibilityX + option * 132, (float)y + 280, 124, 36};
        bool selected = newRepoIsPrivate == (option == 1);
        if (ButtonEnabled(button, option ? "Private" : "Public", selected ? (option ? (Color){110, 80, 20, 255} : UI_BLUE) : UI_BUTTON, true)) newRepoIsPrivate = option == 1;
        if (IsHovered(button)) SetTooltip(option ? "Private" : "Public", option ? "Only you and people you invite can see it" : "Everyone on this computer can see and fork it", NULL);
    }

    bool createPressed = Button((Rectangle){panel.x + panel.width - 230, (float)y + 350, 200, 44}, "Create repository", UI_GREEN);
    bool cancelPressed = Button((Rectangle){panel.x + panel.width - 360, (float)y + 350, 120, 44}, "Cancel", UI_BUTTON);
    if (cancelPressed) { if (!NavBack()) GoHome(); return; }
    if (createPressed || nameEnter || descEnter) TryCreateRepo();
}
