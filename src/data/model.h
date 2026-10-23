/*
 * model.h - the data model
 *
 * Accounts (User), repositories (Repo) and what they hold - blobs,
 * commits, branches, issues and each person's uncommitted drafts - plus
 * the lists of every user and repository.
 */

#ifndef MINIGITHUB_MODEL_H
#define MINIGITHUB_MODEL_H

#include "common.h"
#include "sha_map.h"

/* An account. The password itself is never kept: only a random "salt"
   and the result of hashing salt + password many thousands of times. */
typedef struct {
    int       id;                    /* never reused, even after deletion */
    char      name[NAME_LEN];
    char      salt[33];              /* 16 random bytes as hex            */
    char      hash[65];              /* 32-byte PBKDF2 result as hex      */
    char      recoverySalt[33];      /* recovery code: salt and hash,     */
    char      recoveryHash[65];      /* stored like the password ("" = none) */
    long long created;
    char      bio[TEXT_LEN];
    char      email[TEXT_LEN];       /* put on commits made here, so git shows it ("" = none set) */
    int       claimCount;            /* other emails you've said are you ("That's me"): */
    char      claims[8][TEXT_LEN];   /* commits made with them count as yours            */
    int       followingCount;        /* the people this account follows (their user ids) */
    int       following[MAX_USERS];
    int       theme;                 /* one of THEME_...                  */
    int       avatarColor;           /* 0 = automatic, 1..8 = chosen      */
    char     *avatar;                /* profile picture: PNG as base64 text, or NULL */
    Texture2D avatarTexture;         /* the picture loaded for drawing (not saved)   */
    int       avatarState;           /* 0 not loaded yet, 1 loaded, -1 broken        */
} User;

typedef struct LangCache LangCache;   /* a file's language clues, worked out once (see languages.c) */

/* A blob is one unique version of a file's contents. Identical
   contents are stored only once, just like real git - and with the same
   SHA-1 fingerprint git gives it.
   "Binary" files (pictures, zips, programs...) are stored byte for byte;
   they just can't be shown as text or edited. */
typedef struct { uint8_t sha[20]; char *data; int length; bool binary; LangCache *lang; } Blob;

/* A commit is a snapshot: a list of file paths (in order, e.g. "README.md",
   "src/main.c"), each pointing at a blob. It is also a real git commit:
   "raw" is exactly what git stores for it, and "sha" its fingerprint. */
typedef struct {
    char      id[8];                 /* short id: the first 7 hex digits of sha, e.g. "3fa9c1d" */
    uint8_t   sha[20];
    char      message[TEXT_LEN];     /* the first line of the message */
    long long time;
    int       author;                /* user id */
    int       parents[2];            /* the commit(s) it was built on (index in commits[]):   */
    int       parentCount;           /* 0 = the first commit, 2 = a merge of two lines of work */
    int       fileCount;
    const char **names;              /* sorted paths (shared strings, see InternPath)            */
    int       *blobs;
    uint8_t   *modes;                /* MODE_FILE, MODE_EXEC or MODE_LINK                    */
    char      *raw;                  /* the git commit object: tree, parents, author, message */
    int       rawLen;
    bool      copied;                /* came from the original repository into a fork: */
                                     /* it isn't counted again as someone's contribution */
    bool      statsDone;                      /* the three below are worked out when first needed, */
    int       statFiles, statAdds, statDels;  /* not saved: files changed, lines added / removed     */
    int      *langLines;                      /* lines changed in each language (worked out when needed, not saved) */
} Commit;

/* A file someone is editing. In a draft (below) it also remembers which
   version it started from, so we can spot when someone else changed the
   same file in the meantime. */
typedef struct {
    char name[PATH_LEN]; char *text; int length, capacity; bool binary;
    unsigned version, langVersion;   /* version goes up with every change; langVersion = the version lang was worked out for */
    LangCache *lang;
    bool deleted;                    /* in a draft: "I've deleted this file"               */
    int  baseBlob;                   /* in a draft: blob it started from (-1 = a new file) */
} WorkFile;

/* Each person who can edit a repository has their own draft: just the files
   they've changed and not committed yet. Everything else they see is the
   latest commit - so nobody sees anyone else's half-finished work, and a
   commit only ever contains the changes of the person who made it. */
typedef struct {
    int  userId; int fileCount; WorkFile *files;   /* whose changes, and the files they've changed */
    char branch[NAME_LEN];           /* the branch these changes are for                       */
    bool merging;                    /* a merge is under way: committing makes a merge commit,  */
    uint8_t mergeSha[20];            /* joining in this commit...                               */
    char mergeName[NAME_LEN];        /* ...the tip of this branch                               */
} Draft;

#define MAX_DRAFTS 128               /* per repository: one per person per branch */

/* A branch: a name for a line of work, pointing at its newest commit ("tip").
   Committing on a branch moves its tip forward to the new commit. */
#define MAX_BRANCHES 64

typedef struct { char name[NAME_LEN]; int tip; } Branch;
typedef struct { char title[TEXT_LEN]; bool open; long long time; int author; } Issue;

typedef struct {
    int       owner;                 /* user id, or -1 = not claimed yet */
    char      name[NAME_LEN];
    char      description[TEXT_LEN];
    char      initialBranch[NAME_LEN];  /* the name the first branch gets (while the repository is empty) */
    bool      isPrivate;                /* only the owner, collaborators and people invited can see it */
    int       branchCount;
    Branch    branches[MAX_BRANCHES];
    int       defaultBranch;         /* the default branch (what git clone checks out)   */
    int       currentBranch;         /* the branch being looked at / worked on right now */
    long long created;
    int       starCount;   int      starredBy[MAX_USERS];
    int       collaboratorCount; int      collaborators[MAX_USERS];  /* people invited who accepted   */
    int       inviteCount; int      invites[MAX_USERS];              /* invited, not answered yet     */
    long long inviteTime[MAX_USERS];
    int       draftCount;  Draft    drafts[MAX_DRAFTS];
    int       commitCount, commitCapacity; Commit *commits;   /* oldest first; the last is the latest */
    int       blobCount,   blobCapacity;   Blob   *blobs;
    ShaMap    commitMap, blobMap;    /* fingerprint -> index, for quick lookups */
    unsigned  generation;            /* goes up whenever the commits change */
    int       issueCount;  Issue    issues[MAX_ISSUES];
    /* forks: the original is found by its owner + name (names are unique per owner) */
    bool      isFork;
    int       forkOwner;             /* user id of the original's owner              */
    char      forkName[NAME_LEN];    /* the original's name                           */
    char      forkBase[41];          /* fingerprint of the newest commit of the original */
                                     /* this fork has taken in ("" = the original was empty) */
} Repo;

extern User users[MAX_USERS];
extern int userCount, nextUserId;
extern int currentUserId;            /* id of the signed-in user, -1 = nobody */
extern char lastUserName[NAME_LEN];  /* pre-fills the sign-in box */
extern Repo repos[MAX_REPOS];
extern int repoCount;

#endif
