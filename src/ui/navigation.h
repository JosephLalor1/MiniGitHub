/*
 * navigation.h - moving between screens
 *
 * Signing in and out, and opening profiles, repositories, commits and
 * days.
 */

#ifndef MINIGITHUB_NAVIGATION_H
#define MINIGITHUB_NAVIGATION_H

#include "common.h"
#include "model.h"

/* Takes the keyboard focus away from every text box and the editor. */
void Unfocus(void);

/* Wipe every password box so passwords don't sit around in memory or on screen, and close every
   "eye" so they start hidden again. */
void ClearSecrets(void);

/* Shows the sign-in screen (with the last username filled in). */
void GoToSignIn(void);

/* Shows the create-account screen. */
void GoToCreateAccount(void);

/* Shows the "Reset your password" screen. */
void GoToForgotPassword(void);

/* Shows a freshly made recovery code (it's only ever shown once). */
void ShowRecoveryCode(User *user, bool afterReset);

/* Goes to your profile page (the home page), or sign-in if nobody is signed in. */
void GoHome(void);

/* Signs in as userId and goes to their home page. */
void SignIn(int userId);

/* Signs out and goes back to the sign-in screen. */
void SignOut(void);

/* Opens the settings screen, with the bio and email boxes filled in. */
void OpenSettings(void);

/* Opens a person's profile page. */
void OpenProfile(int userId);

/* Opens the People screen. */
void OpenPeople(void);

/* Opens the page for one day of userId's contributions. */
void OpenDay(int userId, long long day);

/* Opens a repository on its Code tab. */
void OpenRepo(int repoIndex);

/* Picks which changed file of the open commit to show (-1: none). */
void SelectChange(int changeIndex);

/* Opens one commit of the open repository. */
void OpenCommit(int commitIndex);

#endif
