/*
 * navigation.c - moving between screens
 *
 * Signing in and out, and opening profiles, repositories, commits and
 * days.
 */

#include "navigation.h"
#include "app_state.h"
#include "diff.h"
#include "editor.h"
#include "model.h"
#include "passwords.h"
#include "repo.h"
#include "tab_code.h"
#include "ui_core.h"
#include "users.h"
#include "widgets.h"

/* ------------------------------------------------------------------ */
/* Navigation                                                         */
/* ------------------------------------------------------------------ */
/* Takes the keyboard focus away from every text box and the editor. */
void Unfocus(void)
{
    searchField.active = repoNameField.active = repoDescField.active = false;
    newFileField.active = commitMessageField.active = issueTitleField.active = false;
    usernameField.active = passwordField.active = confirmPasswordField.active = recoveryCodeField.active = false;
    uploadPathField.active = uploadMessageField.active = inviteField.active = emailField.active = branchNameField.active = false;
    currentPasswordField.active = newPasswordField.active = confirmNewPasswordField.active = deletePasswordField.active = false;
    editor.focused = false;
    armedButtonId = 0;
}

/* Wipe every password box so passwords don't sit around in memory or on
   screen, and close every "eye" so they start hidden again. */
void ClearSecrets(void)
{
    TextField *secrets[] = {&passwordField, &confirmPasswordField, &currentPasswordField, &newPasswordField, &confirmNewPasswordField, &deletePasswordField, &recoveryCodeField};
    int count = (int)(sizeof secrets / sizeof secrets[0]);
    for (int i = 0; i < count; i++) { memset(secrets[i]->text, 0, sizeof secrets[i]->text); secrets[i]->length = 0; secrets[i]->reveal = false; }
}

static void PrefillUsername(void)
{
    snprintf(usernameField.text, TEXT_LEN, "%s", lastUserName);
    usernameField.length = (int)strlen(usernameField.text);
}

/* Shows the sign-in screen (with the last username filled in). */
void GoToSignIn(void)
{
    Unfocus();
    ClearSecrets();
    PrefillUsername();
    screen = SCREEN_SIGN_IN;
    if (usernameField.length) passwordField.active = true; else usernameField.active = true;
}
/* Shows the create-account screen. */
void GoToCreateAccount(void)
{
    Unfocus();
    ClearSecrets();
    TextFieldClear(&usernameField);
    withExampleRepo = true;
    screen = SCREEN_CREATE_ACCOUNT;
    usernameField.active = true;
}

/* Shows the "Reset your password" screen. */
void GoToForgotPassword(void)
{
    Unfocus();
    ClearSecrets();
    PrefillUsername();
    screen = SCREEN_FORGOT_PASSWORD;
    if (usernameField.length) recoveryCodeField.active = true; else usernameField.active = true;
}

/* Shows a freshly made recovery code (it's only ever shown once). */
void ShowRecoveryCode(User *user, bool afterReset)
{
    Unfocus();
    NewRecoveryCode(user, shownRecoveryCode);
    codeAfterReset = afterReset;
    MarkDirty();
    screen = SCREEN_RECOVERY_CODE;
}

/* Goes to your profile page (the home page), or sign-in if nobody is signed in. */
void GoHome(void)
{
    if (currentUserId < 0) { GoToSignIn(); return; }
    OpenProfile(currentUserId);                    /* your profile is your home page */
    currentRepo = -1;
}

/* Signs in as userId and goes to their home page. */
void SignIn(int userId)
{
    User *user = FindUserById(userId);
    if (!user) return;
    currentUserId = userId;
    snprintf(lastUserName, NAME_LEN, "%s", user->name);
    ClearSecrets();
    TextFieldClear(&searchField);
    homeRepoView = 0;
    profileUserId = -1;
    failedLogins = 0;
    MarkDirty();
    GoHome();
}

/* Signs out and goes back to the sign-in screen. */
void SignOut(void)
{
    currentUserId = -1;
    currentRepo = -1;
    GoToSignIn();
    Toast("You've signed out");
}

/* Opens the settings screen, with the bio and email boxes filled in. */
void OpenSettings(void)
{
    User *user = FindUserById(currentUserId);
    Unfocus();
    ClearSecrets();
    settingsTab = 0;
    settingsScroll = 0;
    TextFieldClear(&avatarPathField);
    snprintf(bioField.text, TEXT_LEN, "%s", user ? user->bio : "");
    bioField.length = (int)strlen(bioField.text);
    snprintf(emailField.text, TEXT_LEN, "%s", user ? user->email : "");
    emailField.length = (int)strlen(emailField.text);
    screen = SCREEN_SETTINGS;
}

/* Opens a person's profile page. */
void OpenProfile(int userId)
{
    if (!FindUserById(userId)) { if (currentUserId >= 0 && userId != currentUserId) GoHome(); return; }
    Unfocus();
    if (profileUserId != userId) {               /* a different person: start at the top, no search */
        profileScroll = 0;
        TextFieldClear(&searchField);
    }
    profileUserId = userId;
    screen = SCREEN_PROFILE;
}

/* Opens the People screen. */
void OpenPeople(void)
{
    Unfocus();
    peopleScroll = 0;
    screen = SCREEN_PEOPLE;
}

/* Opens the page for one day of userId's contributions. */
void OpenDay(int userId, long long day)
{
    Unfocus();
    dayUserId = userId;
    currentDay = day;
    dayScroll = dayReposScroll = 0;
    screen = SCREEN_DAY;
}

/* Opens a repository on its Code tab. */
void OpenRepo(int repoIndex)
{
    Unfocus();
    openedFromDay = false;
    currentRepo = repoIndex;
    screen  = SCREEN_REPO;
    repoTab     = TAB_CODE;
    SelectFileName("");
    fileListScroll = commitListScroll = issueListScroll = peopleTabScroll = graphScroll = 0;
    ForgetScratchFile();
    TextFieldClear(&inviteField);
    TextFieldClear(&commitMessageField);
    TextFieldClear(&newFileField);
    TextFieldClear(&issueTitleField);
}

/* Picks which changed file of the open commit to show (-1: none). */
void SelectChange(int changeIndex)
{
    currentChange = changeIndex;
    diffScroll = 0;
    if (changeIndex >= 0) {
        Repo *repo = &repos[currentRepo];
        if (commitChanges[changeIndex].binary) diffOpCount = 0;
        else ComputeDiff(BlobText(repo, commitChanges[changeIndex].oldBlob), BlobText(repo, commitChanges[changeIndex].newBlob), NULL, NULL);
    } else diffOpCount = 0;
}

/* Opens one commit of the open repository. */
void OpenCommit(int commitIndex)
{
    Unfocus();
    currentCommit = commitIndex;
    openedFromDay = false;
    screen = SCREEN_COMMIT;
    BuildChanges(&repos[currentRepo], commitIndex);
    changeListScroll = 0;
    SelectChange(commitChangeCount ? 0 : -1);
}
