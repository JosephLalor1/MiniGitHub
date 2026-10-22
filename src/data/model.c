/*
 * model.c - the data model
 *
 * Accounts (User), repositories (Repo) and what they hold - blobs,
 * commits, branches, issues and each person's uncommitted drafts - plus
 * the lists of every user and repository.
 */

#include "model.h"
#include "sha_map.h"

/* ------------------------------------------------------------------ */
/* Data                                                               */
/* ------------------------------------------------------------------ */

User users[MAX_USERS];
int  userCount = 0, nextUserId = 1;
int  currentUserId = -1;      /* id of the signed-in user, -1 = nobody */
char lastUserName[NAME_LEN];  /* pre-fills the sign-in box             */

Repo repos[MAX_REPOS];
int  repoCount = 0;
