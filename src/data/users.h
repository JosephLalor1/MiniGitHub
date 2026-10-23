/*
 * users.h - people and what they may do
 *
 * Finding accounts, people who aren't Mini GitHub users (the authors of
 * pushed commits), who may see or change a repository, invitations, stars,
 * and deleting accounts.
 */

#ifndef MINIGITHUB_USERS_H
#define MINIGITHUB_USERS_H

#include "common.h"
#include "model.h"

/* The account with this id, or NULL. */
User *FindUserById(int id);

/* The account with this name (ignoring capitals), or NULL. */
User *FindUserByName(const char *name);
#define OUTSIDER_BASE 10
typedef struct { char name[NAME_LEN]; char email[TEXT_LEN]; } Outsider;
extern int outsiderCount, outsiderCapacity;

/* Is this author id someone who isn't a Mini GitHub user? */
bool IsOutsider(int id);

/* The name and email of an outsider author, or NULL if id isn't one. */
Outsider *OutsiderOf(int id);

/* Deleted accounts show up as "ghost", like on GitHub; outsiders by the name git has for them. */
const char *UserName(int id);

/* The same, saying when it isn't a Mini GitHub account: "Linus Torvalds (not a Mini GitHub user)". */
const char *AuthorLabel(int id);

/* Who made a commit, from the author git recorded in it: an account, or an outsider. */
int AuthorOfCommit(Commit *commit);

/* Works out every commit's author again - after an account is made or deleted, or someone says
   "That's me" or changes their email. */
void RecomputeAuthors(void);

/* The email git puts on commits made here: the one set in your profile, or a made-up one. */
const char *UserEmail(int id);

/* Does the signed-in user own this repository? */
bool IsMine(Repo *repo);

/* Has userId accepted an invitation to this repository? */
bool IsCollaborator(Repo *repo, int userId);

/* Is userId invited to this repository (and hasn't answered yet)? */
bool IsInvited(Repo *repo, int userId);

/* Owners and collaborators can change a repository; everyone else can only look. */
bool CanEditBy(Repo *repo, int userId);

/* Can the signed-in user change this repository? */
bool CanEdit(Repo *repo);

/* Who can see (and so fork, clone, star) a repository: anyone if it's public; if it's private,
   only the owner, collaborators, and people invited to be one. */
bool CanSee(Repo *repo, int userId);

/* Takes back (or declines) userId's invitation. */
void RemoveInvite(Repo *repo, int userId);

/* Takes someone off a repository: their collaborator place, any invitation, and their uncommitted
   draft. Their commits stay in the history. */
void RemoveMember(Repo *repo, int userId);

/* Returns NULL if the invitation was sent, or a message saying why not. */
const char *InviteUser(Repo *repo, const char *name);

/* Makes an invited user a collaborator. */
void AcceptInvite(Repo *repo, int userId);

/* How many repositories userId has been invited to. */
int InvitationCount(int userId);

/* Has userId starred this repository? */
bool HasStarred(Repo *repo, int userId);

/* Takes away userId's star. */
void RemoveStar(Repo *repo, int userId);

/* Stars the repository for userId, or takes the star away. */
void ToggleStar(Repo *repo, int userId);

/* Does followerId follow userId? */
bool IsFollowing(int followerId, int userId);

/* Starts followerId following userId, or stops it. (Nobody follows themselves.) */
void ToggleFollow(int followerId, int userId);

/* How many people follow userId. */
int FollowerCount(int userId);

/* How many files you see: the latest commit plus your own draft if you can edit; just the latest
   commit if you're a visitor. */
int ShownFileCount(Repo *repo);

/* Repos from an old (pre-accounts) save file go to the first account made. */
int ClaimOrphans(int userId);

/* How many repositories have no owner (from a save file older than accounts). */
int OrphanCount(void);

/* How many repositories userId owns. */
int ReposOwnedBy(int userId);

/* Removes the account, all of its repositories, its stars elsewhere and anyone's follow of it.
   Commits and issues it made in other people's repos stay (as "ghost"). */
void DeleteAccount(int userId);

/* NULL if this would make a good new username, or what's wrong with it. */
const char *ValidateUsername(const char *username);

#endif
