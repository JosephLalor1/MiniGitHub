/*
 * Mini GitHub
 * -----------
 * A tiny GitHub-style app that runs entirely on your own computer.
 *
 *   - User accounts: sign up, sign in, sign out, change password, delete account.
 *     Passwords are never stored - only a salted PBKDF2-SHA256 hash of them.
 *   - Password boxes have an "eye" button to show / hide what you typed.
 *   - Forgot your password? Each account gets a one-time recovery code
 *     (shown when you sign up, or made in Account settings). Enter it on the
 *     "Forgot password?" screen to choose a new password.
 *   - Repositories belong to a user: create, star and delete your own (a
 *     repository's Settings tab makes it public or private, or deletes it)
 *   - Browse everyone's repositories (read-only unless they're yours)
 *   - A built-in text editor for the files in your repositories
 *   - Uploads: drag files from your computer onto one of your repositories (or
 *     paste their paths), check the list, and save them as one commit. Text files
 *     can then be edited; pictures are previewed; any other file is stored as-is
 *   - Language detection: reads each file and guesses what it's written in (20
 *     languages), and shows each repository's breakdown of languages
 *   - Downloads: any file, a whole repository (as a .zip), or the files as they
 *     were in any commit - saved to your Downloads folder
 *   - Commits: a snapshot of every file, saved with a message and an author
 *   - Commit history and line-by-line diffs (what was added / removed)
 *   - Issues: anyone can open one; the repo owner or the issue's author can close it
 *   - Forks: make your own copy of someone else's repository (with its whole
 *     history) and change it freely. A fork shows how many commits it is ahead
 *     of / behind the original, and "Sync fork" brings in the original's new
 *     commits: copied straight across when you haven't committed anything
 *     yourself (a fast-forward), or combined with your work in a merge commit
 *     when you have - unless you both changed the same file.
 *   - Profiles: a picture (PNG or JPG, dragged onto the window or by file path), a short
 *     bio, a circle colour, and a choice of animated themes (Classic is the plain look).
 *     A splash screen shows one of them, picked at random, while the app opens
 *   - A People page and a profile page for every user. Your own profile is your
 *     home page: it's where you land after signing in, and it lists your
 *     repositories (or those of people you follow, or everyone's), with a search
 *     box and a New repository button. The column about you hides behind your
 *     picture there: hover the picture to see it, click it to keep it open
 *   - Following: follow other people from their profile or the People page
 *   - A contribution graph of the last six months: hover a day to see what you
 *     did, click it to open a page listing every commit and issue from that day
 *   - Real git: every repository is a real git repository too. Mini GitHub runs
 *     a small git server on this computer only (http://localhost:7979), so
 *     git clone, git pull, git fetch and git push all work. Pushing needs your
 *     Mini GitHub username and password, and only the owner and collaborators
 *     may push. Press "Copy repo address" on a repository for its address; Settings > Git
 *     explains the rest. Commits keep git's own fingerprints (SHA-1), files are
 *     kept byte for byte, and folders show as a tree you can open and close.
 *
 * Everything is saved automatically to "minigithub.dat" in the folder the
 * program is run from. Older save files are upgraded automatically (a copy of
 * the old file is kept as minigithub.dat.v6-backup or similar). A save file
 * from before accounts existed: the first account created takes over its
 * repositories.
 *
 * Language: C99    Library: raylib 5.x    Build: make (or build.bat)
 *
 * Where things are:
 *   main.c      this file: the window and the loop that runs every frame
 *   common.h    limits and headers every file uses
 *   core/       building blocks that know nothing about the rest of the
 *               program: SHA-1, SHA-256, zlib, JPEG, a byte buffer...
 *   data/       the data model and everything done to it: repositories,
 *               commits, drafts, branches, merging, forks, accounts,
 *               the save file
 *   git/        the git server: packs in and out, pushes, the web server
 *   ui/         pieces of the interface shared by the screens: widgets,
 *               the text editor, themes, menus, navigation, Back
 *   screens/    one file per page or repository tab
 * Every .c file has a .h file next to it listing what other files may use.
 */

#include "common.h"
#include "app_state.h"
#include "avatars.h"
#include "git_server.h"
#include "header_bar.h"
#include "model.h"
#include "nav_history.h"
#include "navigation.h"
#include "previews.h"
#include "repo.h"
#include "repo_header.h"
#include "repo_people.h"
#include "save_file.h"
#include "screen_commit.h"
#include "screen_day.h"
#include "screen_load_fail.h"
#include "screen_new_repo.h"
#include "screen_people.h"
#include "screen_profile.h"
#include "screen_settings.h"
#include "screen_sign_in.h"
#include "screen_splash.h"
#include "screen_upload.h"
#include "tab_code.h"
#include "tab_commits.h"
#include "tab_issues.h"
#include "tab_settings.h"
#include "themes.h"
#include "ui_core.h"
#include "uploads.h"
#include "users.h"
#include "widgets.h"

/* ------------------------------------------------------------------ */
/* Main                                                               */
/* ------------------------------------------------------------------ */
int main(void)
{
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT);
    InitWindow(1280, 800, "Mini GitHub");
    SetWindowMinSize(1000, 680);
    SetExitKey(KEY_NULL);            /* Esc goes "back" instead of quitting */
    SetTargetFPS(60);

    SplashIntro();                   /* the logo appears, and stays up while loading */
    Load();
    if (loadFailed) screen = SCREEN_LOAD_FAILED;  /* say so, and wait: nothing is saved until you choose */
    else AfterLoad();
    SplashFinish();

    int   lastCursor = -1;                 /* the mouse cursor shape set last frame */

    /* Each frame: answer git, handle dropped files and the Back keys, draw
       the current page (which also handles its clicks and typing), then
       remember the page for Back and save once the changes have stopped. */
    while (!WindowShouldClose() && !quitNow) {
        screenWidth = GetScreenWidth();
        screenHeight = GetScreenHeight();
        clickHandled  = false;
        hoverBlocked = false;
        wantedCursor = MOUSE_CURSOR_DEFAULT;
        float frameSeconds = GetFrameTime();
        GitServerPoll();
        if (toastSeconds > 0) toastSeconds -= frameSeconds;
        if (signInPauseSeconds > 0)  signInPauseSeconds  -= frameSeconds;
        if (armedButtonId && (armedSeconds -= frameSeconds) <= 0) armedButtonId = 0;

        /* the signed-in user's theme: its colour, and see-through panels if it draws a background */
        int backgroundTheme = currentUserId < 0 ? THEME_CLASSIC
                    : screen == SCREEN_PROFILE && FindUserById(profileUserId) ? UserTheme(profileUserId)  /* their page, their theme */
                    : UserTheme(currentUserId);
        accentColor = THEMES[backgroundTheme].accent;
        unsigned char panelAlpha = backgroundTheme == THEME_CLASSIC ? 255 : 215;
        UI_PANEL.a = panelAlpha;
        UI_HOVER.a = panelAlpha;
        UI_BG.a    = backgroundTheme == THEME_CLASSIC ? 255 : 230;

        /* a picture dropped onto the window */
        if (IsFileDropped()) {
            FilePathList droppedFiles = LoadDroppedFiles();
            User *signedIn = FindUserById(currentUserId);
            bool inRepo = signedIn && currentRepo >= 0 && currentRepo < repoCount &&
                          (screen == SCREEN_REPO || screen == SCREEN_COMMIT || screen == SCREEN_UPLOAD);
            if (signedIn && screen == SCREEN_SETTINGS && droppedFiles.count > 0) {
                settingsTab = 0;
                UseAvatarPath(signedIn, droppedFiles.paths[0]);
            } else if (inRepo && CanEdit(&repos[currentRepo])) {
                if (screen != SCREEN_UPLOAD) OpenUpload();
                StagePaths(droppedFiles.paths, (int)droppedFiles.count);
            } else if (inRepo) {
                Toast("Only the owner and collaborators can upload files to this repository");
            } else if (signedIn) {
                Toast("To upload files, open one of your repositories and drop them there");
            }
            UnloadDroppedFiles(droppedFiles);
        }

        /* every screen except sign-in / sign-up needs someone signed in */
        if (currentUserId < 0 && screen != SCREEN_SIGN_IN && screen != SCREEN_CREATE_ACCOUNT && screen != SCREEN_FORGOT_PASSWORD && screen != SCREEN_LOAD_FAILED) GoToSignIn();
        /* files waiting to be uploaded are let go once you leave the upload page */
        if (screen != SCREEN_UPLOAD && stagedFileCount) ClearStagedFiles();
        /* the recovery code only stays in memory while its screen is open */
        if (screen != SCREEN_RECOVERY_CODE && shownRecoveryCode[0]) memset(shownRecoveryCode, 0, sizeof shownRecoveryCode);

        bool goBack = false;
        if (IsKeyPressed(KEY_ESCAPE)) {
            bool anyFocus = editor.focused || searchField.active || repoNameField.active || repoDescField.active ||
                            newFileField.active || commitMessageField.active || issueTitleField.active || usernameField.active ||
                            passwordField.active || confirmPasswordField.active || currentPasswordField.active || newPasswordField.active ||
                            confirmNewPasswordField.active || deletePasswordField.active || bioField.active || avatarPathField.active || recoveryCodeField.active ||
                            uploadPathField.active || uploadMessageField.active || inviteField.active || emailField.active || branchNameField.active;
            if (codeMenuOpen || branchMenuOpen) codeMenuOpen = branchMenuOpen = false;
            else if (anyFocus) Unfocus();
            else goBack = true;
        }
        /* the mouse's side "back" button and Alt+Left do the same */
        if (IsMouseButtonPressed(MOUSE_BUTTON_BACK) || IsMouseButtonPressed(MOUSE_BUTTON_SIDE) ||
            ((IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT)) && IsKeyPressed(KEY_LEFT))) goBack = true;
        if (goBack) {                      /* like the Back button, but never signs you out */
            if (screen == SCREEN_CREATE_ACCOUNT || screen == SCREEN_FORGOT_PASSWORD) GoToSignIn();
            else if (screen == SCREEN_RECOVERY_CODE) GoHome();
            else if (screen != SCREEN_SIGN_IN && screen != SCREEN_LOAD_FAILED) NavBack();
        }

        BeginDrawing();
        ClearBackground(UI_BASE);
        if (backgroundTheme != THEME_CLASSIC) DrawTheme(backgroundTheme, (Rectangle){0, 0, (float)screenWidth, (float)screenHeight}, 0.55f);
        DrawHeader();

        switch (screen) {
        case SCREEN_LOAD_FAILED: ScreenLoadFail(); break;
        case SCREEN_SIGN_IN:    ScreenSignIn();    break;
        case SCREEN_CREATE_ACCOUNT: ScreenCreateAccount(); break;
        case SCREEN_FORGOT_PASSWORD:   ScreenForgot();   break;
        case SCREEN_RECOVERY_CODE: ScreenRecovery(); break;
        case SCREEN_SETTINGS:  ScreenSettings();  break;
        case SCREEN_DAY:      ScreenDay();      break;
        case SCREEN_PROFILE:  ScreenProfile();  break;
        case SCREEN_PEOPLE:   ScreenPeople();   break;
        case SCREEN_NEW_REPO:      ScreenNewRepo();      break;
        case SCREEN_REPO: {
            Repo *repo = &repos[currentRepo];
            if (!CanSee(repo, currentUserId)) { GoHome(); Toast("That repository is private"); break; }
            if (DrawRepoHeader(repo, repoTab)) break;
            if      (repoTab == TAB_CODE)    TabCode(repo);
            else if (repoTab == TAB_COMMITS) TabCommits(repo);
            else if (repoTab == TAB_ISSUES)  TabIssues(repo);
            else if (repoTab == TAB_PEOPLE)  TabPeople(repo);
            else                         TabSettings(repo);
        } break;
        case SCREEN_COMMIT: {
            Repo *repo = &repos[currentRepo];
            if (!CanSee(repo, currentUserId)) { GoHome(); Toast("That repository is private"); break; }
            if (DrawRepoHeader(repo, TAB_COMMITS)) break;
            if (screen == SCREEN_COMMIT) ScreenCommit(repo);
        } break;
        case SCREEN_UPLOAD: {
            Repo *repo = &repos[currentRepo];
            if (!CanSee(repo, currentUserId)) { GoHome(); Toast("That repository is private"); break; }
            if (DrawRepoHeader(repo, TAB_CODE)) break;
            if (screen == SCREEN_UPLOAD) ScreenUpload(repo);
        } break;
        }

        if (screen == SCREEN_REPO || screen == SCREEN_COMMIT || screen == SCREEN_UPLOAD) { CodeMenu(&repos[currentRepo], true); BranchMenu(&repos[currentRepo], true); }
        else codeMenuOpen = branchMenuOpen = false;
        DrawTooltip();
        DrawToast();
        EndDrawing();
        NavTrack();                        /* remember the page you were on, if you've moved */

        if (wantedCursor != lastCursor) { SetMouseCursor(wantedCursor); lastCursor = wantedCursor; }

        if (SaveIsDue(frameSeconds)) { Save(); unsavedChanges = false; }
    }

    GitServerStop();
    if (unsavedChanges) Save();
    ClearSecrets();
    for (int i = 0; i < repoCount; i++) FreeRepo(&repos[i]);
    for (int i = 0; i < userCount; i++) { FreeAvatarTexture(&users[i]); free(users[i].avatar); }
    ClearStagedFiles();
    for (int i = 0; i < PREVIEW_SLOTS; i++) if (previewSlots[i].state == 1) UnloadTexture(previewSlots[i].texture);
    CloseWindow();
    return 0;
}
