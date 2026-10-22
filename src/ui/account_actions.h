/*
 * account_actions.h - what the account forms do
 *
 * Signing in, creating an account, resetting a forgotten password,
 * changing a password and deleting an account.
 */

#ifndef MINIGITHUB_ACCOUNT_ACTIONS_H
#define MINIGITHUB_ACCOUNT_ACTIONS_H

#include "common.h"

/* Signs in with the username and password typed, or says why not (pausing after 5 wrong tries). */
void DoSignIn(void);

/* Creates an account from the form, signs in, and shows the new recovery code. */
void DoCreateAccount(void);

/* "Forgot password": username + recovery code -> choose a new password. */
void DoResetPassword(void);

/* Changes the password, if the current one typed is right and the new one is acceptable. */
void DoChangePassword(void);

/* Deletes the signed-in account and its repositories, if the password typed is right. */
void DoDeleteAccount(void);

#endif
