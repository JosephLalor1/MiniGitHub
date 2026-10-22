/*
 * screen_sign_in.h - the sign-in screens
 *
 * Sign in, create an account, forgot password, and showing a new recovery
 * code.
 */

#ifndef MINIGITHUB_SCREEN_SIGN_IN_H
#define MINIGITHUB_SCREEN_SIGN_IN_H

#include "common.h"

/* Draws the "Reset your password" screen (username, recovery code, new password). */
void ScreenForgot(void);

/* Shown once after sign-up, after a reset, or when you make a new code. */
void ScreenRecovery(void);

/* Draws the sign-in screen. */
void ScreenSignIn(void);

/* Draws the "Create your account" screen. */
void ScreenCreateAccount(void);

#endif
