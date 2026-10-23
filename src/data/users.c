/*
 * users.c - people and what they may do
 *
 * Finding accounts, people who aren't Mini GitHub users (the authors of
 * pushed commits), who may see or change a repository, invitations, stars,
 * and deleting accounts.
 */

#include "users.h"
#include "drafts.h"
#include "git_commit.h"
#include "model.h"
#include "repo.h"
#include "repo_view.h"
#include "util.h"

/* The account with this id, or NULL. */
User *FindUserById(int id)
{
    for (int i = 0; i < userCount; i++) if (users[i].id == id) return &users[i];
    return NULL;
}
/* The account with this name (ignoring capitals), or NULL. */
User *FindUserByName(const char *name)
{
    for (int i = 0; i < userCount; i++) if (EqualsIgnoreCase(users[i].name, name)) return &users[i];
    return NULL;
}
/* ---------- people who aren't Mini GitHub users ----------
   Commits pushed from elsewhere (a project from GitHub, say) were often made
   by people with no account here. Each of them - told apart by their email -
   gets a number of their own, below -10 so it can't be mixed up with an
   account: author -10 is the first outsider, -11 the next, and so on.
   They're worked out afresh from the commits each time Mini GitHub starts. */

static Outsider *outsiders;
int       outsiderCount, outsiderCapacity;

/* Is this author id someone who isn't a Mini GitHub user? */
bool IsOutsider(int id) { return id <= -OUTSIDER_BASE && -id - OUTSIDER_BASE < outsiderCount; }
/* The name and email of an outsider author, or NULL if id isn't one. */
Outsider *OutsiderOf(int id) { return IsOutsider(id) ? &outsiders[-id - OUTSIDER_BASE] : NULL; }
static int OutsiderIdFor(const char *name, const char *email)
{
    for (int i = 0; i < outsiderCount; i++)
        if (email[0] ? EqualsIgnoreCase(outsiders[i].email, email) : (!outsiders[i].email[0] && strcmp(outsiders[i].name, name) == 0))
            return -(OUTSIDER_BASE + i);
    outsiders = GrowArray(outsiders, &outsiderCapacity, outsiderCount + 1, sizeof(Outsider));
    if (outsiderCount >= outsiderCapacity) return -1;
    snprintf(outsiders[outsiderCount].name, NAME_LEN, "%.*s", NAME_LEN - 1, name[0] ? name : "someone");
    snprintf(outsiders[outsiderCount].email, TEXT_LEN, "%s", email);
    return -(OUTSIDER_BASE + outsiderCount++);
}

/* Deleted accounts show up as "ghost", like on GitHub; outsiders by the name git has for them. */
const char *UserName(int id)
{
    Outsider *outsider = OutsiderOf(id);
    if (outsider) return outsider->name;
    User *user = FindUserById(id);
    return user ? user->name : "ghost";
}
/* The same, saying when it isn't a Mini GitHub account: "Linus Torvalds (not a Mini GitHub user)". */
const char *AuthorLabel(int id)
{
    Outsider *outsider = OutsiderOf(id);
    return outsider ? TextFormat("%s (not a Mini GitHub user)", outsider->name) : UserName(id);
}

/* Whose work is a commit by this git name + email? An account if:
   - the email is the one in its profile, or one it has claimed ("That's me"),
   - the email is the made-up one commits made here get (name@minigithub.local),
   - the email is GitHub's private one for that username (12345+name@users.noreply.github.com),
   - or the name is the account's username.
   Otherwise -1: someone who isn't a Mini GitHub user. */
static int UserIdForAuthor(const char *name, const char *email)
{
    for (int i = 0; i < userCount; i++) {
        User *user = &users[i];
        if (user->email[0] && EqualsIgnoreCase(user->email, email)) return user->id;
        for (int claim = 0; claim < user->claimCount; claim++)
            if (EqualsIgnoreCase(user->claims[claim], email) || (!strchr(user->claims[claim], '@') && EqualsIgnoreCase(user->claims[claim], name))) return user->id;
    }
    for (int i = 0; i < userCount; i++) if (EqualsIgnoreCase(email, TextFormat("%s@minigithub.local", users[i].name))) return users[i].id;
    const char *at = strchr(email, '@');
    if (at && EqualsIgnoreCase(at, "@users.noreply.github.com")) {
        const char *plus = memchr(email, '+', (size_t)(at - email));
        const char *login = plus ? plus + 1 : email;
        char loginName[NAME_LEN];
        snprintf(loginName, sizeof loginName, "%.*s", (int)(at - login), login);
        for (int i = 0; i < userCount; i++) if (EqualsIgnoreCase(users[i].name, loginName)) return users[i].id;
    }
    for (int i = 0; i < userCount; i++) if (EqualsIgnoreCase(users[i].name, name)) return users[i].id;
    return -1;
}

/* Who made a commit, from the author git recorded in it: an account, or an outsider. */
int AuthorOfCommit(Commit *commit)
{
    CommitInfo info;
    if (!commit->raw || !ParseCommit(commit->raw, commit->rawLen, &info)) return commit->author;
    if (EqualsIgnoreCase(info.authorEmail, "ghost@minigithub.local") && commit->author >= 0) return commit->author;  /* very old commits, claimed since */
    int userId = UserIdForAuthor(info.authorName, info.authorEmail);
    return userId >= 0 ? userId : OutsiderIdFor(info.authorName, info.authorEmail);
}

/* Works out every commit's author again - after an account is made or deleted,
   or someone says "That's me" or changes their email. */
void RecomputeAuthors(void)
{
    outsiderCount = 0;
    for (int repoIndex = 0; repoIndex < repoCount; repoIndex++)
        for (int commitIndex = 0; commitIndex < repos[repoIndex].commitCount; commitIndex++) repos[repoIndex].commits[commitIndex].author = AuthorOfCommit(&repos[repoIndex].commits[commitIndex]);
}
/* The email git puts on commits made here: the one set in your profile, or a made-up one. */
const char *UserEmail(int id)
{
    static char madeUp[NAME_LEN + 24];
    User *user = FindUserById(id);
    if (user && user->email[0]) return user->email;
    snprintf(madeUp, sizeof madeUp, "%s@minigithub.local", user ? user->name : "ghost");
    return madeUp;
}

/* Does the signed-in user own this repository? */
bool IsMine(Repo *repo) { return currentUserId >= 0 && repo->owner == currentUserId; }

/* ---------- collaborators ---------- */
/* Has userId accepted an invitation to this repository? */
bool IsCollaborator(Repo *repo, int userId)
{
    for (int i = 0; i < repo->collaboratorCount; i++) if (repo->collaborators[i] == userId) return true;
    return false;
}
/* Is userId invited to this repository (and hasn't answered yet)? */
bool IsInvited(Repo *repo, int userId)
{
    for (int i = 0; i < repo->inviteCount; i++) if (repo->invites[i] == userId) return true;
    return false;
}
/* Owners and collaborators can change a repository; everyone else can only look. */
bool CanEditBy(Repo *repo, int userId) { return userId >= 0 && (repo->owner == userId || IsCollaborator(repo, userId)); }
/* Can the signed-in user change this repository? */
bool CanEdit(Repo *repo)            { return CanEditBy(repo, currentUserId); }
/* Who can see (and so fork, clone, star) a repository: anyone if it's public;
   if it's private, only the owner, collaborators, and people invited to be one. */
bool CanSee(Repo *repo, int userId) { return !repo->isPrivate || CanEditBy(repo, userId) || IsInvited(repo, userId); }

/* Takes back (or declines) userId's invitation. */
void RemoveInvite(Repo *repo, int userId)
{
    for (int i = 0; i < repo->inviteCount; i++)
        if (repo->invites[i] == userId) {
            memmove(&repo->invites[i], &repo->invites[i + 1], (size_t)(repo->inviteCount - i - 1) * sizeof(int));
            memmove(&repo->inviteTime[i], &repo->inviteTime[i + 1], (size_t)(repo->inviteCount - i - 1) * sizeof(long long));
            repo->inviteCount--;
            return;
        }
}
/* Takes someone off a repository: their collaborator place, any invitation,
   and their uncommitted draft. Their commits stay in the history. */
void RemoveMember(Repo *repo, int userId)
{
    for (int i = 0; i < repo->collaboratorCount; i++)
        if (repo->collaborators[i] == userId) {
            memmove(&repo->collaborators[i], &repo->collaborators[i + 1], (size_t)(repo->collaboratorCount - i - 1) * sizeof(int));
            repo->collaboratorCount--;
            break;
        }
    RemoveInvite(repo, userId);
    RemoveDraft(repo, userId);
}

/* Returns NULL if the invitation was sent, or a message saying why not. */
const char *InviteUser(Repo *repo, const char *name)
{
    if (!IsMine(repo)) return "Only the owner can invite people";
    char trimmed[TEXT_LEN];                               /* the name without spaces at either end */
    int start = 0, end = (int)strlen(name);
    while (start < end && name[start] == ' ') start++;
    while (end > start && name[end - 1] == ' ') end--;
    snprintf(trimmed, sizeof trimmed, "%.*s", end - start, name + start);
    if (!trimmed[0]) return "Type the username of the person to invite";
    User *user = FindUserByName(trimmed);
    if (!user)                       return TextFormat("There's no user called \"%.40s\" on this computer", trimmed);
    if (user->id == repo->owner)        return "You already own this repository";
    if (IsCollaborator(repo, user->id))       return TextFormat("%s is already a collaborator", user->name);
    if (IsInvited(repo, user->id))      return TextFormat("%s has already been invited", user->name);
    if (repo->inviteCount + repo->collaboratorCount >= MAX_USERS - 1) return "This repository can't have any more collaborators";
    repo->invites[repo->inviteCount] = user->id;
    repo->inviteTime[repo->inviteCount] = (long long)time(NULL);
    repo->inviteCount++;
    return NULL;
}
/* Makes an invited user a collaborator. */
void AcceptInvite(Repo *repo, int userId)
{
    if (!IsInvited(repo, userId)) return;
    RemoveInvite(repo, userId);
    if (!IsCollaborator(repo, userId) && repo->collaboratorCount < MAX_USERS) repo->collaborators[repo->collaboratorCount++] = userId;
}
/* How many repositories userId has been invited to. */
int InvitationCount(int userId)
{
    int count = 0;
    for (int i = 0; i < repoCount; i++) if (IsInvited(&repos[i], userId)) count++;
    return count;
}

/* Has userId starred this repository? */
bool HasStarred(Repo *repo, int userId)
{
    for (int i = 0; i < repo->starCount; i++) if (repo->starredBy[i] == userId) return true;
    return false;
}
/* Takes away userId's star. */
void RemoveStar(Repo *repo, int userId)
{
    for (int i = 0; i < repo->starCount; i++)
        if (repo->starredBy[i] == userId) { repo->starredBy[i] = repo->starredBy[--repo->starCount]; return; }
}
/* Stars the repository for userId, or takes the star away. */
void ToggleStar(Repo *repo, int userId)
{
    if (HasStarred(repo, userId)) RemoveStar(repo, userId);
    else if (repo->starCount < MAX_USERS) repo->starredBy[repo->starCount++] = userId;
}

/* Does followerId follow userId? */
bool IsFollowing(int followerId, int userId)
{
    User *follower = FindUserById(followerId);
    if (!follower) return false;
    for (int i = 0; i < follower->followingCount; i++) if (follower->following[i] == userId) return true;
    return false;
}
/* Starts followerId following userId, or stops it. (Nobody follows themselves.) */
void ToggleFollow(int followerId, int userId)
{
    User *follower = FindUserById(followerId);
    if (!follower || followerId == userId || !FindUserById(userId)) return;
    for (int i = 0; i < follower->followingCount; i++)
        if (follower->following[i] == userId) {               /* unfollow, keeping the rest in order */
            memmove(&follower->following[i], &follower->following[i + 1], (size_t)(follower->followingCount - i - 1) * sizeof(int));
            follower->followingCount--;
            return;
        }
    if (follower->followingCount < MAX_USERS) follower->following[follower->followingCount++] = userId;
}
/* How many people follow userId. */
int FollowerCount(int userId)
{
    int count = 0;
    for (int i = 0; i < userCount; i++) if (IsFollowing(users[i].id, userId)) count++;
    return count;
}

/* How many files you see: the latest commit plus your own draft if you can
   edit; just the latest commit if you're a visitor. */
int ShownFileCount(Repo *repo)
{
    BuildView(repo, CanEdit(repo) ? currentUserId : -1);
    int count = 0;
    for (int i = 0; i < viewFileCount; i++) if (!ViewDeleted(&viewFiles[i])) count++;
    return count;
}

/* Repos from an old (pre-accounts) save file go to the first account made. */
int ClaimOrphans(int userId)
{
    int claimed = 0;
    for (int i = 0; i < repoCount; i++) {
        Repo *repo = &repos[i];
        if (repo->owner >= 0) continue;
        repo->owner = userId;
        claimed++;
        Draft *draft = GetDraft(repo, -1, false);
        if (draft) draft->userId = userId;
        for (int index = 0; index < repo->commitCount; index++) if (repo->commits[index].author == -1) repo->commits[index].author = userId;
        for (int index = 0; index < repo->issueCount;  index++) if (repo->issues[index].author  < 0) repo->issues[index].author  = userId;
    }
    return claimed;
}
/* How many repositories have no owner (from a save file older than accounts). */
int OrphanCount(void)
{
    int count = 0;
    for (int i = 0; i < repoCount; i++) if (repos[i].owner < 0) count++;
    return count;
}

/* How many repositories userId owns. */
int ReposOwnedBy(int userId)
{
    int count = 0;
    for (int i = 0; i < repoCount; i++) if (repos[i].owner == userId) count++;
    return count;
}

/* Removes the account, all of its repositories, its stars elsewhere and anyone's
   follow of it. Commits and issues it made in other people's repos stay (as "ghost"). */
void DeleteAccount(int userId)
{
    for (int i = repoCount - 1; i >= 0; i--) if (repos[i].owner == userId) DeleteRepo(i);
    for (int i = 0; i < repoCount; i++) {
        RemoveStar(&repos[i], userId);
        RemoveMember(&repos[i], userId);               /* collaborator places and invitations */
    }
    for (int i = 0; i < userCount; i++) if (IsFollowing(users[i].id, userId)) ToggleFollow(users[i].id, userId);
    for (int i = 0; i < userCount; i++)
        if (users[i].id == userId) {
            if (users[i].avatarState == 1) UnloadTexture(users[i].avatarTexture);
            free(users[i].avatar);
            memmove(&users[i], &users[i + 1], (size_t)(userCount - i - 1) * sizeof(User));
            userCount--;
            memset(&users[userCount], 0, sizeof(User));
            break;
        }
}

/* NULL if this would make a good new username, or what's wrong with it. */
const char *ValidateUsername(const char *username)
{
    int length = (int)strlen(username);
    if (length < 1)  return "Choose a username";
    if (length > 39) return "Usernames can be at most 39 characters";
    for (int i = 0; i < length; i++)
        if (!isalnum((unsigned char)username[i]) && username[i] != '-' && username[i] != '_')
            return "Usernames can only use letters, numbers, - and _";
    if (FindUserByName(username)) return "That username is already taken";
    return NULL;
}
