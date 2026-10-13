/*
 * Mini GitHub
 * -----------
 * A tiny GitHub-style app that runs entirely on your own computer.
 *
 *   - User accounts: sign up, sign in, sign out, change password, delete account.
 *     Passwords are never stored - only a salted PBKDF2-SHA256 hash of them.
 *   - Repositories belong to a user: create, star and delete your own
 *   - Browse everyone's repositories (read-only unless they're yours)
 *   - A built-in text editor for the files in your repositories
 *   - Commits: a snapshot of every file, saved with a message and an author
 *   - Commit history and line-by-line diffs (what was added / removed)
 *   - Issues: anyone can open one; the repo owner or the issue's author can close it
 *   - A contribution graph of the last six months: hover a day to see what you
 *     did, click it to open a page listing every commit and issue from that day
 *
 * Everything is saved automatically to "minigithub.dat" in the folder the
 * program is run from. A save file from before accounts existed is upgraded
 * automatically: the first account created takes over its repositories.
 *
 * Language: C99    Library: raylib 5.x    One file, nothing else needed.
 */

#include "raylib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <time.h>

/* ------------------------------------------------------------------ */
/* Limits                                                             */
/* ------------------------------------------------------------------ */
#define MAX_USERS    32
#define MAX_REPOS    48
#define MAX_FILES    32
#define MAX_COMMITS  200
#define MAX_BLOBS    1024
#define MAX_ISSUES   100
#define NAME_LEN     64
#define TEXT_LEN     160
#define MAX_DIFF     1500
#define SAVE_FILE    "minigithub.dat"

#define MIN_PASS     8       /* shortest password allowed                     */
#define PBKDF2_ITER  60000   /* hashing rounds: makes password guessing slow   */

#define FS 20   /* normal font size                 */
#define LH 24   /* row height in the editor / diffs */

/* ------------------------------------------------------------------ */
/* Colours (GitHub's dark theme)                                       */
/* ------------------------------------------------------------------ */
static const Color UI_BG     = { 13,  17,  23, 255};
static const Color UI_PANEL  = { 22,  27,  34, 255};
static const Color UI_HOVER  = { 30,  36,  44, 255};
static const Color UI_BORDER = { 48,  54,  61, 255};
static const Color UI_TEXT   = {230, 237, 243, 255};
static const Color UI_MUTED  = {125, 133, 144, 255};
static const Color UI_LINK   = { 88, 166, 255, 255};
static const Color UI_BTN    = { 33,  38,  45, 255};
static const Color UI_BLUE   = { 31, 111, 235, 255};
static const Color UI_GREEN  = { 35, 134,  54, 255};
static const Color UI_GREENT = { 63, 185,  80, 255};
static const Color UI_REDT   = {248,  81,  73, 255};
static const Color UI_DANGER = {218,  54,  51, 255};
static const Color UI_YELLOW = {210, 153,  34, 255};
static const Color UI_PURPLE = {137,  87, 229, 255};
static const Color UI_ORANGE = {247, 129, 102, 255};

/* ------------------------------------------------------------------ */
/* Data                                                               */
/* ------------------------------------------------------------------ */

/* An account. The password itself is never kept: only a random "salt"
   and the result of hashing salt + password many thousands of times. */
typedef struct {
    int       id;                    /* never reused, even after deletion */
    char      name[NAME_LEN];
    char      salt[33];              /* 16 random bytes as hex            */
    char      hash[65];              /* 32-byte PBKDF2 result as hex      */
    long long created;
} User;

/* A blob is one unique version of a file's contents. Identical
   contents are stored only once, just like real git. */
typedef struct { uint32_t hash; char *data; } Blob;

/* A commit is a snapshot: a list of file names, each pointing at a blob. */
typedef struct {
    char      id[8];                 /* short hash, e.g. "3fa9c1d" */
    char      message[TEXT_LEN];
    long long time;
    int       author;                /* user id */
    int       fileCount;
    char      names[MAX_FILES][NAME_LEN];
    int       blobs[MAX_FILES];
    bool      statsDone;             /* the three below are worked out when first needed, */
    int       sFiles, sAdds, sDels;  /* not saved: files changed, lines added / removed     */
} Commit;

/* A working file is the version the owner is editing right now. */
typedef struct { char name[NAME_LEN]; char *text; int len, cap; } WorkFile;

typedef struct { char title[TEXT_LEN]; bool open; long long time; int author; } Issue;

typedef struct {
    int       owner;                 /* user id, or -1 = not claimed yet */
    char      name[NAME_LEN];
    char      desc[TEXT_LEN];
    long long created;
    int       starCount;   int      starredBy[MAX_USERS];
    int       fileCount;   WorkFile files[MAX_FILES];
    int       commitCount; Commit   commits[MAX_COMMITS];
    int       blobCount;   Blob     blobs[MAX_BLOBS];
    int       issueCount;  Issue    issues[MAX_ISSUES];
} Repo;

static User users[MAX_USERS];
static int  userCount = 0, nextUserId = 1;
static int  curUser = -1;            /* id of the signed-in user, -1 = nobody */
static char lastUser[NAME_LEN];      /* pre-fills the sign-in box             */

static Repo repos[MAX_REPOS];
static int  repoCount = 0;

/* ------------------------------------------------------------------ */
/* App state                                                          */
/* ------------------------------------------------------------------ */
typedef enum { SCR_LOGIN, SCR_REGISTER, SCR_HOME, SCR_NEW, SCR_REPO, SCR_COMMIT, SCR_ACCOUNT, SCR_DAY } Screen;
typedef enum { TAB_CODE, TAB_COMMITS, TAB_ISSUES } Tab;

typedef struct { char buf[TEXT_LEN]; int len; bool active; bool secret; } TextField;
typedef struct { int cursor; float scrollY, scrollX; bool focused; } EditorState;

static Screen screen = SCR_LOGIN;
static Tab    tab    = TAB_CODE;
static int    curRepo = -1, curFile = -1, curCommit = -1, curChange = -1;

static TextField tfSearch, tfName, tfDesc, tfNewFile, tfCommit, tfIssue, tfUser;
static TextField tfPass    = {.secret = true}, tfPass2    = {.secret = true};
static TextField tfOldPass = {.secret = true}, tfNewPass  = {.secret = true};
static TextField tfNewPass2= {.secret = true}, tfDelPass  = {.secret = true};
static EditorState ed;
static bool  optReadme = true, optExample = true;
static int   homeView;                /* 0 = your repositories, 1 = everyone's */
static float scHome, scFiles, scCommits, scIssues, scChanges, scDiff, scDay, scDayRepos;
static long long curDay;              /* midnight at the start of the day being viewed      */
static bool  fromDay;                 /* commit page was opened from a day page: go back there */
static int   failedLogins;
static float lockT;                   /* sign-in paused after too many wrong passwords */

static int   W, H;
static bool  clickUsed;       /* stops one click triggering two things */
static int   wantCursor;
static bool  dirty;           /* something changed and needs saving    */
static char  toastMsg[TEXT_LEN];
static float toastT;
static int   armedId;         /* for "click again to confirm" buttons  */
static float armedT;

/* ------------------------------------------------------------------ */
/* Small helpers                                                      */
/* ------------------------------------------------------------------ */
#define FNV_START 2166136261u
static uint32_t Fnv(const char *s, int n, uint32_t h)
{
    for (int i = 0; i < n; i++) { h ^= (unsigned char)s[i]; h *= 16777619u; }
    return h;
}

static void MarkDirty(void) { dirty = true; }
static void Toast(const char *s) { snprintf(toastMsg, sizeof toastMsg, "%s", s); toastT = 3.0f; }

static bool KeyRep(int k) { return IsKeyPressed(k) || IsKeyPressedRepeat(k); }
static bool Hover(Rectangle r) { return CheckCollisionPointRec(GetMousePosition(), r); }
static bool Clicked(Rectangle r)
{
    if (!clickUsed && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && Hover(r)) { clickUsed = true; return true; }
    return false;
}

static char sliceBuf[1024];
static const char *Slice(const char *s, int n)
{
    if (n < 0) n = 0;
    if (n > 1023) n = 1023;
    memcpy(sliceBuf, s, (size_t)n);
    sliceBuf[n] = 0;
    return sliceBuf;
}
/* Width in pixels of the first n characters (where the cursor would sit). */
static int MeasureN(const char *s, int n, int fs)
{
    if (n <= 0) return 0;
    return MeasureText(Slice(s, n), fs) + fs / 10;
}
static void DrawBold(const char *t, int x, int y, int fs, Color c)
{
    DrawText(t, x, y, fs, c);
    DrawText(t, x + 1, y, fs, c);
}
/* Draw text, cutting it short with "..." if it is too wide. */
static void DrawFit(const char *s, int x, int y, int fs, int maxW, Color c)
{
    if (MeasureText(s, fs) <= maxW) { DrawText(s, x, y, fs, c); return; }
    char b[256];
    int n = (int)strlen(s);
    if (n > 240) n = 240;
    while (n > 0) {
        snprintf(b, sizeof b, "%.*s...", n, s);
        if (MeasureText(b, fs) <= maxW) break;
        n--;
    }
    DrawText(n > 0 ? b : "...", x, y, fs, c);
}

static bool ContainsCI(const char *hay, const char *needle)
{
    if (!needle[0]) return true;
    for (; *hay; hay++) {
        int i = 0;
        while (needle[i] && hay[i] &&
               tolower((unsigned char)hay[i]) == tolower((unsigned char)needle[i])) i++;
        if (!needle[i]) return true;
    }
    return false;
}

/* Keep letters, digits, - _ . ; turn spaces into dashes. */
static void SanitizeName(const char *in, char *out)
{
    int j = 0;
    for (int i = 0; in[i] && j < NAME_LEN - 1; i++) {
        char c = in[i];
        if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.') out[j++] = c;
        else if (c == ' ') out[j++] = '-';
    }
    out[j] = 0;
}

static const char *TimeAgo(long long t)
{
    long long d = (long long)time(NULL) - t;
    if (d < 0) d = 0;
    if (d < 60)         return "just now";
    if (d < 3600)       return TextFormat("%d minute%s ago", (int)(d / 60),    d / 60    == 1 ? "" : "s");
    if (d < 86400)      return TextFormat("%d hour%s ago",   (int)(d / 3600),  d / 3600  == 1 ? "" : "s");
    if (d < 86400 * 30) return TextFormat("%d day%s ago",    (int)(d / 86400), d / 86400 == 1 ? "" : "s");
    static char b[32];
    time_t tt = (time_t)t;
    strftime(b, sizeof b, "on %d %b %Y", localtime(&tt));
    return b;
}

/* ------------------------------------------------------------------ */
/* Working files                                                      */
/* ------------------------------------------------------------------ */
static void WF_Set(WorkFile *f, const char *text)
{
    int n = (int)strlen(text);
    if (f->cap < n + 1) { f->cap = n + 64; f->text = realloc(f->text, (size_t)f->cap); }
    memcpy(f->text, text, (size_t)n + 1);
    f->len = n;
}
static void WF_Insert(WorkFile *f, int at, const char *s, int n)
{
    if (f->len + n + 1 > f->cap) { f->cap = (f->len + n + 1) * 2; f->text = realloc(f->text, (size_t)f->cap); }
    memmove(f->text + at + n, f->text + at, (size_t)(f->len - at + 1));
    memcpy(f->text + at, s, (size_t)n);
    f->len += n;
}
static void WF_Delete(WorkFile *f, int at, int n)
{
    memmove(f->text + at, f->text + at + n, (size_t)(f->len - at - n + 1));
    f->len -= n;
}
static void WF_Free(WorkFile *f) { free(f->text); f->text = NULL; f->len = f->cap = 0; }

/* ------------------------------------------------------------------ */
/* Repositories, blobs and commits                                    */
/* ------------------------------------------------------------------ */
static Repo *NewRepo(int owner, const char *name, const char *desc)
{
    if (repoCount >= MAX_REPOS) return NULL;
    Repo *r = &repos[repoCount++];
    memset(r, 0, sizeof *r);
    r->owner = owner;
    snprintf(r->name, NAME_LEN, "%s", name);
    snprintf(r->desc, TEXT_LEN, "%s", desc);
    r->created = (long long)time(NULL);
    return r;
}

static int FindFile(Repo *r, const char *name)
{
    for (int i = 0; i < r->fileCount; i++) if (strcmp(r->files[i].name, name) == 0) return i;
    return -1;
}

static int AddFile(Repo *r, const char *name, const char *text)
{
    if (r->fileCount >= MAX_FILES || FindFile(r, name) >= 0) return -1;
    WorkFile *f = &r->files[r->fileCount];
    memset(f, 0, sizeof *f);
    snprintf(f->name, NAME_LEN, "%s", name);
    WF_Set(f, text);
    return r->fileCount++;
}

static void RemoveFile(Repo *r, int i)
{
    WF_Free(&r->files[i]);
    memmove(&r->files[i], &r->files[i + 1], (size_t)(r->fileCount - i - 1) * sizeof(WorkFile));
    r->fileCount--;
    memset(&r->files[r->fileCount], 0, sizeof(WorkFile));
}

static void FreeRepo(Repo *r)
{
    for (int i = 0; i < r->fileCount; i++) WF_Free(&r->files[i]);
    for (int i = 0; i < r->blobCount; i++) free(r->blobs[i].data);
}

static void DeleteRepo(int i)
{
    FreeRepo(&repos[i]);
    memmove(&repos[i], &repos[i + 1], (size_t)(repoCount - i - 1) * sizeof(Repo));
    repoCount--;
    memset(&repos[repoCount], 0, sizeof(Repo));
}

/* Store contents once; returns the blob's index (or -1 if full). */
static int InternBlob(Repo *r, const char *data, int len)
{
    uint32_t h = Fnv(data, len, FNV_START);
    for (int i = 0; i < r->blobCount; i++)
        if (r->blobs[i].hash == h && (int)strlen(r->blobs[i].data) == len &&
            memcmp(r->blobs[i].data, data, (size_t)len) == 0) return i;
    if (r->blobCount >= MAX_BLOBS) return -1;
    Blob *b = &r->blobs[r->blobCount];
    b->hash = h;
    b->data = malloc((size_t)len + 1);
    memcpy(b->data, data, (size_t)len);
    b->data[len] = 0;
    return r->blobCount++;
}

static const char *BlobText(Repo *r, int b) { return b >= 0 ? r->blobs[b].data : ""; }
static Commit *Head(Repo *r) { return r->commitCount ? &r->commits[r->commitCount - 1] : NULL; }
static long long LastActivity(Repo *r) { return r->commitCount ? Head(r)->time : r->created; }

static int FindInCommit(Commit *c, const char *name)
{
    for (int i = 0; i < c->fileCount; i++) if (strcmp(c->names[i], name) == 0) return i;
    return -1;
}

/* 'A' = new since last commit, 'M' = modified, 0 = unchanged */
static char FileStatus(Repo *r, int i)
{
    Commit *h = Head(r);
    if (!h) return 'A';
    int k = FindInCommit(h, r->files[i].name);
    if (k < 0) return 'A';
    return strcmp(r->blobs[h->blobs[k]].data, r->files[i].text) ? 'M' : 0;
}

static int ChangeCount(Repo *r)
{
    int n = 0;
    for (int i = 0; i < r->fileCount; i++) if (FileStatus(r, i)) n++;
    Commit *h = Head(r);
    if (h) for (int k = 0; k < h->fileCount; k++) if (FindFile(r, h->names[k]) < 0) n++;
    return n;
}

static int OpenIssues(Repo *r)
{
    int n = 0;
    for (int i = 0; i < r->issueCount; i++) if (r->issues[i].open) n++;
    return n;
}

/* Returns NULL on success, or a message explaining what went wrong. */
static const char *DoCommit(Repo *r, const char *msg)
{
    if (!msg[0])                       return "Write a commit message first";
    if (ChangeCount(r) == 0)           return "Nothing to commit - no changes since the last commit";
    if (r->commitCount >= MAX_COMMITS) return "This repository has reached its commit limit";

    Commit c;
    memset(&c, 0, sizeof c);
    uint32_t h = FNV_START;
    for (int i = 0; i < r->fileCount; i++) {
        int b = InternBlob(r, r->files[i].text, r->files[i].len);
        if (b < 0) return "This repository is out of storage space";
        snprintf(c.names[i], NAME_LEN, "%s", r->files[i].name);
        c.blobs[i] = b;
        h = Fnv(c.names[i], (int)strlen(c.names[i]), h);
        h ^= r->blobs[b].hash; h *= 16777619u;
    }
    c.fileCount = r->fileCount;
    c.author = curUser;
    snprintf(c.message, TEXT_LEN, "%s", msg);
    c.time = (long long)time(NULL);
    h = Fnv(msg, (int)strlen(msg), h);
    h = Fnv((const char *)&c.author, (int)sizeof c.author, h);
    h = Fnv((const char *)&c.time, (int)sizeof c.time, h);
    if (r->commitCount) h = Fnv(Head(r)->id, 7, h);
    snprintf(c.id, sizeof c.id, "%07x", (unsigned)(h & 0xFFFFFFFu));
    r->commits[r->commitCount++] = c;
    return NULL;
}

/* Replace the working files with the files from an old commit. */
static void RestoreCommit(Repo *r, int ci)
{
    for (int i = 0; i < r->fileCount; i++) WF_Free(&r->files[i]);
    memset(r->files, 0, sizeof r->files);
    r->fileCount = 0;
    Commit *c = &r->commits[ci];
    for (int k = 0; k < c->fileCount; k++) AddFile(r, c->names[k], r->blobs[c->blobs[k]].data);
}

/* ------------------------------------------------------------------ */
/* Users                                                              */
/* ------------------------------------------------------------------ */
static bool EqualsCI(const char *a, const char *b)
{
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
        a++; b++;
    }
    return *a == *b;
}

static User *FindUserById(int id)
{
    for (int i = 0; i < userCount; i++) if (users[i].id == id) return &users[i];
    return NULL;
}
static User *FindUserByName(const char *name)
{
    for (int i = 0; i < userCount; i++) if (EqualsCI(users[i].name, name)) return &users[i];
    return NULL;
}
/* Deleted accounts show up as "ghost", like on GitHub. */
static const char *UserName(int id) { User *u = FindUserById(id); return u ? u->name : "ghost"; }

static bool IsMine(Repo *r) { return curUser >= 0 && r->owner == curUser; }

static bool HasStarred(Repo *r, int uid)
{
    for (int i = 0; i < r->starCount; i++) if (r->starredBy[i] == uid) return true;
    return false;
}
static void RemoveStar(Repo *r, int uid)
{
    for (int i = 0; i < r->starCount; i++)
        if (r->starredBy[i] == uid) { r->starredBy[i] = r->starredBy[--r->starCount]; return; }
}
static void ToggleStar(Repo *r, int uid)
{
    if (HasStarred(r, uid)) RemoveStar(r, uid);
    else if (r->starCount < MAX_USERS) r->starredBy[r->starCount++] = uid;
}

/* For someone else's repo you see their latest commit, not their unsaved edits. */
static int ShownCount(Repo *r)
{
    if (IsMine(r)) return r->fileCount;
    Commit *h = Head(r);
    return h ? h->fileCount : 0;
}

/* Repos from an old (pre-accounts) save file go to the first account made. */
static int ClaimOrphans(int uid)
{
    int n = 0;
    for (int i = 0; i < repoCount; i++) {
        Repo *r = &repos[i];
        if (r->owner >= 0) continue;
        r->owner = uid;
        n++;
        for (int k = 0; k < r->commitCount; k++) if (r->commits[k].author < 0) r->commits[k].author = uid;
        for (int k = 0; k < r->issueCount;  k++) if (r->issues[k].author  < 0) r->issues[k].author  = uid;
    }
    return n;
}
static int OrphanCount(void)
{
    int n = 0;
    for (int i = 0; i < repoCount; i++) if (repos[i].owner < 0) n++;
    return n;
}

static int ReposOwnedBy(int uid)
{
    int n = 0;
    for (int i = 0; i < repoCount; i++) if (repos[i].owner == uid) n++;
    return n;
}

/* Removes the account, all of its repositories, and its stars elsewhere.
   Commits and issues it made in other people's repos stay (as "ghost"). */
static void DeleteAccount(int uid)
{
    for (int i = repoCount - 1; i >= 0; i--) if (repos[i].owner == uid) DeleteRepo(i);
    for (int i = 0; i < repoCount; i++) RemoveStar(&repos[i], uid);
    for (int i = 0; i < userCount; i++)
        if (users[i].id == uid) {
            memmove(&users[i], &users[i + 1], (size_t)(userCount - i - 1) * sizeof(User));
            userCount--;
            memset(&users[userCount], 0, sizeof(User));
            break;
        }
}

static const char *ValidateUsername(const char *u)
{
    int n = (int)strlen(u);
    if (n < 1)  return "Choose a username";
    if (n > 39) return "Usernames can be at most 39 characters";
    for (int i = 0; i < n; i++)
        if (!isalnum((unsigned char)u[i]) && u[i] != '-' && u[i] != '_')
            return "Usernames can only use letters, numbers, - and _";
    if (FindUserByName(u)) return "That username is already taken";
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Password hashing                                                   */
/*                                                                    */
/* SHA-256 turns any input into a 32-byte "fingerprint" that can't be */
/* reversed. PBKDF2 repeats it tens of thousands of times with a      */
/* random salt, so even a stolen save file is slow to crack and two   */
/* people with the same password get different hashes.               */
/* ------------------------------------------------------------------ */
typedef struct { uint32_t h[8]; uint8_t buf[64]; uint64_t len; int n; } Sha256;

static const uint32_t K256[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};
#define ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

static void ShaBlock(Sha256 *s, const uint8_t *p)
{
    uint32_t w[64];
    for (int i = 0; i < 16; i++)
        w[i] = (uint32_t)p[4*i] << 24 | (uint32_t)p[4*i+1] << 16 | (uint32_t)p[4*i+2] << 8 | p[4*i+3];
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = ROR(w[i-15], 7) ^ ROR(w[i-15], 18) ^ (w[i-15] >> 3);
        uint32_t s1 = ROR(w[i-2], 17) ^ ROR(w[i-2], 19) ^ (w[i-2] >> 10);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    uint32_t a = s->h[0], b = s->h[1], c = s->h[2], d = s->h[3];
    uint32_t e = s->h[4], f = s->h[5], g = s->h[6], h = s->h[7];
    for (int i = 0; i < 64; i++) {
        uint32_t t1 = h + (ROR(e, 6) ^ ROR(e, 11) ^ ROR(e, 25)) + ((e & f) ^ (~e & g)) + K256[i] + w[i];
        uint32_t t2 = (ROR(a, 2) ^ ROR(a, 13) ^ ROR(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    s->h[0] += a; s->h[1] += b; s->h[2] += c; s->h[3] += d;
    s->h[4] += e; s->h[5] += f; s->h[6] += g; s->h[7] += h;
}
static void ShaInit(Sha256 *s)
{
    static const uint32_t iv[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    memcpy(s->h, iv, sizeof iv);
    s->len = 0;
    s->n = 0;
}
static void ShaUpdate(Sha256 *s, const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    s->len += len;
    while (len--) {
        s->buf[s->n++] = *p++;
        if (s->n == 64) { ShaBlock(s, s->buf); s->n = 0; }
    }
}
static void ShaFinal(Sha256 *s, uint8_t out[32])
{
    uint64_t bits = s->len * 8;
    uint8_t one = 0x80, zero = 0, lenBytes[8];
    ShaUpdate(s, &one, 1);
    while (s->n != 56) ShaUpdate(s, &zero, 1);
    for (int i = 0; i < 8; i++) lenBytes[i] = (uint8_t)(bits >> (56 - 8 * i));
    ShaUpdate(s, lenBytes, 8);
    for (int i = 0; i < 8; i++) {
        out[4*i]   = (uint8_t)(s->h[i] >> 24); out[4*i+1] = (uint8_t)(s->h[i] >> 16);
        out[4*i+2] = (uint8_t)(s->h[i] >> 8);  out[4*i+3] = (uint8_t)(s->h[i]);
    }
}

static void HmacSha256(const uint8_t *key, size_t klen, const uint8_t *msg, size_t mlen, uint8_t out[32])
{
    uint8_t k[64] = {0}, ipad[64], opad[64], inner[32];
    Sha256 s;
    if (klen > 64) { ShaInit(&s); ShaUpdate(&s, key, klen); ShaFinal(&s, k); }
    else memcpy(k, key, klen);
    for (int i = 0; i < 64; i++) { ipad[i] = k[i] ^ 0x36; opad[i] = k[i] ^ 0x5c; }
    ShaInit(&s); ShaUpdate(&s, ipad, 64); ShaUpdate(&s, msg, mlen); ShaFinal(&s, inner);
    ShaInit(&s); ShaUpdate(&s, opad, 64); ShaUpdate(&s, inner, 32); ShaFinal(&s, out);
}

/* Standard PBKDF2-HMAC-SHA256, producing one 32-byte block. */
static void Pbkdf2(const char *pass, const uint8_t *salt, int saltLen, int iter, uint8_t out[32])
{
    size_t plen = strlen(pass);
    uint8_t msg[64 + 4], u[32], next[32];
    memcpy(msg, salt, (size_t)saltLen);
    msg[saltLen] = 0; msg[saltLen + 1] = 0; msg[saltLen + 2] = 0; msg[saltLen + 3] = 1;
    HmacSha256((const uint8_t *)pass, plen, msg, (size_t)saltLen + 4, u);
    memcpy(out, u, 32);
    for (int i = 1; i < iter; i++) {
        HmacSha256((const uint8_t *)pass, plen, u, 32, next);
        memcpy(u, next, 32);
        for (int j = 0; j < 32; j++) out[j] ^= u[j];
    }
}

static void ToHex(const uint8_t *in, int n, char *out)
{
    static const char digits[] = "0123456789abcdef";
    for (int i = 0; i < n; i++) { out[2*i] = digits[in[i] >> 4]; out[2*i+1] = digits[in[i] & 15]; }
    out[2 * n] = 0;
}
static int HexVal(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}
static void FromHex(const char *in, uint8_t *out, int n)
{
    for (int i = 0; i < n; i++) out[i] = (uint8_t)(HexVal(in[2*i]) << 4 | HexVal(in[2*i+1]));
}

/* Random bytes for salts: the operating system's random source when there
   is one (Linux / macOS), otherwise a mix of clocks and raylib's generator. */
static void RandomBytes(uint8_t *out, int n)
{
    FILE *f = fopen("/dev/urandom", "rb");
    if (f) {
        size_t got = fread(out, 1, (size_t)n, f);
        fclose(f);
        if ((int)got == n) return;
    }
    Sha256 s;
    uint8_t d[32];
    long long t = (long long)time(NULL);
    clock_t c = clock();
    double g = GetTime();
    void *addr = &s;
    ShaInit(&s);
    ShaUpdate(&s, &t, sizeof t); ShaUpdate(&s, &c, sizeof c);
    ShaUpdate(&s, &g, sizeof g); ShaUpdate(&s, &addr, sizeof addr);
    for (int i = 0; i < 16; i++) { int v = GetRandomValue(0, 0x7fffffff); ShaUpdate(&s, &v, sizeof v); }
    ShaFinal(&s, d);
    for (int i = 0; i < n; i++) out[i] = d[i % 32] ^ (uint8_t)GetRandomValue(0, 255);
}

static void HashPassword(const char *pass, const char *saltHex, char outHex[65])
{
    uint8_t salt[16], dk[32];
    FromHex(saltHex, salt, 16);
    Pbkdf2(pass, salt, 16, PBKDF2_ITER, dk);
    ToHex(dk, 32, outHex);
}
static void SetPassword(User *u, const char *pass)
{
    uint8_t salt[16];
    RandomBytes(salt, 16);
    ToHex(salt, 16, u->salt);
    HashPassword(pass, u->salt, u->hash);
}
/* Compares every character (no early exit) so timing gives nothing away. */
static bool CheckPassword(User *u, const char *pass)
{
    char h[65];
    HashPassword(pass, u->salt, h);
    unsigned diff = 0;
    for (int i = 0; i < 64; i++) diff |= (unsigned)(h[i] ^ u->hash[i]);
    return diff == 0;
}

/* ------------------------------------------------------------------ */
/* Diff: which lines were added, removed or kept                       */
/* (longest common subsequence of lines)                               */
/* ------------------------------------------------------------------ */
typedef struct { const char *s; int n; } LineRef;
typedef struct { char kind; int a, b; } DiffOp;   /* kind: ' ', '+', '-' */

static LineRef dA[MAX_DIFF], dB[MAX_DIFF];
static DiffOp  dOps[MAX_DIFF * 2];
static int     dOpCount;

static int SplitLines(const char *t, LineRef *out, int max)
{
    int c = 0;
    const char *p = t;
    while (*p && c < max) {
        const char *e = strchr(p, '\n');
        out[c].s = p;
        out[c].n = e ? (int)(e - p) : (int)strlen(p);
        c++;
        if (!e) break;
        p = e + 1;
    }
    return c;
}
static bool SameLine(LineRef x, LineRef y) { return x.n == y.n && memcmp(x.s, y.s, (size_t)x.n) == 0; }

static void ComputeDiff(const char *oldT, const char *newT, int *adds, int *dels)
{
    int n = SplitLines(oldT, dA, MAX_DIFF), m = SplitLines(newT, dB, MAX_DIFF);
    int w = m + 1, ad = 0, de = 0, i = 0, j = 0;
    int *L = calloc((size_t)(n + 1) * (size_t)w, sizeof(int));
    dOpCount = 0;
    if (L) {
        for (int x = n - 1; x >= 0; x--)
            for (int y = m - 1; y >= 0; y--) {
                if (SameLine(dA[x], dB[y])) L[x * w + y] = L[(x + 1) * w + y + 1] + 1;
                else {
                    int down = L[(x + 1) * w + y], right = L[x * w + y + 1];
                    L[x * w + y] = down > right ? down : right;
                }
            }
        while (i < n && j < m) {
            if (SameLine(dA[i], dB[j]))                      { dOps[dOpCount++] = (DiffOp){' ', i, j};  i++; j++; }
            else if (L[(i + 1) * w + j] >= L[i * w + j + 1]) { dOps[dOpCount++] = (DiffOp){'-', i, -1}; i++; de++; }
            else                                             { dOps[dOpCount++] = (DiffOp){'+', -1, j}; j++; ad++; }
        }
        free(L);
    }
    while (i < n) { dOps[dOpCount++] = (DiffOp){'-', i, -1}; i++; de++; }
    while (j < m) { dOps[dOpCount++] = (DiffOp){'+', -1, j}; j++; ad++; }
    if (adds) *adds = ad;
    if (dels) *dels = de;
}

/* The list of files a commit changed, compared with the commit before it. */
typedef struct { char name[NAME_LEN]; int oldB, newB; char kind; int adds, dels; } Change;
static Change changes[MAX_FILES * 2];
static int    changeCount, totalAdds, totalDels;

static void BuildChanges(Repo *r, int ci)
{
    changeCount = totalAdds = totalDels = 0;
    Commit *c = &r->commits[ci];
    Commit *p = ci > 0 ? &r->commits[ci - 1] : NULL;

    for (int k = 0; k < c->fileCount; k++) {
        int pk = p ? FindInCommit(p, c->names[k]) : -1;
        int ob = pk >= 0 ? p->blobs[pk] : -1;
        if (ob == c->blobs[k]) continue;                 /* same blob = unchanged */
        Change *ch = &changes[changeCount++];
        snprintf(ch->name, NAME_LEN, "%s", c->names[k]);
        ch->oldB = ob; ch->newB = c->blobs[k]; ch->kind = ob < 0 ? 'A' : 'M';
    }
    if (p) for (int k = 0; k < p->fileCount; k++) {
        if (FindInCommit(c, p->names[k]) >= 0) continue;
        Change *ch = &changes[changeCount++];
        snprintf(ch->name, NAME_LEN, "%s", p->names[k]);
        ch->oldB = p->blobs[k]; ch->newB = -1; ch->kind = 'D';
    }
    for (int k = 0; k < changeCount; k++) {
        ComputeDiff(BlobText(r, changes[k].oldB), BlobText(r, changes[k].newB), &changes[k].adds, &changes[k].dels);
        totalAdds += changes[k].adds;
        totalDels += changes[k].dels;
    }
}

/* ------------------------------------------------------------------ */
/* Saving and loading                                                 */
/* Format: numbers on their own line; text as  <length>:<bytes>\n      */
/* ------------------------------------------------------------------ */
static void WS(FILE *f, const char *s) { int n = (int)strlen(s); fprintf(f, "%d:", n); fwrite(s, 1, (size_t)n, f); fputc('\n', f); }
static void WI(FILE *f, long long v)   { fprintf(f, "%lld\n", v); }

static int loadErr;
static char *RS(FILE *f)
{
    int n;
    if (fscanf(f, " %d:", &n) != 1 || n < 0 || n > 50000000) { loadErr = 1; return NULL; }
    char *s = malloc((size_t)n + 1);
    if (!s || fread(s, 1, (size_t)n, f) != (size_t)n) { free(s); loadErr = 1; return NULL; }
    s[n] = 0;
    fgetc(f);
    return s;
}
static void RSInto(FILE *f, char *dst, int cap)
{
    char *s = RS(f);
    snprintf(dst, (size_t)cap, "%s", s ? s : "");
    free(s);
}
static long long RI(FILE *f)
{
    long long v = 0;
    if (fscanf(f, " %lld", &v) != 1) loadErr = 1;
    return v;
}

static void Save(void)
{
    FILE *f = fopen(SAVE_FILE ".tmp", "wb");
    if (!f) { Toast("Could not save - is the folder read-only?"); return; }
    fprintf(f, "MINIGITHUB2\n");
    WI(f, nextUserId);
    WS(f, lastUser);
    WI(f, userCount);
    for (int i = 0; i < userCount; i++) {
        User *u = &users[i];
        WI(f, u->id); WS(f, u->name); WS(f, u->salt); WS(f, u->hash); WI(f, u->created);
    }
    WI(f, repoCount);
    for (int i = 0; i < repoCount; i++) {
        Repo *r = &repos[i];
        WI(f, r->owner); WS(f, r->name); WS(f, r->desc); WI(f, r->created);
        WI(f, r->starCount);
        for (int k = 0; k < r->starCount; k++) WI(f, r->starredBy[k]);
        WI(f, r->fileCount);
        for (int k = 0; k < r->fileCount; k++) { WS(f, r->files[k].name); WS(f, r->files[k].text); }
        WI(f, r->blobCount);
        for (int k = 0; k < r->blobCount; k++) WS(f, r->blobs[k].data);
        WI(f, r->commitCount);
        for (int k = 0; k < r->commitCount; k++) {
            Commit *c = &r->commits[k];
            WS(f, c->id); WS(f, c->message); WI(f, c->time); WI(f, c->author); WI(f, c->fileCount);
            for (int j = 0; j < c->fileCount; j++) { WS(f, c->names[j]); WI(f, c->blobs[j]); }
        }
        WI(f, r->issueCount);
        for (int k = 0; k < r->issueCount; k++) {
            Issue *is = &r->issues[k];
            WS(f, is->title); WI(f, is->open); WI(f, is->time); WI(f, is->author);
        }
    }
    fclose(f);
    remove(SAVE_FILE);
    rename(SAVE_FILE ".tmp", SAVE_FILE);
}

/* Reads version 2 (with accounts) and version 1 (before accounts). */
static bool Load(void)
{
    FILE *f = fopen(SAVE_FILE, "rb");
    if (!f) return false;
    loadErr = 0;
    char magic[32] = {0};
    int ver = 0;
    if (fscanf(f, "%31s", magic) == 1) {
        if (strcmp(magic, "MINIGITHUB1") == 0) ver = 1;
        if (strcmp(magic, "MINIGITHUB2") == 0) ver = 2;
    }
    if (!ver) loadErr = 1;

    if (ver == 2) {
        nextUserId = (int)RI(f);
        RSInto(f, lastUser, NAME_LEN);
        int uc = (int)RI(f);
        if (uc < 0 || uc > MAX_USERS) loadErr = 1;
        for (int i = 0; i < uc && !loadErr; i++) {
            User *u = &users[userCount++];
            memset(u, 0, sizeof *u);
            u->id = (int)RI(f);
            RSInto(f, u->name, NAME_LEN);
            RSInto(f, u->salt, (int)sizeof u->salt);
            RSInto(f, u->hash, (int)sizeof u->hash);
            u->created = RI(f);
            if (u->id <= 0 || strlen(u->salt) != 32 || strlen(u->hash) != 64) loadErr = 1;
            if (u->id >= nextUserId) nextUserId = u->id + 1;
        }
    }

    int n = loadErr ? 0 : (int)RI(f);
    if (n < 0 || n > MAX_REPOS) loadErr = 1;

    for (int i = 0; i < n && !loadErr; i++) {
        Repo *r = &repos[repoCount++];
        memset(r, 0, sizeof *r);
        r->owner = ver == 2 ? (int)RI(f) : -1;
        RSInto(f, r->name, NAME_LEN);
        RSInto(f, r->desc, TEXT_LEN);
        if (ver == 1) RI(f);                    /* old single-user "starred" flag: dropped */
        r->created = RI(f);
        if (ver == 2) {
            int sc = (int)RI(f);
            if (sc < 0 || sc > MAX_USERS) { loadErr = 1; break; }
            for (int k = 0; k < sc; k++) r->starredBy[r->starCount++] = (int)RI(f);
        }

        int fc = (int)RI(f);
        if (fc < 0 || fc > MAX_FILES) { loadErr = 1; break; }
        for (int k = 0; k < fc && !loadErr; k++) {
            char nm[NAME_LEN];
            RSInto(f, nm, NAME_LEN);
            char *tx = RS(f);
            if (tx) { AddFile(r, nm, tx); free(tx); }
        }

        int bc = (int)RI(f);
        if (bc < 0 || bc > MAX_BLOBS) { loadErr = 1; break; }
        for (int k = 0; k < bc && !loadErr; k++) {
            char *d = RS(f);
            if (!d) break;
            r->blobs[r->blobCount].data = d;
            r->blobs[r->blobCount].hash = Fnv(d, (int)strlen(d), FNV_START);
            r->blobCount++;
        }

        int cc = (int)RI(f);
        if (cc < 0 || cc > MAX_COMMITS) { loadErr = 1; break; }
        for (int k = 0; k < cc && !loadErr; k++) {
            Commit *c = &r->commits[k];
            memset(c, 0, sizeof *c);
            RSInto(f, c->id, (int)sizeof c->id);
            RSInto(f, c->message, TEXT_LEN);
            c->time   = RI(f);
            c->author = ver == 2 ? (int)RI(f) : -1;
            int ff = (int)RI(f);
            if (ff < 0 || ff > MAX_FILES) { loadErr = 1; break; }
            c->fileCount = ff;
            for (int j = 0; j < ff; j++) {
                RSInto(f, c->names[j], NAME_LEN);
                int b = (int)RI(f);
                if (b < 0 || b >= r->blobCount) { loadErr = 1; break; }
                c->blobs[j] = b;
            }
            r->commitCount++;
        }

        int ic = (int)RI(f);
        if (ic < 0 || ic > MAX_ISSUES) { loadErr = 1; break; }
        for (int k = 0; k < ic && !loadErr; k++) {
            Issue *is = &r->issues[r->issueCount++];
            RSInto(f, is->title, TEXT_LEN);
            is->open   = RI(f) != 0;
            is->time   = RI(f);
            is->author = ver == 2 ? (int)RI(f) : -1;
        }
    }
    fclose(f);

    if (loadErr) {
        for (int i = 0; i < repoCount; i++) FreeRepo(&repos[i]);
        memset(repos, 0, sizeof repos);
        memset(users, 0, sizeof users);
        repoCount = userCount = 0;
        nextUserId = 1;
        lastUser[0] = 0;
        remove(SAVE_FILE ".bak");
        rename(SAVE_FILE, SAVE_FILE ".bak");
        Toast("Save file was damaged - kept a copy as minigithub.dat.bak");
        return false;
    }
    return true;
}

/* An example repository for new accounts that want one. */
static void SeedExample(int owner)
{
    Repo *r = NewRepo(owner, "hello-world", "An example repository to try things out");
    if (!r) return;
    AddFile(r, "README.md",
        "# hello-world\n"
        "\n"
        "Welcome to Mini GitHub!\n"
        "\n"
        "This is an example repository so you can see how things work.\n");
    AddFile(r, "main.c",
        "#include <stdio.h>\n"
        "\n"
        "int main(void)\n"
        "{\n"
        "    printf(\"Hello, world!\\n\");\n"
        "    return 0;\n"
        "}\n");
    DoCommit(r, "Initial commit");

    WF_Set(&r->files[0],
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
    DoCommit(r, "Add instructions to the README");

    Issue *a = &r->issues[r->issueCount++];
    snprintf(a->title, TEXT_LEN, "%s", "Add a LICENSE file");
    a->open = true;
    a->time = (long long)time(NULL);
    a->author = owner;
}

/* ------------------------------------------------------------------ */
/* Widgets                                                            */
/* ------------------------------------------------------------------ */
static bool ButtonEx(Rectangle r, const char *label, Color bg, bool enabled)
{
    bool hot = enabled && Hover(r);
    if (hot) wantCursor = MOUSE_CURSOR_POINTING_HAND;
    DrawRectangleRounded(r, 0.25f, 6, hot ? ColorBrightness(bg, 0.15f) : bg);
    int w = MeasureText(label, FS);
    DrawText(label, (int)(r.x + (r.width - (float)w) / 2), (int)(r.y + (r.height - FS) / 2), FS, UI_TEXT);
    return enabled && Clicked(r);
}
static bool Button(Rectangle r, const char *label, Color bg) { return ButtonEx(r, label, bg, true); }

/* First click arms it (turns red), second click within 3 seconds does it. */
static bool ConfirmButton(Rectangle r, const char *label, const char *sure, int id)
{
    bool isArmed = (armedId == id);
    if (Button(r, isArmed ? sure : label, isArmed ? UI_DANGER : UI_BTN)) {
        if (isArmed) { armedId = 0; return true; }
        armedId = id;
        armedT  = 3.0f;
    }
    return false;
}

static void TF_Clear(TextField *t) { t->buf[0] = 0; t->len = 0; }

/* Tab / Shift+Tab moves between the text boxes on a form. */
static void TabCycle(TextField **f, int n)
{
    if (!IsKeyPressed(KEY_TAB)) return;
    int cur = -1;
    for (int i = 0; i < n; i++) { if (f[i]->active) cur = i; f[i]->active = false; }
    int dir = (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) ? -1 : 1;
    f[cur < 0 ? 0 : (cur + dir + n) % n]->active = true;
}

static void Checkbox(bool *v, int x, int y, const char *label)
{
    Rectangle row = {(float)x, (float)y - 3, (float)(34 + MeasureText(label, FS)), 28};
    Rectangle cb  = {(float)x, (float)y, 22, 22};
    if (Hover(row)) wantCursor = MOUSE_CURSOR_POINTING_HAND;
    DrawRectangleRec(cb, *v ? UI_GREEN : UI_BG);
    DrawRectangleLinesEx(cb, 1, *v ? UI_GREEN : UI_BORDER);
    if (*v) {
        DrawLineEx((Vector2){cb.x + 5, cb.y + 11}, (Vector2){cb.x + 9, cb.y + 16}, 3, UI_TEXT);
        DrawLineEx((Vector2){cb.x + 9, cb.y + 16}, (Vector2){cb.x + 17, cb.y + 6}, 3, UI_TEXT);
    }
    DrawText(label, x + 34, y + 1, FS, UI_TEXT);
    if (Clicked(row)) *v = !*v;
}

/* One-line text box. Returns true when Enter is pressed in it. */
static bool TextFieldUI(TextField *t, Rectangle r, const char *placeholder)
{
    bool enter = false;
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) t->active = Hover(r);
    if (Hover(r)) wantCursor = MOUSE_CURSOR_IBEAM;
    if (t->active) {
        int ch;
        while ((ch = GetCharPressed()) > 0)
            if (ch >= 32 && ch < 127 && t->len < TEXT_LEN - 1) { t->buf[t->len++] = (char)ch; t->buf[t->len] = 0; }
        if (KeyRep(KEY_BACKSPACE) && t->len > 0) t->buf[--t->len] = 0;
        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) enter = true;
    }
    DrawRectangleRec(r, UI_BG);
    DrawRectangleLinesEx(r, t->active ? 2.0f : 1.0f, t->active ? UI_LINK : UI_BORDER);

    /* password boxes show one * per character instead of the text */
    const char *shown = t->buf;
    char masked[TEXT_LEN];
    if (t->secret) { memset(masked, '*', (size_t)t->len); masked[t->len] = 0; shown = masked; }

    int tx = (int)r.x + 10, ty = (int)(r.y + (r.height - FS) / 2);
    int textW = MeasureText(shown, FS);
    int off = textW > (int)r.width - 30 ? textW - (int)r.width + 30 : 0;
    BeginScissorMode((int)r.x + 2, (int)r.y, (int)r.width - 4, (int)r.height);
    if (t->len == 0) DrawText(placeholder, tx, ty, FS, UI_MUTED);
    else             DrawText(shown, tx - off, ty, FS, UI_TEXT);
    if (t->active && ((int)(GetTime() * 2) % 2) == 0)
        DrawRectangle(tx - off + textW + (t->len ? 2 : 0), ty - 2, 2, FS + 4, UI_TEXT);
    EndScissorMode();
    return enter;
}

static void ScrollArea(Rectangle area, float *s, float contentH)
{
    if (Hover(area)) *s -= GetMouseWheelMove() * 40.0f;
    float max = contentH - area.height;
    if (max < 0) max = 0;
    if (*s > max) *s = max;
    if (*s < 0)   *s = 0;
}
static void DrawScrollbar(Rectangle area, float s, float contentH)
{
    if (contentH <= area.height) return;
    float th = area.height * area.height / contentH;
    float ty = area.y + (s / contentH) * area.height;
    DrawRectangleRounded((Rectangle){area.x + area.width - 7, ty, 5, th}, 1.0f, 4, UI_BORDER);
}

/* ------------------------------------------------------------------ */
/* Text editor                                                        */
/* ------------------------------------------------------------------ */
static int LineStart(const char *t, int p)        { while (p > 0 && t[p - 1] != '\n') p--; return p; }
static int LineEnd(const char *t, int len, int p) { while (p < len && t[p] != '\n') p++; return p; }
static int LineOf(const char *t, int p)           { int n = 0; for (int i = 0; i < p; i++) if (t[i] == '\n') n++; return n; }

static int XToCol(const char *line, int lineLen, float x)
{
    int best = 0;
    float bestD = 1e9f;
    for (int c = 0; c <= lineLen; c++) {
        float cx = (float)MeasureN(line, c, FS);
        float d = cx - x;
        if (d < 0) d = -d;
        if (d < bestD) { bestD = d; best = c; }
        if (cx > x + 30) break;
    }
    return best;
}

static void SelectFile(int i) { curFile = i; ed.cursor = 0; ed.scrollX = ed.scrollY = 0; }

/* Draws and runs the editor. Returns true if the text was changed.
   In read-only mode you can still click, scroll and move the cursor. */
static bool EditorUI(WorkFile *f, Rectangle area, bool readOnly)
{
    bool changed = false, moved = false;
    float gutter = 58, textX = area.x + gutter + 12, top = area.y + 8;

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) ed.focused = Hover(area);
    if (Hover(area)) {
        wantCursor = MOUSE_CURSOR_IBEAM;
        ed.scrollY -= GetMouseWheelMove() * LH * 3;
    }
    if (ed.cursor > f->len) ed.cursor = f->len;

    if (ed.focused) {
        int ch;
        while ((ch = GetCharPressed()) > 0)
            if (!readOnly && ch >= 32 && ch < 127) { char c = (char)ch; WF_Insert(f, ed.cursor, &c, 1); ed.cursor++; changed = true; }

        if (!readOnly && (KeyRep(KEY_ENTER) || KeyRep(KEY_KP_ENTER))) {   /* new line, keeping indentation */
            int ls = LineStart(f->text, ed.cursor), ind = 0;
            while (ls + ind < ed.cursor && f->text[ls + ind] == ' ' && ind < 100) ind++;
            char buf[128];
            buf[0] = '\n';
            memset(buf + 1, ' ', (size_t)ind);
            WF_Insert(f, ed.cursor, buf, ind + 1);
            ed.cursor += ind + 1;
            changed = true;
        }
        if (!readOnly && KeyRep(KEY_TAB))             { WF_Insert(f, ed.cursor, "    ", 4); ed.cursor += 4; changed = true; }
        if (!readOnly && KeyRep(KEY_BACKSPACE) && ed.cursor > 0)   { WF_Delete(f, ed.cursor - 1, 1); ed.cursor--; changed = true; }
        if (!readOnly && KeyRep(KEY_DELETE) && ed.cursor < f->len) { WF_Delete(f, ed.cursor, 1); changed = true; }
        if (KeyRep(KEY_LEFT) && ed.cursor > 0)        { ed.cursor--; moved = true; }
        if (KeyRep(KEY_RIGHT) && ed.cursor < f->len)  { ed.cursor++; moved = true; }
        if (IsKeyPressed(KEY_HOME))                   { ed.cursor = LineStart(f->text, ed.cursor); moved = true; }
        if (IsKeyPressed(KEY_END))                    { ed.cursor = LineEnd(f->text, f->len, ed.cursor); moved = true; }
        if (KeyRep(KEY_UP)) {
            int ls = LineStart(f->text, ed.cursor), col = ed.cursor - ls;
            if (ls > 0) {
                int pls = LineStart(f->text, ls - 1), plen = (ls - 1) - pls;
                ed.cursor = pls + (col < plen ? col : plen);
            }
            moved = true;
        }
        if (KeyRep(KEY_DOWN)) {
            int ls = LineStart(f->text, ed.cursor), col = ed.cursor - ls;
            int le = LineEnd(f->text, f->len, ed.cursor);
            if (le < f->len) {
                int nls = le + 1, nlen = LineEnd(f->text, f->len, nls) - nls;
                ed.cursor = nls + (col < nlen ? col : nlen);
            }
            moved = true;
        }
    }

    /* click to place the cursor */
    if (ed.focused && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && Hover(area)) {
        Vector2 m = GetMousePosition();
        int line = (int)((m.y - top + ed.scrollY) / LH);
        if (line < 0) line = 0;
        int p = 0, l = 0;
        while (l < line && p < f->len) { if (f->text[p] == '\n') l++; p++; }
        int ls = LineStart(f->text, p), le = LineEnd(f->text, f->len, ls);
        ed.cursor = ls + XToCol(f->text + ls, le - ls, m.x - textX + ed.scrollX);
    }

    /* keep the cursor on screen */
    int totalLines = LineOf(f->text, f->len) + 1;
    if (changed || moved) {
        float cy = (float)LineOf(f->text, ed.cursor) * LH, viewH = area.height - 16;
        if (cy < ed.scrollY) ed.scrollY = cy;
        if (cy + LH > ed.scrollY + viewH) ed.scrollY = cy + LH - viewH;
        int ls = LineStart(f->text, ed.cursor);
        float cx = (float)MeasureN(f->text + ls, ed.cursor - ls, FS), viewW = area.width - gutter - 30;
        if (cx - ed.scrollX > viewW) ed.scrollX = cx - viewW + 40;
        if (cx < ed.scrollX) ed.scrollX = cx - 40;
        if (ed.scrollX < 0) ed.scrollX = 0;
    }
    float maxScroll = totalLines * LH - (area.height - 16);
    if (maxScroll < 0) maxScroll = 0;
    if (ed.scrollY > maxScroll) ed.scrollY = maxScroll;
    if (ed.scrollY < 0) ed.scrollY = 0;

    /* draw */
    DrawRectangleRec(area, UI_BG);
    BeginScissorMode((int)area.x, (int)area.y, (int)area.width, (int)area.height);
    DrawRectangle((int)area.x, (int)area.y, (int)gutter, (int)area.height, UI_PANEL);
    const char *t = f->text;
    int p = 0, line = 0, curLine = LineOf(t, ed.cursor);
    for (;;) {
        int le = LineEnd(t, f->len, p);
        float y = top + line * LH - ed.scrollY;
        if (y > area.y + area.height) break;
        if (y + LH >= area.y) {
            if (line == curLine && ed.focused)
                DrawRectangle((int)(area.x + gutter), (int)y - 2, (int)(area.width - gutter), LH, (Color){255, 255, 255, 10});

            /* light colouring: headings / #include lines, and // comments */
            int s = p;
            while (s < le && t[s] == ' ') s++;
            Color c = UI_TEXT;
            if (s < le && t[s] == '#') c = UI_ORANGE;
            else if (s + 1 < le && t[s] == '/' && t[s + 1] == '/') c = UI_MUTED;
            DrawText(Slice(t + p, le - p), (int)(textX - ed.scrollX), (int)y, FS, c);

            if (line == curLine && ed.focused && ((int)(GetTime() * 2) % 2) == 0) {
                int cx = (int)(textX - ed.scrollX) + MeasureN(t + p, ed.cursor - p, FS);
                DrawRectangle(cx, (int)y - 2, 2, FS + 4, UI_TEXT);
            }
            /* line numbers, drawn over any text that scrolled left */
            DrawRectangle((int)area.x, (int)y - 2, (int)gutter, LH, UI_PANEL);
            const char *num = TextFormat("%d", line + 1);
            DrawText(num, (int)(area.x + gutter - 10) - MeasureText(num, FS), (int)y, FS,
                     line == curLine ? UI_TEXT : UI_MUTED);
        }
        if (le >= f->len) break;
        p = le + 1;
        line++;
    }
    EndScissorMode();
    return changed;
}

/* ------------------------------------------------------------------ */
/* Days and contributions                                             */
/*                                                                    */
/* A "contribution" is a commit you made or an issue you opened.      */
/* Days run from local midnight to local midnight.                    */
/* ------------------------------------------------------------------ */

/* Midnight (local time) at the start of the day that t falls on. */
static long long DayStart(long long t)
{
    time_t tt = (time_t)t;
    struct tm lt = *localtime(&tt);
    lt.tm_hour = lt.tm_min = lt.tm_sec = 0;
    lt.tm_isdst = -1;
    return (long long)mktime(&lt);
}

/* Midnight n days later (n may be negative). Safe across clock changes. */
static long long AddDays(long long dayStart, int n)
{
    time_t tt = (time_t)dayStart;
    struct tm lt = *localtime(&tt);
    lt.tm_mday += n;
    lt.tm_hour = lt.tm_min = lt.tm_sec = 0;
    lt.tm_isdst = -1;
    return (long long)mktime(&lt);
}

/* Whole days from midnight a to midnight b (rounded, so a 23- or 25-hour day still counts as one). */
static int DaysBetween(long long a, long long b)
{
    long long d = b - a;
    return (int)((d >= 0 ? d + 43200 : d - 43200) / 86400);
}

/* "Tuesday 13 October 2026" (long) or "Tue 13 Oct 2026" (short). */
static const char *FormatDay(long long day, bool longForm)
{
    static char b[64];
    time_t tt = (time_t)day;
    struct tm lt = *localtime(&tt);
    char wd[16], mon[16];
    strftime(wd,  sizeof wd,  longForm ? "%A" : "%a", &lt);
    strftime(mon, sizeof mon, longForm ? "%B" : "%b", &lt);
    snprintf(b, sizeof b, "%s %d %s %d", wd, lt.tm_mday, mon, lt.tm_year + 1900);
    return b;
}

/* "1 commit", "3 commits", "2 repositories"... written into the caller's buffer. */
static const char *Count(char *buf, int n, const char *one, const char *many)
{
    snprintf(buf, 48, "%d %s", n, n == 1 ? one : many);
    return buf;
}

/* "hello-world" for your own repos, "alice/hello-world" for anyone else's. */
static const char *RepoLabel(int i)
{
    static char b[2][NAME_LEN * 2 + 4];
    static int which;
    which ^= 1;
    Repo *r = &repos[i];
    if (IsMine(r)) snprintf(b[which], sizeof b[which], "%s", r->name);
    else           snprintf(b[which], sizeof b[which], "%s/%s", UserName(r->owner), r->name);
    return b[which];
}

/* How many files a commit touched and how many lines it added / removed.
   Worked out the first time it's needed, then remembered. */
static void CommitStats(Repo *r, int ci)
{
    Commit *c = &r->commits[ci];
    if (c->statsDone) return;
    Commit *p = ci > 0 ? &r->commits[ci - 1] : NULL;
    int files = 0, adds = 0, dels = 0, a, d;
    for (int k = 0; k < c->fileCount; k++) {
        int pk = p ? FindInCommit(p, c->names[k]) : -1;
        int ob = pk >= 0 ? p->blobs[pk] : -1;
        if (ob == c->blobs[k]) continue;
        ComputeDiff(BlobText(r, ob), BlobText(r, c->blobs[k]), &a, &d);
        files++; adds += a; dels += d;
    }
    if (p) for (int k = 0; k < p->fileCount; k++) {
        if (FindInCommit(c, p->names[k]) >= 0) continue;          /* file was deleted */
        ComputeDiff(BlobText(r, p->blobs[k]), "", &a, &d);
        files++; adds += a; dels += d;
    }
    c->sFiles = files; c->sAdds = adds; c->sDels = dels;
    c->statsDone = true;
}

/* The files a commit changed: kind is 'A' added, 'M' modified, 'D' deleted. Cheap - no diffing. */
static int CommitFileList(Repo *r, int ci, char *kinds, const char **names)
{
    Commit *c = &r->commits[ci];
    Commit *p = ci > 0 ? &r->commits[ci - 1] : NULL;
    int n = 0;
    for (int k = 0; k < c->fileCount; k++) {
        int pk = p ? FindInCommit(p, c->names[k]) : -1;
        if (pk >= 0 && p->blobs[pk] == c->blobs[k]) continue;
        kinds[n] = pk < 0 ? 'A' : 'M';
        names[n++] = c->names[k];
    }
    if (p) for (int k = 0; k < p->fileCount; k++)
        if (FindInCommit(c, p->names[k]) < 0) { kinds[n] = 'D'; names[n++] = p->names[k]; }
    return n;
}

/* Everything one user did on one day. */
typedef struct { char kind; int repo, idx; long long time; } Activity;   /* kind: 'C' commit, 'I' issue */
typedef struct { int repo, commits, issues, adds, dels; } RepoTally;

#define MAX_ACTS 1024
static Activity  acts[MAX_ACTS];
static int       actCount, actTotal;                 /* stored / actually happened */
static RepoTally tallies[MAX_REPOS];
static int       tallyCount, dayCommits, dayIssues, dayAdds, dayDels, dayFiles;

static void GatherDay(int uid, long long start)
{
    long long end = AddDays(start, 1);
    actCount = actTotal = tallyCount = 0;
    dayCommits = dayIssues = dayAdds = dayDels = dayFiles = 0;

    for (int i = 0; i < repoCount; i++) {
        Repo *r = &repos[i];
        RepoTally t = {i, 0, 0, 0, 0};
        for (int k = 0; k < r->commitCount; k++) {
            Commit *c = &r->commits[k];
            if (c->author != uid || c->time < start || c->time >= end) continue;
            CommitStats(r, k);
            t.commits++; t.adds += c->sAdds; t.dels += c->sDels;
            dayFiles += c->sFiles;
            if (actCount < MAX_ACTS) acts[actCount++] = (Activity){'C', i, k, c->time};
            actTotal++;
        }
        for (int k = 0; k < r->issueCount; k++) {
            Issue *is = &r->issues[k];
            if (is->author != uid || is->time < start || is->time >= end) continue;
            t.issues++;
            if (actCount < MAX_ACTS) acts[actCount++] = (Activity){'I', i, k, is->time};
            actTotal++;
        }
        if (t.commits || t.issues) {
            tallies[tallyCount++] = t;
            dayCommits += t.commits; dayIssues += t.issues;
            dayAdds += t.adds;       dayDels += t.dels;
        }
    }
    /* timeline: earliest first */
    for (int a = 1; a < actCount; a++) {
        Activity x = acts[a];
        int b = a - 1;
        while (b >= 0 && acts[b].time > x.time) { acts[b + 1] = acts[b]; b--; }
        acts[b + 1] = x;
    }
    /* repositories: busiest first */
    for (int a = 1; a < tallyCount; a++) {
        RepoTally x = tallies[a];
        int b = a - 1;
        while (b >= 0 && tallies[b].commits + tallies[b].issues < x.commits + x.issues) { tallies[b + 1] = tallies[b]; b--; }
        tallies[b + 1] = x;
    }
}

/* "3 commits, 1 issue" */
static const char *TallyText(int commits, int issues)
{
    static char b[104];
    char c1[48], c2[48];
    if (commits && issues) snprintf(b, sizeof b, "%s, %s", Count(c1, commits, "commit", "commits"), Count(c2, issues, "issue", "issues"));
    else if (commits)      snprintf(b, sizeof b, "%s", Count(c1, commits, "commit", "commits"));
    else                   snprintf(b, sizeof b, "%s", Count(c2, issues, "issue opened", "issues opened"));
    return b;
}

/* Draws "+12 -4" in green and red. Returns the width used. */
static int DrawAddsDels(int adds, int dels, int x, int y)
{
    const char *p = TextFormat("+%d", adds);
    int w = MeasureText(p, FS);
    DrawText(p, x, y, FS, UI_GREENT);
    const char *m = TextFormat("-%d", dels);
    DrawText(m, x + w + 10, y, FS, UI_REDT);
    return w + 10 + MeasureText(m, FS);
}

/* The little pop-up shown when the mouse is over a day square. */
static void DrawDayTooltip(long long day)
{
    GatherDay(curUser, day);

    enum { MAXL = 12 };
    char  text[MAXL][160];
    Color col[MAXL];
    int   addsDels[MAXL][2];     /* -1 = none; otherwise +adds -dels drawn after the text */
    bool  bold[MAXL] = {0}, rule[MAXL] = {0};
    int   linkLen[MAXL] = {0};   /* this many leading characters are a repository name, drawn in blue */
    int   n = 0;
    char  b1[48], b2[48];

#define LINE(c) (addsDels[n][0] = -1, col[n] = (c), n++)
    snprintf(text[n], 160, "%s", FormatDay(day, false)); bold[n] = true; LINE(UI_TEXT);
    if (actTotal == 0) {
        snprintf(text[n], 160, "No contributions"); LINE(UI_MUTED);
    } else {
        snprintf(text[n], 160, "%s in %s", Count(b1, actTotal, "contribution", "contributions"),
                 Count(b2, tallyCount, "repository", "repositories"));
        LINE(UI_TEXT);
        if (dayCommits) {
            snprintf(text[n], 160, "%s, %s changed   ", Count(b1, dayCommits, "commit", "commits"),
                     Count(b2, dayFiles, "file", "files"));
            addsDels[n][0] = dayAdds; addsDels[n][1] = dayDels; col[n] = UI_MUTED; n++;
        }
        if (dayIssues) { snprintf(text[n], 160, "%s", Count(b1, dayIssues, "issue opened", "issues opened")); LINE(UI_MUTED); }

        rule[n] = true;
        int shown = tallyCount < 5 ? tallyCount : 5;
        for (int i = 0; i < shown; i++) {
            RepoTally *t = &tallies[i];
            const char *label = RepoLabel(t->repo);
            snprintf(text[n], 160, "%s:  %s   ", label, TallyText(t->commits, t->issues));
            linkLen[n] = (int)strlen(label);
            col[n] = UI_MUTED;
            addsDels[n][0] = t->commits ? t->adds : -1;
            addsDels[n][1] = t->dels;
            n++;
        }
        if (tallyCount > shown) {
            snprintf(text[n], 160, "...and %s", Count(b1, tallyCount - shown, "more repository", "more repositories"));
            LINE(UI_MUTED);
        }
    }
    rule[n] = true;
    snprintf(text[n], 160, "Click to see the whole day"); LINE(UI_MUTED);
#undef LINE

    /* size and place it next to the mouse, kept on screen */
    int w = 0;
    for (int i = 0; i < n; i++) {
        int lw = MeasureText(text[i], FS);
        if (addsDels[i][0] >= 0) lw += MeasureText(TextFormat("+%d -%d", addsDels[i][0], addsDels[i][1]), FS) + 10;
        if (lw > w) w = lw;
    }
    int rules = 0;
    for (int i = 0; i < n; i++) if (rule[i]) rules++;
    int bw = w + 32, bh = n * 26 + rules * 10 + 22;
    Vector2 m = GetMousePosition();
    int x = (int)m.x + 18, y = (int)m.y + 22;
    if (x + bw > W - 8) x = (int)m.x - 18 - bw;
    if (x < 8) x = 8;
    if (y + bh > H - 8) y = (int)m.y - 12 - bh;
    if (y < 8) y = 8;

    Rectangle box = {(float)x, (float)y, (float)bw, (float)bh};
    DrawRectangleRounded((Rectangle){box.x + 3, box.y + 4, box.width, box.height}, 0.06f, 6, (Color){0, 0, 0, 120});
    DrawRectangleRounded(box, 0.06f, 6, (Color){36, 41, 49, 255});
    DrawRectangleRoundedLinesEx(box, 0.06f, 6, 1, UI_BORDER);

    int ty = y + 12;
    for (int i = 0; i < n; i++) {
        if (rule[i]) { DrawLine(x + 12, ty + 3, x + bw - 12, ty + 3, UI_BORDER); ty += 10; }
        if (bold[i]) DrawBold(text[i], x + 16, ty, FS, col[i]);
        else         DrawText(text[i], x + 16, ty, FS, col[i]);
        if (linkLen[i] > 0) {            /* redraw the name part in blue on a clean background */
            int nw = MeasureText(Slice(text[i], linkLen[i]), FS);
            DrawRectangle(x + 15, ty - 2, nw + 2, FS + 4, (Color){36, 41, 49, 255});
            DrawText(Slice(text[i], linkLen[i]), x + 16, ty, FS, UI_LINK);
        }
        if (addsDels[i][0] >= 0) DrawAddsDels(addsDels[i][0], addsDels[i][1], x + 16 + MeasureText(text[i], FS), ty);
        ty += 26;
    }
}

/* ------------------------------------------------------------------ */
/* Navigation                                                         */
/* ------------------------------------------------------------------ */
static void Unfocus(void)
{
    tfSearch.active = tfName.active = tfDesc.active = false;
    tfNewFile.active = tfCommit.active = tfIssue.active = false;
    tfUser.active = tfPass.active = tfPass2.active = false;
    tfOldPass.active = tfNewPass.active = tfNewPass2.active = tfDelPass.active = false;
    ed.focused = false;
    armedId = 0;
}

/* Wipe every password box so passwords don't sit around in memory or on screen. */
static void ClearSecrets(void)
{
    TextField *s[] = {&tfPass, &tfPass2, &tfOldPass, &tfNewPass, &tfNewPass2, &tfDelPass};
    for (int i = 0; i < 6; i++) { memset(s[i]->buf, 0, sizeof s[i]->buf); s[i]->len = 0; }
}

static void PrefillUser(void)
{
    snprintf(tfUser.buf, TEXT_LEN, "%s", lastUser);
    tfUser.len = (int)strlen(tfUser.buf);
}

static void GoLogin(void)
{
    Unfocus();
    ClearSecrets();
    PrefillUser();
    screen = SCR_LOGIN;
    if (tfUser.len) tfPass.active = true; else tfUser.active = true;
}
static void GoRegister(void)
{
    Unfocus();
    ClearSecrets();
    TF_Clear(&tfUser);
    optExample = true;
    screen = SCR_REGISTER;
    tfUser.active = true;
}

static void GoHome(void)
{
    if (curUser < 0) { GoLogin(); return; }
    Unfocus();
    screen = SCR_HOME;
    curRepo = -1;
}

static void SignIn(int id)
{
    User *u = FindUserById(id);
    if (!u) return;
    curUser = id;
    snprintf(lastUser, NAME_LEN, "%s", u->name);
    ClearSecrets();
    TF_Clear(&tfSearch);
    homeView = 0;
    scHome = 0;
    failedLogins = 0;
    MarkDirty();
    GoHome();
}

static void SignOut(void)
{
    curUser = -1;
    curRepo = -1;
    GoLogin();
    Toast("You've signed out");
}

static void OpenAccount(void)
{
    Unfocus();
    ClearSecrets();
    screen = SCR_ACCOUNT;
}

static void OpenDay(long long day)
{
    Unfocus();
    curDay = day;
    scDay = scDayRepos = 0;
    screen = SCR_DAY;
}

static void OpenRepo(int i)
{
    Unfocus();
    fromDay = false;
    curRepo = i;
    screen  = SCR_REPO;
    tab     = TAB_CODE;
    SelectFile(ShownCount(&repos[i]) ? 0 : -1);
    scFiles = scCommits = scIssues = 0;
    TF_Clear(&tfCommit);
    TF_Clear(&tfNewFile);
    TF_Clear(&tfIssue);
}

static void SelectChange(int i)
{
    curChange = i;
    scDiff = 0;
    if (i >= 0) {
        Repo *r = &repos[curRepo];
        ComputeDiff(BlobText(r, changes[i].oldB), BlobText(r, changes[i].newB), NULL, NULL);
    } else dOpCount = 0;
}

static void OpenCommit(int ci)
{
    Unfocus();
    curCommit = ci;
    fromDay = false;
    screen = SCR_COMMIT;
    BuildChanges(&repos[curRepo], ci);
    scChanges = 0;
    SelectChange(changeCount ? 0 : -1);
}

/* ------------------------------------------------------------------ */
/* Account actions                                                    */
/* ------------------------------------------------------------------ */
static void DoLogin(void)
{
    if (lockT > 0) { Toast(TextFormat("Too many wrong attempts - wait %d seconds", (int)lockT + 1)); return; }
    if (!tfUser.buf[0] || !tfPass.buf[0]) { Toast("Enter your username and password"); return; }

    User *u = FindUserByName(tfUser.buf);
    bool ok;
    if (u) ok = CheckPassword(u, tfPass.buf);
    else {
        /* do the same slow work anyway, so you can't tell "no such user" from "wrong password" */
        char dummy[65];
        HashPassword(tfPass.buf, "00000000000000000000000000000000", dummy);
        ok = false;
    }
    ClearSecrets();

    if (!ok) {
        if (++failedLogins >= 5) {
            failedLogins = 0;
            lockT = 30;
            Toast("Too many wrong attempts - sign-in is paused for 30 seconds");
        } else Toast("Incorrect username or password");
        Unfocus();
        tfPass.active = true;
        return;
    }
    char nm[NAME_LEN];
    snprintf(nm, NAME_LEN, "%s", u->name);
    SignIn(u->id);
    Toast(TextFormat("Welcome back, %s", nm));
}

static void DoRegister(void)
{
    const char *err = ValidateUsername(tfUser.buf);
    if (!err && (int)strlen(tfPass.buf) < MIN_PASS) err = TextFormat("Passwords need at least %d characters", MIN_PASS);
    if (!err && strcmp(tfPass.buf, tfPass2.buf) != 0) err = "The two passwords don't match";
    if (!err && userCount >= MAX_USERS) err = "This computer has reached the maximum number of accounts";
    if (err) { Toast(err); return; }

    User *u = &users[userCount++];
    memset(u, 0, sizeof *u);
    u->id = nextUserId++;
    snprintf(u->name, NAME_LEN, "%s", tfUser.buf);
    u->created = (long long)time(NULL);
    SetPassword(u, tfPass.buf);

    int  uid = u->id;
    char nm[NAME_LEN];
    snprintf(nm, NAME_LEN, "%s", u->name);
    SignIn(uid);
    int claimed = ClaimOrphans(uid);
    if (optExample && !claimed) SeedExample(uid);
    MarkDirty();
    Toast(claimed ? TextFormat("Welcome, %s - your existing repositories are now yours", nm)
                  : TextFormat("Welcome to Mini GitHub, %s!", nm));
}

static void DoChangePassword(void)
{
    User *u = FindUserById(curUser);
    if (!u) return;
    const char *msg = NULL;
    if (!CheckPassword(u, tfOldPass.buf))                 msg = "Your current password is wrong";
    else if ((int)strlen(tfNewPass.buf) < MIN_PASS)      msg = TextFormat("New passwords need at least %d characters", MIN_PASS);
    else if (strcmp(tfNewPass.buf, tfNewPass2.buf) != 0) msg = "The two new passwords don't match";
    else {
        SetPassword(u, tfNewPass.buf);
        MarkDirty();
        msg = "Password updated";
    }
    ClearSecrets();
    Unfocus();
    Toast(msg);
}

static void DoDeleteAccount(void)
{
    User *u = FindUserById(curUser);
    if (!u) return;
    if (!CheckPassword(u, tfDelPass.buf)) { ClearSecrets(); Toast("Wrong password - your account was not deleted"); return; }
    char nm[NAME_LEN];
    snprintf(nm, NAME_LEN, "%s", u->name);
    int id = curUser;
    curUser = -1;
    DeleteAccount(id);
    if (EqualsCI(lastUser, nm)) lastUser[0] = 0;
    MarkDirty();
    if (userCount) GoLogin(); else GoRegister();
    Toast(TextFormat("The account %s has been deleted", nm));
}

/* ------------------------------------------------------------------ */
/* Screens                                                            */
/* ------------------------------------------------------------------ */
static Color AvatarColor(int id)
{
    static const Color pal[6] = {
        {137, 87, 229, 255}, { 31, 111, 235, 255}, { 35, 134,  54, 255},
        {191, 135,  0, 255}, {207,  34,  46, 255}, {219,  97, 162, 255}
    };
    return pal[(id < 0 ? 0 : id) % 6];
}

/* A coloured circle with the first letter of the user's name. */
static void DrawAvatar(int id, int cx, int cy, int radius)
{
    const char *n = UserName(id);
    char letter[2] = {(char)toupper((unsigned char)n[0]), 0};
    int fs = radius >= 40 ? 60 : radius >= 24 ? 30 : 20;
    DrawCircle(cx, cy, (float)radius, AvatarColor(id));
    DrawText(letter, cx - MeasureText(letter, fs) / 2, cy - fs / 2 + 1, fs, UI_TEXT);
}

static void DrawLogo(int cx, int cy)
{
    DrawCircle(cx, cy, 18, UI_TEXT);
    DrawLineEx((Vector2){(float)cx - 6, (float)cy - 10}, (Vector2){(float)cx - 6, (float)cy + 10}, 3, UI_BG);
    DrawLineEx((Vector2){(float)cx + 6, (float)cy - 6},  (Vector2){(float)cx - 6, (float)cy + 4},  3, UI_BG);
    DrawCircle(cx - 6, cy - 10, 4, UI_BG);
    DrawCircle(cx - 6, cy + 10, 4, UI_BG);
    DrawCircle(cx + 6, cy - 6, 4, UI_BG);
}

static void DrawHeader(void)
{
    DrawRectangle(0, 0, W, 56, UI_PANEL);
    DrawLine(0, 56, W, 56, UI_BORDER);
    DrawLogo(38, 28);
    DrawBold("Mini GitHub", 68, 18, FS, UI_TEXT);
    if (curUser < 0) return;

    Rectangle home = {18, 8, 200, 40};
    if (Hover(home)) wantCursor = MOUSE_CURSOR_POINTING_HAND;
    if (Clicked(home)) GoHome();

    if (Button((Rectangle){(float)W - 130, 10, 112, 36}, "Sign out", UI_BTN)) { SignOut(); return; }

    /* name + avatar: click to open account settings */
    const char *nm = UserName(curUser);
    int nw = MeasureText(nm, FS);
    int bw = nw + 8 + 12 + 32 + 8;
    Rectangle acct = {(float)(W - 146 - bw), 8, (float)bw, 40};
    if (Hover(acct)) {
        wantCursor = MOUSE_CURSOR_POINTING_HAND;
        DrawRectangleRounded(acct, 0.3f, 6, UI_HOVER);
    }
    DrawText(nm, (int)acct.x + 8, 18, FS, UI_TEXT);
    DrawAvatar(curUser, (int)acct.x + 8 + nw + 12 + 16, 28, 16);
    if (Clicked(acct)) OpenAccount();
}

static Color Heat(int c)
{
    if (c <= 0) return (Color){ 33,  38,  45, 255};
    if (c == 1) return (Color){ 14,  68,  41, 255};
    if (c == 2) return (Color){  0, 109,  50, 255};
    if (c <= 4) return (Color){ 38, 166,  65, 255};
    return             (Color){ 57, 211,  83, 255};
}

/* Squares for the last 26 weeks, coloured by how many contributions uid made that day.
   Hovering a square sets *hoverDay; clicking one opens that day's page. */
static void DrawContributions(Rectangle area, int uid, long long *hoverDay)
{
    enum { WEEKS = 26 };
    int counts[WEEKS * 7] = {0}, total = 0;
    long long today = DayStart((long long)time(NULL));
    time_t tt = (time_t)today;
    int wd = localtime(&tt)->tm_wday;                 /* 0 = Sunday: rows run Sunday to Saturday */
    int todayPos = (WEEKS - 1) * 7 + wd;

    for (int i = 0; i < repoCount; i++) {
        Repo *r = &repos[i];
        for (int k = 0; k < r->commitCount + r->issueCount; k++) {
            bool isCommit = k < r->commitCount;
            int author    = isCommit ? r->commits[k].author : r->issues[k - r->commitCount].author;
            long long t   = isCommit ? r->commits[k].time   : r->issues[k - r->commitCount].time;
            if (author != uid) continue;
            int pos = todayPos - DaysBetween(DayStart(t), today);
            if (pos >= 0 && pos <= todayPos) { counts[pos]++; total++; }
        }
    }

    DrawRectangleRec(area, UI_PANEL);
    DrawRectangleLinesEx(area, 1, UI_BORDER);
    DrawText(TextFormat("%d contribution%s in the last 6 months", total, total == 1 ? "" : "s"),
             (int)area.x + 16, (int)area.y + 12, FS, UI_TEXT);
    const char *hint = "Hover a day for details, click it for the full list";
    int titleW = MeasureText(TextFormat("%d contribution%s in the last 6 months", total, total == 1 ? "" : "s"), FS);
    if (area.width > titleW + MeasureText(hint, FS) + 60)
        DrawText(hint, (int)(area.x + area.width) - 16 - MeasureText(hint, FS), (int)area.y + 12, FS, UI_MUTED);

    int hovered = -1;
    for (int pos = 0; pos <= todayPos; pos++) {
        int col = pos / 7, row = pos % 7;
        Rectangle cell = {area.x + 16 + col * 15, area.y + 44 + row * 15, 12, 12};
        Rectangle hit  = {cell.x - 1, cell.y - 1, 15, 15};     /* no gaps between hover areas */
        DrawRectangleRounded(cell, 0.3f, 4, Heat(counts[pos]));
        if (Hover(hit)) hovered = pos;
    }
    if (hovered >= 0) {
        int col = hovered / 7, row = hovered % 7;
        Rectangle cell = {area.x + 16 + col * 15, area.y + 44 + row * 15, 12, 12};
        DrawRectangleRoundedLinesEx((Rectangle){cell.x - 1, cell.y - 1, 14, 14}, 0.3f, 4, 2, UI_TEXT);
        wantCursor = MOUSE_CURSOR_POINTING_HAND;
        long long day = AddDays(today, hovered - todayPos);
        *hoverDay = day;
        if (Clicked((Rectangle){cell.x - 1, cell.y - 1, 15, 15})) OpenDay(day);
    }

    int lx = (int)(area.x + area.width) - 200, ly = (int)(area.y + area.height) - 30;
    DrawText("Less", lx, ly, FS, UI_MUTED);
    for (int i = 0; i < 5; i++)
        DrawRectangleRounded((Rectangle){(float)lx + 52 + i * 16, (float)ly + 4, 12, 12}, 0.3f, 4, Heat(i == 4 ? 5 : i));
    DrawText("More", lx + 138, ly, FS, UI_MUTED);
}

/* ---------------- sign in / create account ---------------- */
static void DrawCentered(const char *t, int y, int fs, Color c) { DrawText(t, W / 2 - MeasureText(t, fs) / 2, y, fs, c); }

/* A grey box under the form with "question  link". Returns true when the link is clicked. */
static bool SwitchBox(float x, float y, float w, const char *question, const char *link)
{
    Rectangle q = {x, y, w, 56};
    DrawRectangleRec(q, UI_BG);
    DrawRectangleLinesEx(q, 1, UI_BORDER);
    int aw = MeasureText(question, FS), lw = MeasureText(link, FS);
    int sx = (int)(x + w / 2) - (aw + lw) / 2;
    DrawText(question, sx, (int)y + 18, FS, UI_MUTED);
    DrawText(link, sx + aw, (int)y + 18, FS, UI_LINK);
    Rectangle hit = {(float)(sx + aw), y + 12, (float)lw, 32};
    if (Hover(hit)) { wantCursor = MOUSE_CURSOR_POINTING_HAND; DrawLine(sx + aw, (int)y + 40, sx + aw + lw, (int)y + 40, UI_LINK); }
    return Clicked(hit);
}

static void ScreenLogin(void)
{
    TextField *order[] = {&tfUser, &tfPass};
    TabCycle(order, 2);

    float pw = 440, px = ((float)W - pw) / 2;
    DrawCentered("Sign in to Mini GitHub", 96, 30, UI_TEXT);

    Rectangle p = {px, 150, pw, 296};
    DrawRectangleRec(p, UI_PANEL);
    DrawRectangleLinesEx(p, 1, UI_BORDER);
    float x = px + 24, y = p.y, fw = pw - 48;

    DrawText("Username", (int)x, (int)y + 24, FS, UI_TEXT);
    bool e1 = TextFieldUI(&tfUser, (Rectangle){x, y + 50, fw, 40}, "your-username");
    DrawText("Password", (int)x, (int)y + 108, FS, UI_TEXT);
    bool e2 = TextFieldUI(&tfPass, (Rectangle){x, y + 134, fw, 40}, "");
    if (lockT > 0) DrawText(TextFormat("Paused - try again in %d s", (int)lockT + 1), (int)x, (int)y + 188, FS, UI_REDT);
    bool go = Button((Rectangle){x, y + 226, fw, 44}, "Sign in", UI_GREEN);

    if (e1 && !tfPass.len) { Unfocus(); tfPass.active = true; }   /* Enter in username: jump to password */
    else if (go || e1 || e2) DoLogin();

    if (SwitchBox(px, 466, pw, "New to Mini GitHub? ", "Create an account")) GoRegister();
    if (userCount == 0) DrawCentered("No accounts exist yet - create the first one.", 546, FS, UI_MUTED);
}

static void ScreenRegister(void)
{
    TextField *order[] = {&tfUser, &tfPass, &tfPass2};
    TabCycle(order, 3);

    float pw = 460, px = ((float)W - pw) / 2;
    DrawCentered("Create your account", 80, 30, UI_TEXT);

    Rectangle p = {px, 130, pw, 404};
    DrawRectangleRec(p, UI_PANEL);
    DrawRectangleLinesEx(p, 1, UI_BORDER);
    float x = px + 24, y = p.y, fw = pw - 48;

    DrawText("Username", (int)x, (int)y + 22, FS, UI_TEXT);
    bool e1 = TextFieldUI(&tfUser, (Rectangle){x, y + 48, fw, 40}, "letters, numbers, - and _");

    DrawText("Password", (int)x, (int)y + 104, FS, UI_TEXT);
    const char *rule = TextFormat("at least %d characters", MIN_PASS);
    DrawText(rule, (int)(x + fw) - MeasureText(rule, FS), (int)y + 104, FS, tfPass.len >= MIN_PASS ? UI_GREENT : UI_MUTED);
    bool e2 = TextFieldUI(&tfPass, (Rectangle){x, y + 130, fw, 40}, "");

    DrawText("Confirm password", (int)x, (int)y + 186, FS, UI_TEXT);
    if (tfPass2.len) {
        bool same = strcmp(tfPass.buf, tfPass2.buf) == 0;
        const char *m = same ? "matches" : "doesn't match";
        DrawText(m, (int)(x + fw) - MeasureText(m, FS), (int)y + 186, FS, same ? UI_GREENT : UI_REDT);
    }
    bool e3 = TextFieldUI(&tfPass2, (Rectangle){x, y + 212, fw, 40}, "");

    if (OrphanCount() > 0 && userCount == 0)
        DrawText("Your existing repositories will move here.", (int)x, (int)y + 272, FS, UI_YELLOW);
    else
        Checkbox(&optExample, (int)x, (int)y + 272, "Start with an example repository");

    bool go = Button((Rectangle){x, y + 328, fw, 44}, "Create account", UI_GREEN);
    if (go || e1 || e2 || e3) DoRegister();

    if (userCount > 0 && SwitchBox(px, 554, pw, "Already have an account? ", "Sign in")) GoLogin();
}

/* ---------------- account settings ---------------- */
static void ScreenAccount(void)
{
    User *u = FindUserById(curUser);
    if (!u) { SignOut(); return; }
    TextField *order[] = {&tfOldPass, &tfNewPass, &tfNewPass2, &tfDelPass};
    TabCycle(order, 4);

    DrawAvatar(u->id, 60, 112, 40);
    DrawBold(u->name, 120, 84, 30, UI_TEXT);
    int owned = ReposOwnedBy(u->id);
    DrawText(TextFormat("Joined %s   |   %d repositor%s", TimeAgo(u->created), owned, owned == 1 ? "y" : "ies"),
             120, 120, FS, UI_MUTED);
    if (Button((Rectangle){(float)W - 160, 88, 140, 40}, "Back", UI_BTN)) { GoHome(); return; }

    float pw = ((float)W - 60) / 2;
    Rectangle a = {20, 176, pw, 380}, b = {40 + pw, 176, pw, 380};
    float fw = pw - 48;

    /* change password */
    DrawRectangleRec(a, UI_PANEL);
    DrawRectangleLinesEx(a, 1, UI_BORDER);
    float x = a.x + 24, y = a.y;
    DrawBold("Change password", (int)x, (int)y + 20, FS, UI_TEXT);
    DrawText("Current password", (int)x, (int)y + 64, FS, UI_MUTED);
    bool c1 = TextFieldUI(&tfOldPass, (Rectangle){x, y + 90, fw, 40}, "");
    DrawText("New password", (int)x, (int)y + 144, FS, UI_MUTED);
    bool c2 = TextFieldUI(&tfNewPass, (Rectangle){x, y + 170, fw, 40}, "");
    DrawText("Confirm new password", (int)x, (int)y + 224, FS, UI_MUTED);
    bool c3 = TextFieldUI(&tfNewPass2, (Rectangle){x, y + 250, fw, 40}, "");
    if (Button((Rectangle){x, y + 312, 220, 44}, "Update password", UI_GREEN) || c1 || c2 || c3) DoChangePassword();

    /* delete account */
    DrawRectangleRec(b, UI_PANEL);
    DrawRectangleLinesEx(b, 1, UI_DANGER);
    x = b.x + 24; y = b.y;
    DrawBold("Delete account", (int)x, (int)y + 20, FS, UI_REDT);
    DrawText("This permanently deletes your account", (int)x, (int)y + 64, FS, UI_MUTED);
    DrawText(TextFormat("and all %d of your repositories.", owned), (int)x, (int)y + 90, FS, UI_MUTED);
    DrawText("This can't be undone.", (int)x, (int)y + 116, FS, UI_MUTED);
    DrawText("Type your password to confirm", (int)x, (int)y + 224, FS, UI_TEXT);
    TextFieldUI(&tfDelPass, (Rectangle){x, y + 250, fw, 40}, "");
    if (ConfirmButton((Rectangle){x, y + 312, 260, 44}, "Delete my account", "Click again to delete", 4)) DoDeleteAccount();
}

/* ---------------- home ---------------- */
static void ScreenHome(void)
{
    User *me = FindUserById(curUser);
    if (!me) { SignOut(); return; }

    /* sidebar: who you are and your numbers */
    int sx = 20, sy = 76;
    DrawAvatar(curUser, sx + 64, sy + 64, 60);
    DrawFit(me->name, sx, sy + 140, 30, 280, UI_TEXT);
    DrawText(TextFormat("Joined %s", TimeAgo(me->created)), sx, sy + 176, FS, UI_MUTED);

    int myRepos = 0, myCommits = 0, myStars = 0, myIssues = 0;
    for (int i = 0; i < repoCount; i++) {
        Repo *r = &repos[i];
        if (r->owner == curUser) { myRepos++; myIssues += OpenIssues(r); }
        if (HasStarred(r, curUser)) myStars++;
        for (int k = 0; k < r->commitCount; k++) if (r->commits[k].author == curUser) myCommits++;
    }
    DrawLine(sx, sy + 214, sx + 260, sy + 214, UI_BORDER);
    DrawText(TextFormat("%d repositor%s",  myRepos,   myRepos == 1 ? "y" : "ies"), sx, sy + 230, FS, UI_TEXT);
    DrawText(TextFormat("%d commit%s",     myCommits, myCommits == 1 ? "" : "s"),  sx, sy + 260, FS, UI_TEXT);
    DrawText(TextFormat("%d starred",      myStars),                                sx, sy + 290, FS, UI_TEXT);
    DrawText(TextFormat("%d open issue%s", myIssues,  myIssues == 1 ? "" : "s"),    sx, sy + 320, FS, UI_TEXT);
    DrawText(TextFormat("%d user%s on this computer", userCount, userCount == 1 ? "" : "s"), sx, sy + 366, FS, UI_MUTED);

    /* main column */
    float mx = 320, mw = (float)W - 340;
    long long hoverDay = 0;
    DrawContributions((Rectangle){mx, 72, mw, 160}, curUser, &hoverDay);
    if (screen != SCR_HOME) return;                 /* a day was clicked */

    if (ButtonEx((Rectangle){mx, 248, 100, 40}, "Yours", homeView == 0 ? UI_BLUE : UI_BTN, true))       { homeView = 0; scHome = 0; }
    if (ButtonEx((Rectangle){mx + 106, 248, 120, 40}, "Everyone", homeView == 1 ? UI_BLUE : UI_BTN, true)) { homeView = 1; scHome = 0; }
    TextFieldUI(&tfSearch, (Rectangle){mx + 240, 248, mw - 240 - 130, 40}, "Find a repository...");
    if (Button((Rectangle){mx + mw - 120, 248, 120, 40}, "New", UI_GREEN)) {
        Unfocus();
        TF_Clear(&tfName);
        TF_Clear(&tfDesc);
        optReadme = true;
        screen = SCR_NEW;
        tfName.active = true;
        return;
    }

    /* which repos to list: filter, then newest activity first */
    int order[MAX_REPOS], n = 0;
    for (int i = 0; i < repoCount; i++) {
        Repo *r = &repos[i];
        if (homeView == 0 && r->owner != curUser) continue;
        if (!ContainsCI(r->name, tfSearch.buf) && !ContainsCI(UserName(r->owner), tfSearch.buf)) continue;
        order[n++] = i;
    }
    for (int a = 0; a < n; a++)
        for (int b = a + 1; b < n; b++)
            if (LastActivity(&repos[order[b]]) > LastActivity(&repos[order[a]])) { int t = order[a]; order[a] = order[b]; order[b] = t; }

    Rectangle la = {mx, 302, mw, (float)H - 322};
    float rowH = 110;
    ScrollArea(la, &scHome, n * rowH - 10);
    if (n == 0) {
        const char *m = tfSearch.len ? "No repositories match your search."
                      : homeView == 0 ? "You have no repositories yet - press New to make one."
                                      : "Nobody has made a repository yet.";
        DrawText(m, (int)la.x, (int)la.y + 10, FS, UI_MUTED);
    }

    BeginScissorMode((int)la.x, (int)la.y, (int)la.width, (int)la.height);
    for (int k = 0; k < n; k++) {
        Repo *r = &repos[order[k]];
        Rectangle card = {la.x, la.y + k * rowH - scHome, la.width - 12, rowH - 10};
        if (card.y > la.y + la.height || card.y + card.height < la.y) continue;
        bool hot = Hover(la) && Hover(card);
        if (hot) wantCursor = MOUSE_CURSOR_POINTING_HAND;
        DrawRectangleRec(card, hot ? UI_HOVER : UI_PANEL);
        DrawRectangleLinesEx(card, 1, UI_BORDER);

        int tx = (int)card.x + 18;
        if (homeView == 1) {                       /* show "owner / name" when browsing everyone */
            const char *own = TextFormat("%s / ", UserName(r->owner));
            DrawText(own, tx, (int)card.y + 14, FS, UI_LINK);
            tx += MeasureText(own, FS);
        }
        DrawBold(r->name, tx, (int)card.y + 14, FS, UI_LINK);
        tx += MeasureText(r->name, FS) + 16;
        if (HasStarred(r, curUser)) DrawText("starred", tx, (int)card.y + 14, FS, UI_YELLOW);

        DrawFit(r->desc[0] ? r->desc : "No description", (int)card.x + 18, (int)card.y + 42, FS, (int)card.width - 36, UI_MUTED);
        DrawFit(TextFormat("%d commit%s   |   %d open issue%s   |   %d star%s   |   updated %s",
                           r->commitCount, r->commitCount == 1 ? "" : "s",
                           OpenIssues(r), OpenIssues(r) == 1 ? "" : "s",
                           r->starCount, r->starCount == 1 ? "" : "s",
                           TimeAgo(LastActivity(r))),
                (int)card.x + 18, (int)card.y + 70, FS, (int)card.width - 36, UI_MUTED);
        if (Hover(la) && Clicked(card)) OpenRepo(order[k]);
    }
    EndScissorMode();
    DrawScrollbar(la, scHome, n * rowH - 10);

    if (hoverDay) DrawDayTooltip(hoverDay);          /* last, so it sits on top of everything */
}

static void TryCreateRepo(void)
{
    char nm[NAME_LEN];
    SanitizeName(tfName.buf, nm);
    if (!nm[0]) { Toast("Please give the repository a name"); return; }
    for (int i = 0; i < repoCount; i++)
        if (repos[i].owner == curUser && EqualsCI(repos[i].name, nm)) { Toast("You already have a repository with that name"); return; }
    Repo *r = NewRepo(curUser, nm, tfDesc.buf);
    if (!r) { Toast("This computer has reached the maximum number of repositories"); return; }
    if (optReadme) {
        AddFile(r, "README.md", TextFormat("# %s\n\n%s\n", nm, tfDesc.buf[0] ? tfDesc.buf : "A new project."));
        DoCommit(r, "Initial commit");
    }
    MarkDirty();
    OpenRepo(repoCount - 1);
    Toast(TextFormat("Created %s", nm));
}

static void ScreenNew(void)
{
    TextField *order[] = {&tfName, &tfDesc};
    TabCycle(order, 2);

    Rectangle p = {(float)W / 2 - 310, 100, 620, 430};
    DrawRectangleRec(p, UI_PANEL);
    DrawRectangleLinesEx(p, 1, UI_BORDER);
    int x = (int)p.x + 30, y = (int)p.y;

    DrawBold("Create a new repository", x, y + 28, 30, UI_TEXT);
    DrawText(TextFormat("Owner: %s", UserName(curUser)), x, y + 70, FS, UI_MUTED);

    DrawText("Repository name", x, y + 116, FS, UI_TEXT);
    bool e1 = TextFieldUI(&tfName, (Rectangle){(float)x, (float)y + 142, 560, 40}, "e.g. my-project");
    DrawText("Description (optional)", x, y + 198, FS, UI_TEXT);
    bool e2 = TextFieldUI(&tfDesc, (Rectangle){(float)x, (float)y + 224, 560, 40}, "What is this project about?");
    Checkbox(&optReadme, x, y + 287, "Add a README file");

    bool create = Button((Rectangle){p.x + p.width - 230, (float)y + 350, 200, 44}, "Create repository", UI_GREEN);
    bool cancel = Button((Rectangle){p.x + p.width - 360, (float)y + 350, 120, 44}, "Cancel", UI_BTN);
    if (cancel) { GoHome(); return; }
    if (create || e1 || e2) TryCreateRepo();
}

/* ---------------- repository pages ---------------- */

/* Returns true if the repository was deleted or we left it (so stop drawing it). */
static bool DrawRepoHeader(Repo *r, int activeTab)
{
    int x = 20, y = 72;
    const char *owner = UserName(r->owner);
    int w1 = MeasureText(owner, 30), w2 = MeasureText(" / ", 30), w3 = MeasureText(r->name, 30);
    Rectangle ownerHit = {(float)x, (float)y, (float)w1, 30};
    if (Hover(ownerHit)) wantCursor = MOUSE_CURSOR_POINTING_HAND;
    DrawText(owner, x, y, 30, UI_LINK);
    DrawText(" / ", x + w1, y, 30, UI_MUTED);
    DrawBold(r->name, x + w1 + w2, y, 30, UI_TEXT);
    bool mine = IsMine(r);
    const char *badge = mine ? "Yours" : "Read-only";
    Rectangle pill = {(float)(x + w1 + w2 + w3 + 16), (float)y + 2, (float)MeasureText(badge, FS) + 20, 26};
    DrawRectangleRounded(pill, 0.5f, 6, UI_BTN);
    DrawText(badge, (int)pill.x + 10, (int)pill.y + 3, FS, UI_MUTED);
    DrawFit(r->desc[0] ? r->desc : "No description", x, y + 40, FS, W - 400, UI_MUTED);
    if (Clicked(ownerHit)) { GoHome(); return true; }

    bool starred = HasStarred(r, curUser);
    if (Button((Rectangle){(float)W - 170, (float)y, 150, 38},
               TextFormat("%s  %d", starred ? "Starred" : "Star", r->starCount),
               starred ? (Color){110, 80, 20, 255} : UI_BTN)) {
        ToggleStar(r, curUser);
        MarkDirty();
    }
    if (mine && ConfirmButton((Rectangle){(float)W - 360, (float)y, 180, 38}, "Delete repo", "Really delete?", 1)) {
        int i = curRepo;
        GoHome();
        DeleteRepo(i);
        MarkDirty();
        Toast("Repository deleted");
        return true;
    }

    int tx = 20;
    for (int t = 0; t < 3; t++) {
        const char *label = t == 0 ? TextFormat("Code  %d", ShownCount(r))
                          : t == 1 ? TextFormat("Commits  %d", r->commitCount)
                                   : TextFormat("Issues  %d", OpenIssues(r));
        int w = MeasureText(label, FS) + 28;
        Rectangle tr = {(float)tx, 132, (float)w, 38};
        bool on = (t == activeTab);
        if (Hover(tr)) {
            wantCursor = MOUSE_CURSOR_POINTING_HAND;
            if (!on) DrawRectangleRounded((Rectangle){(float)tx, 134, (float)w, 30}, 0.3f, 6, UI_HOVER);
        }
        DrawText(label, tx + 14, 140, FS, on ? UI_TEXT : UI_MUTED);
        if (on) DrawRectangle(tx, 167, w, 3, UI_ORANGE);
        if (Clicked(tr)) { Unfocus(); tab = (Tab)t; screen = SCR_REPO; }
        tx += w + 6;
    }
    DrawLine(0, 170, W, 170, UI_BORDER);
    return false;
}

/* File list on the left. Names come from the working files (yours) or the latest commit (theirs). */
static void DrawFileList(Repo *r, Rectangle la)
{
    bool mine = IsMine(r);
    int n = ShownCount(r);
    float rowH = 30;
    ScrollArea(la, &scFiles, n * rowH);
    if (!n) DrawText(mine ? "No files yet" : "Nothing committed yet", (int)la.x + 14, (int)la.y + 6, FS, UI_MUTED);
    BeginScissorMode((int)la.x, (int)la.y, (int)la.width, (int)la.height);
    for (int i = 0; i < n; i++) {
        Rectangle row = {la.x, la.y + i * rowH - scFiles, la.width, rowH};
        bool hot = Hover(la) && Hover(row);
        if (i == curFile) DrawRectangleRec(row, (Color){56, 139, 253, 40});
        else if (hot)     DrawRectangleRec(row, UI_HOVER);
        if (hot) wantCursor = MOUSE_CURSOR_POINTING_HAND;
        const char *name = mine ? r->files[i].name : Head(r)->names[i];
        DrawFit(name, (int)row.x + 14, (int)row.y + 5, FS, (int)row.width - 54, UI_TEXT);
        if (mine) {
            char st = FileStatus(r, i);
            if (st) DrawText(st == 'A' ? "A" : "M", (int)(row.x + row.width - 28), (int)row.y + 5, FS, st == 'A' ? UI_GREENT : UI_YELLOW);
        }
        if (Hover(la) && Clicked(row)) SelectFile(i);
    }
    EndScissorMode();
    DrawScrollbar(la, scFiles, n * rowH);
}

static void DrawFileHeader(Rectangle fh, const char *name, const char *tag, Color tagColor, WorkFile *f)
{
    DrawRectangleRec(fh, UI_PANEL);
    DrawRectangleLinesEx(fh, 1, UI_BORDER);
    DrawBold(name, (int)fh.x + 14, (int)fh.y + 11, FS, UI_TEXT);
    if (tag) DrawText(tag, (int)fh.x + 30 + MeasureText(name, FS), (int)fh.y + 11, FS, tagColor);
    int ls = LineStart(f->text, ed.cursor);
    const char *info = TextFormat("Ln %d, Col %d   |   %d lines",
                                  LineOf(f->text, ed.cursor) + 1, ed.cursor - ls + 1, LineOf(f->text, f->len) + 1);
    DrawText(info, (int)(fh.x + fh.width) - 14 - MeasureText(info, FS), (int)fh.y + 11, FS, UI_MUTED);
}

static void TabCode(Repo *r)
{
    bool mine = IsMine(r);
    float top = 186, bottom = (float)H - 20;

    /* ---- left: file list ---- */
    Rectangle lp = {20, top, 260, bottom - top};
    DrawRectangleRec(lp, UI_PANEL);
    DrawRectangleLinesEx(lp, 1, UI_BORDER);
    DrawBold("Files", (int)lp.x + 14, (int)top + 12, FS, UI_TEXT);
    DrawFileList(r, (Rectangle){lp.x + 1, top + 44, lp.width - 2, lp.height - 44 - (mine ? 156 : 4)});

    if (mine) {
        float by = lp.y + lp.height - 148;
        DrawLine((int)lp.x, (int)by - 10, (int)(lp.x + lp.width), (int)by - 10, UI_BORDER);
        bool addEnter = TextFieldUI(&tfNewFile, (Rectangle){lp.x + 12, by, lp.width - 24, 38}, "new-file.txt");
        if (Button((Rectangle){lp.x + 12, by + 46, 114, 36}, "Add file", UI_GREEN) || addEnter) {
            char nm[NAME_LEN];
            SanitizeName(tfNewFile.buf, nm);
            if (!nm[0])                    Toast("Type a file name first, e.g. notes.txt");
            else if (FindFile(r, nm) >= 0) Toast("A file with that name already exists");
            else {
                int idx = AddFile(r, nm, "");
                if (idx < 0) Toast("This repository can't hold any more files");
                else {
                    SelectFile(idx);
                    Unfocus();
                    ed.focused = true;
                    TF_Clear(&tfNewFile);
                    MarkDirty();
                }
            }
        }
        if (curFile >= 0 && ConfirmButton((Rectangle){lp.x + lp.width - 126, by + 46, 114, 36}, "Delete", "Sure?", 3)) {
            int was = curFile;
            RemoveFile(r, was);
            SelectFile(r->fileCount == 0 ? -1 : (was < r->fileCount ? was : r->fileCount - 1));
            MarkDirty();
            Toast("File deleted - commit to record it in the history");
        }
        int nch = ChangeCount(r);
        DrawText(nch ? TextFormat("%d change%s to commit", nch, nch == 1 ? "" : "s") : "All changes committed",
                 (int)lp.x + 14, (int)by + 100, FS, nch ? UI_YELLOW : UI_MUTED);
    }

    /* ---- right: editor (or read-only viewer) + bottom bar ---- */
    Rectangle rp  = {300, top, (float)W - 320, bottom - top};
    Rectangle fh  = {rp.x, rp.y, rp.width, 42};
    Rectangle edr = {rp.x, rp.y + 42, rp.width, rp.height - 42 - 62};
    Rectangle cb  = {rp.x, rp.y + rp.height - 48, rp.width, 48};

    if (curFile >= 0 && curFile < ShownCount(r)) {
        if (mine) {
            WorkFile *f = &r->files[curFile];
            if (EditorUI(f, edr, false)) MarkDirty();
            DrawRectangleLinesEx(edr, 1, UI_BORDER);
            char st = FileStatus(r, curFile);
            DrawFileHeader(fh, f->name, st == 'A' ? "new file" : st == 'M' ? "modified" : NULL,
                           st == 'A' ? UI_GREENT : UI_YELLOW, f);
        } else {
            /* show the committed version through a temporary, never-modified WorkFile */
            static WorkFile view;
            Commit *h = Head(r);
            snprintf(view.name, NAME_LEN, "%s", h->names[curFile]);
            view.text = (char *)BlobText(r, h->blobs[curFile]);
            view.len  = (int)strlen(view.text);
            view.cap  = 0;
            EditorUI(&view, edr, true);
            DrawRectangleLinesEx(edr, 1, UI_BORDER);
            DrawFileHeader(fh, view.name, TextFormat("as of commit %s", h->id), UI_MUTED, &view);
        }
    } else {
        Rectangle e = {rp.x, rp.y, rp.width, rp.height - 62};
        DrawRectangleRec(e, UI_PANEL);
        DrawRectangleLinesEx(e, 1, UI_BORDER);
        const char *m = mine ? "Pick a file on the left, or add a new one." : "This repository has no committed files yet.";
        DrawText(m, (int)(e.x + e.width / 2) - MeasureText(m, FS) / 2, (int)(e.y + e.height / 2) - 10, FS, UI_MUTED);
    }

    if (!mine) {
        const char *o = UserName(r->owner);
        DrawFit(TextFormat("You're viewing %s's latest commit. Only %s can change this repository.", o, o),
                (int)cb.x, (int)cb.y + 14, FS, (int)cb.width, UI_MUTED);
        return;
    }
    bool enter = TextFieldUI(&tfCommit, (Rectangle){cb.x, cb.y + 4, cb.width - 156, 42}, "Describe your changes (the commit message)");
    if (Button((Rectangle){cb.x + cb.width - 146, cb.y + 4, 146, 42}, "Commit", UI_GREEN) || enter) {
        const char *err = DoCommit(r, tfCommit.buf);
        if (err) Toast(err);
        else {
            Toast(TextFormat("Committed %s", Head(r)->id));
            TF_Clear(&tfCommit);
            tfCommit.active = false;
            MarkDirty();
        }
    }
}

static void TabCommits(Repo *r)
{
    Rectangle la = {20, 186, (float)W - 40, (float)H - 206};
    int n = r->commitCount;
    float rowH = 74;
    ScrollArea(la, &scCommits, n * rowH);
    if (!n) {
        DrawText("No commits yet.", 20, 196, FS, UI_TEXT);
        if (IsMine(r)) DrawText("Edit some files in the Code tab, write a message, then press Commit.", 20, 226, FS, UI_MUTED);
        return;
    }
    BeginScissorMode((int)la.x, (int)la.y, (int)la.width, (int)la.height);
    for (int k = 0; k < n; k++) {
        int ci = n - 1 - k;
        Commit *c = &r->commits[ci];
        Rectangle row = {la.x, la.y + k * rowH - scCommits, la.width - 12, rowH};
        if (row.y > la.y + la.height || row.y + row.height < la.y) continue;
        bool hot = Hover(la) && Hover(row);
        if (hot) wantCursor = MOUSE_CURSOR_POINTING_HAND;
        DrawRectangleRec(row, hot ? UI_HOVER : UI_PANEL);
        DrawRectangleLinesEx(row, 1, UI_BORDER);
        DrawAvatar(c->author, (int)row.x + 36, (int)row.y + 37, 18);
        DrawFit(c->message, (int)row.x + 70, (int)row.y + 14, FS, (int)row.width - 310, UI_TEXT);
        DrawText(TextFormat("%s committed %s", UserName(c->author), TimeAgo(c->time)), (int)row.x + 70, (int)row.y + 42, FS, UI_MUTED);

        Rectangle idPill = {row.x + row.width - 120, row.y + 20, 102, 34};
        DrawRectangleRounded(idPill, 0.3f, 6, UI_BTN);
        DrawText(c->id, (int)idPill.x + 12, (int)idPill.y + 7, FS, UI_LINK);
        if (ci == n - 1) {
            Rectangle latest = {row.x + row.width - 220, row.y + 22, 88, 30};
            DrawRectangleRounded(latest, 0.5f, 6, (Color){35, 134, 54, 90});
            DrawText("latest", (int)latest.x + 14, (int)latest.y + 5, FS, UI_GREENT);
        }
        if (Hover(la) && Clicked(row)) OpenCommit(ci);
    }
    EndScissorMode();
    DrawScrollbar(la, scCommits, n * rowH);
}

static void TabIssues(Repo *r)
{
    bool enter = TextFieldUI(&tfIssue, (Rectangle){20, 186, (float)W - 200, 42}, "Title for a new issue (a bug, an idea, a to-do...)");
    if (Button((Rectangle){(float)W - 170, 186, 150, 42}, "New issue", UI_GREEN) || enter) {
        if (!tfIssue.buf[0])                  Toast("Give the issue a title first");
        else if (r->issueCount >= MAX_ISSUES) Toast("This repository has too many issues");
        else {
            Issue *is = &r->issues[r->issueCount++];
            snprintf(is->title, TEXT_LEN, "%s", tfIssue.buf);
            is->open   = true;
            is->time   = (long long)time(NULL);
            is->author = curUser;
            TF_Clear(&tfIssue);
            MarkDirty();
            Toast(TextFormat("Opened issue #%d", r->issueCount));
        }
    }

    int open = OpenIssues(r);
    DrawText(TextFormat("%d open    %d closed", open, r->issueCount - open), 20, 244, FS, UI_TEXT);

    /* open issues first, newest first within each group */
    int order[MAX_ISSUES], n = 0;
    for (int pass = 0; pass < 2; pass++)
        for (int i = r->issueCount - 1; i >= 0; i--)
            if (r->issues[i].open == (pass == 0)) order[n++] = i;

    Rectangle la = {20, 276, (float)W - 40, (float)H - 296};
    float rowH = 68;
    ScrollArea(la, &scIssues, n * rowH);
    if (!n) DrawText("No issues yet. Use issues to keep track of bugs and ideas.", 20, 286, FS, UI_MUTED);

    BeginScissorMode((int)la.x, (int)la.y, (int)la.width, (int)la.height);
    for (int k = 0; k < n; k++) {
        int i = order[k];
        Issue *is = &r->issues[i];
        Rectangle row = {la.x, la.y + k * rowH - scIssues, la.width - 12, rowH};
        if (row.y > la.y + la.height || row.y + row.height < la.y) continue;
        DrawRectangleRec(row, UI_PANEL);
        DrawRectangleLinesEx(row, 1, UI_BORDER);

        Rectangle pill = {row.x + 16, row.y + 19, 90, 30};
        DrawRectangleRounded(pill, 0.5f, 6, is->open ? UI_GREEN : UI_PURPLE);
        const char *state = is->open ? "Open" : "Closed";
        DrawText(state, (int)(pill.x + pill.width / 2) - MeasureText(state, FS) / 2, (int)pill.y + 5, FS, UI_TEXT);

        DrawFit(is->title, (int)row.x + 124, (int)row.y + 12, FS, (int)row.width - 290, is->open ? UI_TEXT : UI_MUTED);
        DrawText(TextFormat("#%d opened %s by %s", i + 1, TimeAgo(is->time), UserName(is->author)),
                 (int)row.x + 124, (int)row.y + 38, FS, UI_MUTED);

        /* only the repo owner or whoever opened the issue may close / reopen it */
        bool canManage = IsMine(r) || is->author == curUser;
        if (canManage &&
            ButtonEx((Rectangle){row.x + row.width - 140, row.y + 16, 124, 36}, is->open ? "Close" : "Reopen", UI_BTN, Hover(la))) {
            is->open = !is->open;
            MarkDirty();
        }
    }
    EndScissorMode();
    DrawScrollbar(la, scIssues, n * rowH);
}

static void DrawDiff(Rectangle area)
{
    DrawRectangleRec(area, UI_BG);
    DrawRectangleLinesEx(area, 1, UI_BORDER);
    ScrollArea(area, &scDiff, dOpCount * LH + 16);
    if (dOpCount == 0) { DrawText("(empty file)", (int)area.x + 16, (int)area.y + 12, FS, UI_MUTED); return; }

    BeginScissorMode((int)area.x + 1, (int)area.y + 1, (int)area.width - 2, (int)area.height - 2);
    for (int i = 0; i < dOpCount; i++) {
        float y = area.y + 8 + i * LH - scDiff;
        if (y > area.y + area.height) break;
        if (y + LH < area.y) continue;
        DiffOp *o = &dOps[i];
        Color bg = o->kind == '+' ? (Color){46, 160, 67, 45} : o->kind == '-' ? (Color){248, 81, 73, 45} : BLANK;
        Color fg = o->kind == '+' ? UI_GREENT : o->kind == '-' ? UI_REDT : UI_TEXT;
        DrawRectangle((int)area.x, (int)y - 2, (int)area.width, LH, bg);
        if (o->a >= 0) DrawText(TextFormat("%d", o->a + 1), (int)area.x + 10, (int)y, FS, UI_MUTED);
        if (o->b >= 0) DrawText(TextFormat("%d", o->b + 1), (int)area.x + 62, (int)y, FS, UI_MUTED);
        char sign[2] = {o->kind, 0};
        DrawText(sign, (int)area.x + 118, (int)y, FS, fg);
        LineRef lr = o->b >= 0 ? dB[o->b] : dA[o->a];
        DrawText(Slice(lr.s, lr.n), (int)area.x + 140, (int)y, FS, o->kind == ' ' ? UI_TEXT : fg);
    }
    EndScissorMode();
    DrawScrollbar(area, scDiff, dOpCount * LH + 16);
}

static void ScreenCommit(Repo *r)
{
    Commit *c = &r->commits[curCommit];

    if (Button((Rectangle){20, 186, 170, 38}, fromDay ? "< Back to day" : "< All commits", UI_BTN)) {
        Unfocus();
        if (fromDay) screen = SCR_DAY;
        else { screen = SCR_REPO; tab = TAB_COMMITS; }
        return;
    }
    if (IsMine(r) &&
        ConfirmButton((Rectangle){(float)W - 330, 186, 310, 38}, "Restore files from here", "Click again to confirm", 2)) {
        RestoreCommit(r, curCommit);
        SelectFile(r->fileCount ? 0 : -1);
        MarkDirty();
        Toast(TextFormat("Files restored to %s - commit them to keep this version", c->id));
        Unfocus();
        screen = SCR_REPO;
        tab = TAB_CODE;
        return;
    }

    DrawFit(c->message, 20, 238, 30, W - 40, UI_TEXT);
    const char *meta = TextFormat("%s committed %s   |   commit %s   |   %d file%s changed   ",
                                  UserName(c->author), TimeAgo(c->time), c->id, changeCount, changeCount == 1 ? "" : "s");
    DrawText(meta, 20, 276, FS, UI_MUTED);
    int mx = 20 + MeasureText(meta, FS);
    const char *plus = TextFormat("+%d ", totalAdds);
    DrawText(plus, mx, 276, FS, UI_GREENT);
    DrawText(TextFormat("-%d", totalDels), mx + MeasureText(plus, FS) + 4, 276, FS, UI_REDT);

    float top = 314, bottom = (float)H - 20;

    /* left: changed files */
    Rectangle lp = {20, top, 260, bottom - top};
    DrawRectangleRec(lp, UI_PANEL);
    DrawRectangleLinesEx(lp, 1, UI_BORDER);
    DrawBold("Files changed", (int)lp.x + 14, (int)top + 12, FS, UI_TEXT);
    Rectangle la = {lp.x + 1, top + 44, lp.width - 2, lp.height - 46};
    float rowH = 30;
    ScrollArea(la, &scChanges, changeCount * rowH);
    BeginScissorMode((int)la.x, (int)la.y, (int)la.width, (int)la.height);
    for (int i = 0; i < changeCount; i++) {
        Rectangle row = {la.x, la.y + i * rowH - scChanges, la.width, rowH};
        bool hot = Hover(la) && Hover(row);
        if (i == curChange) DrawRectangleRec(row, (Color){56, 139, 253, 40});
        else if (hot)       DrawRectangleRec(row, UI_HOVER);
        if (hot) wantCursor = MOUSE_CURSOR_POINTING_HAND;
        Color kc = changes[i].kind == 'A' ? UI_GREENT : changes[i].kind == 'D' ? UI_REDT : UI_YELLOW;
        char k[2] = {changes[i].kind, 0};
        DrawText(k, (int)row.x + 14, (int)row.y + 5, FS, kc);
        DrawFit(changes[i].name, (int)row.x + 38, (int)row.y + 5, FS, (int)row.width - 52, UI_TEXT);
        if (Hover(la) && Clicked(row)) SelectChange(i);
    }
    EndScissorMode();

    /* right: the diff */
    Rectangle rp = {300, top, (float)W - 320, bottom - top};
    if (curChange < 0) {
        DrawRectangleRec(rp, UI_PANEL);
        DrawRectangleLinesEx(rp, 1, UI_BORDER);
        DrawText("This commit didn't change any files.", (int)rp.x + 16, (int)rp.y + 16, FS, UI_MUTED);
        return;
    }
    Change *ch = &changes[curChange];
    Rectangle fh = {rp.x, rp.y, rp.width, 42};
    DrawRectangleRec(fh, UI_PANEL);
    DrawRectangleLinesEx(fh, 1, UI_BORDER);
    DrawBold(ch->name, (int)fh.x + 14, (int)fh.y + 11, FS, UI_TEXT);
    const char *kind = ch->kind == 'A' ? "added" : ch->kind == 'D' ? "deleted" : "modified";
    DrawText(kind, (int)fh.x + 30 + MeasureText(ch->name, FS), (int)fh.y + 11, FS, UI_MUTED);
    const char *ad = TextFormat("+%d  -%d", ch->adds, ch->dels);
    DrawText(ad, (int)(fh.x + fh.width) - 14 - MeasureText(ad, FS), (int)fh.y + 11, FS, UI_MUTED);
    DrawDiff((Rectangle){rp.x, rp.y + 42, rp.width, rp.height - 42});
}

/* ---------------- one day of contributions ---------------- */
static void ScreenDay(void)
{
    long long today = DayStart((long long)time(NULL));

    if (Button((Rectangle){20, 72, 110, 38}, "< Back", UI_BTN)) { GoHome(); return; }
    if (Button((Rectangle){(float)W - 380, 72, 175, 38}, "< Earlier day", UI_BTN)) { curDay = AddDays(curDay, -1); scDay = scDayRepos = 0; }
    bool canLater = curDay < today;
    if (ButtonEx((Rectangle){(float)W - 195, 72, 175, 38}, "Later day >", canLater ? UI_BTN : UI_PANEL, canLater)) {
        curDay = AddDays(curDay, 1);
        scDay = scDayRepos = 0;
    }

    char title[96];
    snprintf(title, sizeof title, "%s%s", FormatDay(curDay, true), curDay == today ? "  (today)" : "");
    DrawFit(title, 150, 75, 30, W - 560, UI_TEXT);

    GatherDay(curUser, curDay);

    /* one-line summary */
    char b1[48], b2[48];
    int sy = 124;
    if (actTotal == 0) {
        DrawText("You didn't make any commits or open any issues on this day.", 20, sy, FS, UI_MUTED);
    } else {
        const char *s = TextFormat("%s in %s   |   ", Count(b1, actTotal, "contribution", "contributions"),
                                   Count(b2, tallyCount, "repository", "repositories"));
        DrawText(s, 20, sy, FS, UI_TEXT);
        int x = 20 + MeasureText(s, FS);
        s = TextFormat("%s, %s changed   ", Count(b1, dayCommits, "commit", "commits"), Count(b2, dayFiles, "file", "files"));
        DrawText(s, x, sy, FS, UI_MUTED);
        x += MeasureText(s, FS);
        x += DrawAddsDels(dayAdds, dayDels, x, sy);
        if (dayIssues) DrawText(TextFormat("   |   %s", Count(b1, dayIssues, "issue opened", "issues opened")), x, sy, FS, UI_MUTED);
    }
    DrawLine(0, 160, W, 160, UI_BORDER);

    float top = 176, bottom = (float)H - 20;

    /* ---- left: breakdown by repository ---- */
    Rectangle lp = {20, top, 300, bottom - top};
    DrawRectangleRec(lp, UI_PANEL);
    DrawRectangleLinesEx(lp, 1, UI_BORDER);
    DrawBold("By repository", (int)lp.x + 14, (int)top + 12, FS, UI_TEXT);
    Rectangle la = {lp.x + 1, top + 44, lp.width - 2, lp.height - 46};
    float rh = 84;
    ScrollArea(la, &scDayRepos, tallyCount * rh);
    if (!tallyCount) DrawText("Nothing here", (int)la.x + 14, (int)la.y + 6, FS, UI_MUTED);
    BeginScissorMode((int)la.x, (int)la.y, (int)la.width, (int)la.height);
    for (int i = 0; i < tallyCount; i++) {
        RepoTally *t = &tallies[i];
        Rectangle row = {la.x, la.y + i * rh - scDayRepos, la.width, rh};
        bool hot = Hover(la) && Hover(row);
        if (hot) { DrawRectangleRec(row, UI_HOVER); wantCursor = MOUSE_CURSOR_POINTING_HAND; }
        if (i) DrawLine((int)row.x + 12, (int)row.y, (int)(row.x + row.width - 12), (int)row.y, UI_BORDER);
        DrawFit(RepoLabel(t->repo), (int)row.x + 14, (int)row.y + 10, FS, (int)row.width - 28, UI_LINK);
        DrawText(TallyText(t->commits, t->issues), (int)row.x + 14, (int)row.y + 34, FS, UI_MUTED);
        if (t->commits) DrawAddsDels(t->adds, t->dels, (int)row.x + 14, (int)row.y + 56);
        if (Hover(la) && Clicked(row)) { OpenRepo(t->repo); EndScissorMode(); return; }
    }
    EndScissorMode();
    DrawScrollbar(la, scDayRepos, tallyCount * rh);

    /* ---- right: every contribution, in the order it happened ---- */
    Rectangle ra = {340, top, (float)W - 360, bottom - top};
    if (actTotal == 0) {
        DrawRectangleRec(ra, UI_PANEL);
        DrawRectangleLinesEx(ra, 1, UI_BORDER);
        const char *m = "Use the buttons at the top to look at other days.";
        DrawText(m, (int)(ra.x + ra.width / 2) - MeasureText(m, FS) / 2, (int)(ra.y + ra.height / 2) - 10, FS, UI_MUTED);
        return;
    }

    /* work out each card's height first, so the list can scroll */
    static float cardY[MAX_ACTS], cardH[MAX_ACTS];
    static char        kinds[MAX_FILES * 2];
    static const char *names[MAX_FILES * 2];
    float total = 0;
    for (int i = 0; i < actCount; i++) {
        Activity *a = &acts[i];
        int nf = a->kind == 'C' ? CommitFileList(&repos[a->repo], a->idx, kinds, names) : 0;
        cardY[i] = total;
        cardH[i] = 76 + (nf ? nf * 24 + 8 : 0);
        total += cardH[i] + 10;
    }
    float extra = actCount < actTotal ? 40 : 0;
    ScrollArea(ra, &scDay, total + extra);

    BeginScissorMode((int)ra.x, (int)ra.y, (int)ra.width, (int)ra.height);
    for (int i = 0; i < actCount; i++) {
        Activity *a = &acts[i];
        Repo *r = &repos[a->repo];
        Rectangle card = {ra.x, ra.y + cardY[i] - scDay, ra.width - 12, cardH[i]};
        if (card.y > ra.y + ra.height || card.y + card.height < ra.y) continue;
        bool hot = Hover(ra) && Hover(card);
        if (hot) wantCursor = MOUSE_CURSOR_POINTING_HAND;
        DrawRectangleRec(card, hot ? UI_HOVER : UI_PANEL);
        DrawRectangleLinesEx(card, 1, UI_BORDER);

        /* left column: time and what kind of thing it was */
        char hm[16];
        time_t tt = (time_t)a->time;
        strftime(hm, sizeof hm, "%H:%M", localtime(&tt));
        DrawText(hm, (int)card.x + 18, (int)card.y + 12, FS, UI_MUTED);
        Rectangle pill = {card.x + 14, card.y + 40, 82, 26};
        const char *kind = a->kind == 'C' ? "Commit" : "Issue";
        DrawRectangleRounded(pill, 0.5f, 6, a->kind == 'C' ? UI_GREEN : UI_PURPLE);
        DrawText(kind, (int)(pill.x + pill.width / 2) - MeasureText(kind, FS) / 2, (int)pill.y + 3, FS, UI_TEXT);

        int cx = (int)card.x + 116, cw = (int)card.width - 132;
        if (a->kind == 'C') {
            Commit *c = &r->commits[a->idx];
            CommitStats(r, a->idx);
            DrawFit(c->message, cx, (int)card.y + 12, FS, cw, UI_TEXT);
            const char *meta = TextFormat("%s   |   commit %s   |   %d file%s changed   ",
                                          RepoLabel(a->repo), c->id, c->sFiles, c->sFiles == 1 ? "" : "s");
            DrawText(meta, cx, (int)card.y + 42, FS, UI_MUTED);
            DrawAddsDels(c->sAdds, c->sDels, cx + MeasureText(meta, FS), (int)card.y + 42);

            int nf = CommitFileList(r, a->idx, kinds, names);
            for (int f = 0; f < nf; f++) {
                int fy = (int)card.y + 76 + f * 24;
                char k[2] = {kinds[f], 0};
                Color kc = kinds[f] == 'A' ? UI_GREENT : kinds[f] == 'D' ? UI_REDT : UI_YELLOW;
                DrawText(k, cx, fy, FS, kc);
                DrawFit(names[f], cx + 24, fy, FS, cw - 24, UI_TEXT);
            }
            if (Hover(ra) && Clicked(card)) {
                int repo = a->repo, ci = a->idx;
                EndScissorMode();
                OpenRepo(repo);
                tab = TAB_COMMITS;
                OpenCommit(ci);
                fromDay = true;
                return;
            }
        } else {
            Issue *is = &r->issues[a->idx];
            DrawFit(is->title, cx, (int)card.y + 12, FS, cw, UI_TEXT);
            DrawText(TextFormat("%s   |   opened issue #%d   |   %s", RepoLabel(a->repo), a->idx + 1,
                                is->open ? "still open" : "now closed"),
                     cx, (int)card.y + 42, FS, UI_MUTED);
            if (Hover(ra) && Clicked(card)) {
                int repo = a->repo;
                EndScissorMode();
                OpenRepo(repo);
                tab = TAB_ISSUES;
                return;
            }
        }
    }
    if (extra > 0)
        DrawText(TextFormat("...and %d more not shown", actTotal - actCount), (int)ra.x + 4, (int)(ra.y + total - scDay + 8), FS, UI_MUTED);
    EndScissorMode();
    DrawScrollbar(ra, scDay, total + extra);
}

static void DrawToast(void)
{
    if (toastT <= 0) return;
    float a = toastT < 0.4f ? toastT / 0.4f : 1.0f;
    int w = MeasureText(toastMsg, FS) + 44;
    Rectangle t = {(float)(W - w) / 2, (float)H - 130, (float)w, 46};
    DrawRectangleRounded(t, 0.3f, 8, Fade((Color){48, 54, 61, 255}, a));
    DrawText(toastMsg, (int)t.x + 22, (int)t.y + 13, FS, Fade(UI_TEXT, a));
}

/* ------------------------------------------------------------------ */
/* Main                                                               */
/* ------------------------------------------------------------------ */
int main(void)
{
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT);
    InitWindow(1280, 800, "Mini GitHub");
    SetWindowMinSize(1000, 680);
    SetExitKey(KEY_NULL);            /* Esc goes "back" instead of quitting */
    SetTargetFPS(60);

    Load();
    if (userCount == 0) {
        GoRegister();
        if (OrphanCount() > 0) Toast("Create an account to take over your existing repositories");
    } else GoLogin();

    float saveTimer = 0;
    int   lastCursor = -1;

    while (!WindowShouldClose()) {
        W = GetScreenWidth();
        H = GetScreenHeight();
        clickUsed  = false;
        wantCursor = MOUSE_CURSOR_DEFAULT;
        float dt = GetFrameTime();
        if (toastT > 0) toastT -= dt;
        if (lockT > 0)  lockT  -= dt;
        if (armedId && (armedT -= dt) <= 0) armedId = 0;

        /* every screen except sign-in / sign-up needs someone signed in */
        if (curUser < 0 && screen != SCR_LOGIN && screen != SCR_REGISTER) GoLogin();

        if (IsKeyPressed(KEY_ESCAPE)) {
            bool anyFocus = ed.focused || tfSearch.active || tfName.active || tfDesc.active ||
                            tfNewFile.active || tfCommit.active || tfIssue.active || tfUser.active ||
                            tfPass.active || tfPass2.active || tfOldPass.active || tfNewPass.active ||
                            tfNewPass2.active || tfDelPass.active;
            if (anyFocus) Unfocus();
            else if (screen == SCR_NEW || screen == SCR_REPO || screen == SCR_ACCOUNT || screen == SCR_DAY) GoHome();
            else if (screen == SCR_COMMIT && fromDay) screen = SCR_DAY;
            else if (screen == SCR_COMMIT) { screen = SCR_REPO; tab = TAB_COMMITS; }
        }

        BeginDrawing();
        ClearBackground(UI_BG);
        DrawHeader();

        switch (screen) {
        case SCR_LOGIN:    ScreenLogin();    break;
        case SCR_REGISTER: ScreenRegister(); break;
        case SCR_ACCOUNT:  ScreenAccount();  break;
        case SCR_DAY:      ScreenDay();      break;
        case SCR_HOME:     ScreenHome();     break;
        case SCR_NEW:      ScreenNew();      break;
        case SCR_REPO: {
            Repo *r = &repos[curRepo];
            if (DrawRepoHeader(r, tab)) break;
            if      (tab == TAB_CODE)    TabCode(r);
            else if (tab == TAB_COMMITS) TabCommits(r);
            else                         TabIssues(r);
        } break;
        case SCR_COMMIT: {
            Repo *r = &repos[curRepo];
            if (DrawRepoHeader(r, TAB_COMMITS)) break;
            if (screen == SCR_COMMIT) ScreenCommit(r);
        } break;
        }

        DrawToast();
        EndDrawing();

        if (wantCursor != lastCursor) { SetMouseCursor(wantCursor); lastCursor = wantCursor; }

        saveTimer += dt;
        if (dirty && saveTimer > 0.75f) { Save(); dirty = false; saveTimer = 0; }
    }

    if (dirty) Save();
    ClearSecrets();
    for (int i = 0; i < repoCount; i++) FreeRepo(&repos[i]);
    CloseWindow();
    return 0;
}
