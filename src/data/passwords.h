/*
 * passwords.h - passwords and recovery codes
 *
 * Salted PBKDF2-SHA256 password hashes, random bytes from the operating
 * system, and one-time recovery codes for forgotten passwords. Passwords
 * themselves are never kept.
 */

#ifndef MINIGITHUB_PASSWORDS_H
#define MINIGITHUB_PASSWORDS_H

#include "common.h"
#include "model.h"

/* The value of one hex digit (0 for anything that isn't one). */
int HexDigitValue(char digit);

/* The PBKDF2-SHA256 hash (as 64 hex digits) of a password with a salt given as 32 hex digits. */
void HashPassword(const char *password, const char *saltHex, char outHex[65]);

/* Gives the user a new random salt and stores the hash of password with it. */
void SetPassword(User *user, const char *password);

/* Compares every character (no early exit) so timing gives nothing away. */
bool CheckPassword(User *user, const char *password);
#define RECOVERY_CODE_LENGTH 16
/* Keep only letters and digits, in capitals, so "k7qm 2xwd-..." still works. Returns how many
   characters were kept. */
int NormalizeRecoveryCode(const char *typed, char normalized[RECOVERY_CODE_LENGTH + 1]);

/* Has this user made a recovery code? */
bool HasRecoveryCode(User *user);

/* Makes a new code for user (the old one stops working) and writes the readable "XXXX-XXXX-XXXX-
   XXXX" form into shown. */
void NewRecoveryCode(User *user, char shown[24]);

/* Is this the user's recovery code? Spaces, dashes and capitals don't matter. */
bool CheckRecoveryCode(User *user, const char *typed);

#endif
