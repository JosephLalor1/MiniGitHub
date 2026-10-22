/*
 * account_actions.c - what the account forms do
 *
 * Signing in, creating an account, resetting a forgotten password,
 * changing a password and deleting an account.
 */

#include "account_actions.h"
#include "app_state.h"
#include "drafts.h"
#include "model.h"
#include "navigation.h"
#include "passwords.h"
#include "repo.h"
#include "ui_core.h"
#include "users.h"
#include "util.h"

/* An example repository for new accounts that want one. */
static void SeedExample(int owner)
{
    Repo *repo = NewRepo(owner, "hello-world", "An example repository to try things out");
    if (!repo) return;
    DraftSetText(repo, owner, "README.md",
        "# hello-world\n"
        "\n"
        "Welcome to Mini GitHub!\n"
        "\n"
        "This is an example repository so you can see how things work.\n");
    DraftSetText(repo, owner, "main.c",
        "#include <stdio.h>\n"
        "\n"
        "int main(void)\n"
        "{\n"
        "    printf(\"Hello, world!\\n\");\n"
        "    return 0;\n"
        "}\n");
    DoCommit(repo, "Initial commit");

    DraftSetText(repo, owner, "README.md",
        "# hello-world\n"
        "\n"
        "Welcome to Mini GitHub!\n"
        "\n"
        "This is an example repository so you can see how things work.\n"
        "\n"
        "## How to use it\n"
        "\n"
        "1. Open the Code tab and edit a file\n"
        "2. Type a commit message and press Commit\n"
        "3. Open the Commits tab to see exactly what changed\n");
    DoCommit(repo, "Add instructions to the README");

    Issue *issue = &repo->issues[repo->issueCount++];
    snprintf(issue->title, TEXT_LEN, "%s", "Add a LICENSE file");
    issue->open = true;
    issue->time = (long long)time(NULL);
    issue->author = owner;
}

/* ------------------------------------------------------------------ */
/* Account actions                                                    */
/* ------------------------------------------------------------------ */
/* Signs in with the username and password typed, or says why not (pausing after 5 wrong tries). */
void DoSignIn(void)
{
    if (signInPauseSeconds > 0) { Toast(TextFormat("Too many wrong attempts - wait %d seconds", (int)signInPauseSeconds + 1)); return; }
    if (!usernameField.text[0] || !passwordField.text[0]) { Toast("Enter your username and password"); return; }

    User *user = FindUserByName(usernameField.text);
    bool ok;
    if (user) ok = CheckPassword(user, passwordField.text);
    else {
        /* do the same slow work anyway, so you can't tell "no such user" from "wrong password" */
        char dummy[65];
        HashPassword(passwordField.text, "00000000000000000000000000000000", dummy);
        ok = false;
    }
    ClearSecrets();

    if (!ok) {
        if (++failedLogins >= 5) {
            failedLogins = 0;
            signInPauseSeconds = 30;
            Toast("Too many wrong attempts - sign-in is paused for 30 seconds");
        } else Toast("Incorrect username or password");
        Unfocus();
        passwordField.active = true;
        return;
    }
    char name[NAME_LEN];
    snprintf(name, NAME_LEN, "%s", user->name);
    bool noCode = !HasRecoveryCode(user);
    int invitations = InvitationCount(user->id);
    SignIn(user->id);
    if (invitations)         Toast(TextFormat("You've been invited to %d repositor%s - see your profile", invitations, invitations == 1 ? "y" : "ies"));
    else if (noCode) Toast("Tip: make a recovery code in Account settings in case you forget your password");
    else             Toast(TextFormat("Welcome back, %s", name));
}

/* Creates an account from the form, signs in, and shows the new recovery code. */
void DoCreateAccount(void)
{
    const char *error = ValidateUsername(usernameField.text);
    if (!error && (int)strlen(passwordField.text) < MIN_PASS) error = TextFormat("Passwords need at least %d characters", MIN_PASS);
    if (!error && strcmp(passwordField.text, confirmPasswordField.text) != 0) error = "The two passwords don't match";
    if (!error && userCount >= MAX_USERS) error = "This computer has reached the maximum number of accounts";
    if (error) { Toast(error); return; }

    User *user = &users[userCount++];
    memset(user, 0, sizeof *user);
    user->id = nextUserId++;
    snprintf(user->name, NAME_LEN, "%.*s", NAME_LEN - 1, usernameField.text);  /* already checked to be short */
    user->created = (long long)time(NULL);
    SetPassword(user, passwordField.text);

    int  userId = user->id;
    char name[NAME_LEN];
    snprintf(name, NAME_LEN, "%s", user->name);
    SignIn(userId);
    int claimed = ClaimOrphans(userId);
    if (withExampleRepo && !claimed) SeedExample(userId);
    RecomputeAuthors();                    /* commits made under this name now count as theirs */
    MarkDirty();
    ShowRecoveryCode(FindUserById(userId), false);
    Toast(claimed ? TextFormat("Welcome, %s - your existing repositories are now yours", name)
                  : TextFormat("Welcome to Mini GitHub, %s!", name));
}

/* "Forgot password": username + recovery code -> choose a new password. */
void DoResetPassword(void)
{
    if (signInPauseSeconds > 0) { Toast(TextFormat("Too many wrong attempts - wait %d seconds", (int)signInPauseSeconds + 1)); return; }
    char normalized[RECOVERY_CODE_LENGTH + 1];
    const char *error = NULL;
    if (!usernameField.text[0])                                       error = "Enter your username";
    else if (NormalizeRecoveryCode(recoveryCodeField.text, normalized) != RECOVERY_CODE_LENGTH)   error = "Recovery codes have 16 letters and numbers, like K7QM-2XWD-9PRT-H4NC";
    else if ((int)strlen(newPasswordField.text) < MIN_PASS)           error = TextFormat("New passwords need at least %d characters", MIN_PASS);
    else if (strcmp(newPasswordField.text, confirmNewPasswordField.text) != 0)      error = "The two new passwords don't match";
    if (error) { Toast(error); return; }

    User *user = FindUserByName(usernameField.text);
    bool ok;
    if (user) ok = CheckRecoveryCode(user, recoveryCodeField.text);
    else {
        char dummy[65];   /* same slow work, so a wrong username looks the same as a wrong code */
        HashPassword(normalized, "00000000000000000000000000000000", dummy);
        ok = false;
    }
    if (!ok) {
        memset(recoveryCodeField.text, 0, sizeof recoveryCodeField.text);
        recoveryCodeField.length = 0;
        if (++failedLogins >= 5) {
            failedLogins = 0;
            signInPauseSeconds = 30;
            Toast("Too many wrong attempts - paused for 30 seconds");
        } else Toast("That username and recovery code don't match");
        Unfocus();
        recoveryCodeField.active = true;
        return;
    }

    SetPassword(user, newPasswordField.text);
    int userId = user->id;
    SignIn(userId);                                /* also wipes the password boxes */
    ShowRecoveryCode(FindUserById(userId), true);  /* the old code is used up: here's a new one */
    Toast("Password changed - you're signed in");
}

/* Changes the password, if the current one typed is right and the new one is acceptable. */
void DoChangePassword(void)
{
    User *user = FindUserById(currentUserId);
    if (!user) return;
    const char *message = NULL;
    if (!CheckPassword(user, currentPasswordField.text))                 message = "Your current password is wrong";
    else if ((int)strlen(newPasswordField.text) < MIN_PASS)      message = TextFormat("New passwords need at least %d characters", MIN_PASS);
    else if (strcmp(newPasswordField.text, confirmNewPasswordField.text) != 0) message = "The two new passwords don't match";
    else {
        SetPassword(user, newPasswordField.text);
        MarkDirty();
        message = "Password updated";
    }
    ClearSecrets();
    Unfocus();
    Toast(message);
}

/* Deletes the signed-in account and its repositories, if the password typed is right. */
void DoDeleteAccount(void)
{
    User *user = FindUserById(currentUserId);
    if (!user) return;
    if (!CheckPassword(user, deletePasswordField.text)) { ClearSecrets(); Toast("Wrong password - your account was not deleted"); return; }
    char name[NAME_LEN];
    snprintf(name, NAME_LEN, "%s", user->name);
    int userId = currentUserId;
    currentUserId = -1;
    DeleteAccount(userId);
    RecomputeAuthors();                    /* their commits now show as by someone outside Mini GitHub */
    if (EqualsIgnoreCase(lastUserName, name)) lastUserName[0] = 0;
    MarkDirty();
    if (userCount) GoToSignIn(); else GoToCreateAccount();
    Toast(TextFormat("The account %s has been deleted", name));
}
