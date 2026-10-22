/*
 * app_state.h - what the program is showing right now
 *
 * The current screen and tab, the repository and commit open, every text
 * box, scroll positions, and per-frame input state.
 */

#ifndef MINIGITHUB_APP_STATE_H
#define MINIGITHUB_APP_STATE_H

#include "common.h"

typedef enum { SCREEN_SIGN_IN, SCREEN_CREATE_ACCOUNT, SCREEN_FORGOT_PASSWORD, SCREEN_RECOVERY_CODE, SCREEN_NEW_REPO, SCREEN_REPO, SCREEN_COMMIT, SCREEN_SETTINGS, SCREEN_DAY, SCREEN_PROFILE, SCREEN_PEOPLE, SCREEN_UPLOAD, SCREEN_LOAD_FAILED } Screen;
typedef enum { TAB_CODE, TAB_COMMITS, TAB_ISSUES, TAB_PEOPLE } Tab;
typedef struct { char text[TEXT_LEN]; int length; bool active; bool secret; bool reveal; } TextField;
typedef struct { int cursor; float scrollY, scrollX; bool focused; } EditorState;
extern Screen screen;
extern Tab repoTab;
extern int currentRepo, currentCommit, currentChange;
extern char selectedFile[PATH_LEN];  /* the file open in the Code tab ("" = pick the first) */
extern TextField searchField, repoNameField, repoDescField, newFileField, commitMessageField, issueTitleField, usernameField, bioField, avatarPathField, recoveryCodeField, uploadPathField, uploadMessageField, inviteField, emailField, branchNameField;
extern TextField passwordField, confirmPasswordField;
extern TextField currentPasswordField, newPasswordField;
extern TextField confirmNewPasswordField, deletePasswordField;
extern EditorState editor;
extern bool newRepoWithReadme, withExampleRepo, newRepoIsPrivate;
extern int homeRepoView;  /* 0 = your repositories, 1 = everyone's */
extern float graphScroll, fileListScroll, commitListScroll, issueListScroll, changeListScroll, diffScroll, dayScroll, dayReposScroll;
extern char shownRecoveryCode[24];  /* recovery code on screen right now (never saved) */
extern bool codeAfterReset;         /* the code screen was reached by resetting a password */
extern float profileScroll, peopleScroll, settingsScroll, peopleTabScroll;
extern int profileUserId;     /* whose profile page is open */
extern int dayUserId;         /* whose contributions the day page shows */
extern int settingsTab;       /* 0 = profile, 1 = password & account, 2 = git */
extern Color accentColor;     /* the signed-in user's theme colour */
extern long long currentDay;  /* midnight at the start of the day being viewed */
extern bool openedFromDay;    /* commit page was opened from a day page: go back there */
extern int failedLogins;
extern float signInPauseSeconds;  /* sign-in paused after too many wrong passwords */
extern int screenWidth, screenHeight;
extern bool clickHandled;  /* stops one click triggering two things */
extern int wantedCursor;
extern int gitPort;                /* the git server's port (see "Talking to real git"), 0 = off */
extern char gitProblem[TEXT_LEN];  /* why it isn't running */
extern bool unsavedChanges;        /* something changed and needs saving */
extern unsigned changeCount;       /* goes up with every change (MarkDirty), so the main loop can tell when changes stop */
extern char toastText[TEXT_LEN];
extern float toastSeconds;
extern int armedButtonId;  /* for "click again to confirm" buttons */
extern float armedSeconds;

#endif
