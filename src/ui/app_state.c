/*
 * app_state.c - what the program is showing right now
 *
 * The current screen and tab, the repository and commit open, every text
 * box, scroll positions, and per-frame input state.
 */

#include "app_state.h"

/* ------------------------------------------------------------------ */
/* App state                                                          */
/* ------------------------------------------------------------------ */

Screen screen = SCREEN_SIGN_IN;
Tab    repoTab    = TAB_CODE;
int    currentRepo = -1, currentCommit = -1, currentChange = -1;
char   selectedFile[PATH_LEN];      /* the file open in the Code tab ("" = pick the first) */

TextField searchField, repoNameField, repoDescField, newFileField, commitMessageField, issueTitleField, usernameField, bioField, avatarPathField, recoveryCodeField, uploadPathField, uploadMessageField, inviteField, emailField, branchNameField;
TextField passwordField    = {.secret = true}, confirmPasswordField    = {.secret = true};
TextField currentPasswordField = {.secret = true}, newPasswordField  = {.secret = true};
TextField confirmNewPasswordField= {.secret = true}, deletePasswordField  = {.secret = true};
EditorState editor;
bool  newRepoWithReadme = true, withExampleRepo = true, newRepoIsPrivate = false;
int   homeRepoView;            /* 0 = your repositories, 1 = everyone's */
float graphScroll, fileListScroll, commitListScroll, issueListScroll, changeListScroll, diffScroll, dayScroll, dayReposScroll;
char  shownRecoveryCode[24];   /* recovery code on screen right now (never saved) */
bool  codeAfterReset;          /* the code screen was reached by resetting a password */
float profileScroll, peopleScroll, settingsScroll, peopleTabScroll;
int   profileUserId = -1;                  /* whose profile page is open             */
int   dayUserId  = -1;                     /* whose contributions the day page shows */
int   settingsTab;                         /* 0 = profile, 1 = password & account, 2 = git */
Color accentColor = {247, 129, 102, 255};  /* the signed-in user's theme colour */
long long currentDay;                      /* midnight at the start of the day being viewed      */
bool  openedFromDay;                       /* commit page was opened from a day page: go back there */
int   failedLogins;
float signInPauseSeconds;      /* sign-in paused after too many wrong passwords */

int   screenWidth, screenHeight;
bool  clickHandled;    /* stops one click triggering two things */
int   wantedCursor;
int   gitPort;               /* the git server's port (see "Talking to real git"), 0 = off */
char  gitProblem[TEXT_LEN];  /* why it isn't running                                       */
bool  unsavedChanges;        /* something changed and needs saving    */
char  toastText[TEXT_LEN];
float toastSeconds;
int   armedButtonId;         /* for "click again to confirm" buttons  */
float armedSeconds;
