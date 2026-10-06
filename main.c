/*
 * Mini GitHub
 * -----------
 * A tiny GitHub-style app that runs entirely on your own computer.
 *
 *   - Repositories you can create, star and delete
 *   - A built-in text editor for the files in each repository
 *   - Commits: a snapshot of every file, saved with a message
 *   - Commit history and line-by-line diffs (what was added / removed)
 *   - Issues: a simple bug / to-do list per repository
 *   - A contribution graph of your commits over the last six months
 *
 * Everything is saved automatically to "minigithub.dat" in the folder
 * the program is run from.
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
#define MAX_REPOS    32
#define MAX_FILES    32
#define MAX_COMMITS  200
#define MAX_BLOBS    1024
#define MAX_ISSUES   100
#define NAME_LEN     64
#define TEXT_LEN     160
#define MAX_DIFF     1500
#define SAVE_FILE    "minigithub.dat"

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

/* A blob is one unique version of a file's contents. Identical
   contents are stored only once, just like real git. */
typedef struct { uint32_t hash; char *data; } Blob;

/* A commit is a snapshot: a list of file names, each pointing at a blob. */
typedef struct {
    char      id[8];                 /* short hash, e.g. "3fa9c1d" */
    char      message[TEXT_LEN];
    long long time;
    int       fileCount;
    char      names[MAX_FILES][NAME_LEN];
    int       blobs[MAX_FILES];
} Commit;

/* A working file is the version you are editing right now. */
typedef struct { char name[NAME_LEN]; char *text; int len, cap; } WorkFile;

typedef struct { char title[TEXT_LEN]; bool open; long long time; } Issue;

typedef struct {
    char      name[NAME_LEN];
    char      desc[TEXT_LEN];
    bool      starred;
    long long created;
    int       fileCount;   WorkFile files[MAX_FILES];
    int       commitCount; Commit   commits[MAX_COMMITS];
    int       blobCount;   Blob     blobs[MAX_BLOBS];
    int       issueCount;  Issue    issues[MAX_ISSUES];
} Repo;

static Repo repos[MAX_REPOS];
static int  repoCount = 0;

/* ------------------------------------------------------------------ */
/* App state                                                          */
/* ------------------------------------------------------------------ */
typedef enum { SCR_HOME, SCR_NEW, SCR_REPO, SCR_COMMIT } Screen;
typedef enum { TAB_CODE, TAB_COMMITS, TAB_ISSUES } Tab;

typedef struct { char buf[TEXT_LEN]; int len; bool active; } TextField;
typedef struct { int cursor; float scrollY, scrollX; bool focused; } EditorState;

static Screen screen = SCR_HOME;
static Tab    tab    = TAB_CODE;
static int    curRepo = -1, curFile = -1, curCommit = -1, curChange = -1;

static TextField tfSearch, tfName, tfDesc, tfNewFile, tfCommit, tfIssue;
static EditorState ed;
static bool  optReadme = true;
static float scHome, scFiles, scCommits, scIssues, scChanges, scDiff;

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
static Repo *NewRepo(const char *name, const char *desc)
{
    if (repoCount >= MAX_REPOS) return NULL;
    Repo *r = &repos[repoCount++];
    memset(r, 0, sizeof *r);
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
    snprintf(c.message, TEXT_LEN, "%s", msg);
    c.time = (long long)time(NULL);
    h = Fnv(msg, (int)strlen(msg), h);
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
    fprintf(f, "MINIGITHUB1\n");
    WI(f, repoCount);
    for (int i = 0; i < repoCount; i++) {
        Repo *r = &repos[i];
        WS(f, r->name); WS(f, r->desc); WI(f, r->starred); WI(f, r->created);
        WI(f, r->fileCount);
        for (int k = 0; k < r->fileCount; k++) { WS(f, r->files[k].name); WS(f, r->files[k].text); }
        WI(f, r->blobCount);
        for (int k = 0; k < r->blobCount; k++) WS(f, r->blobs[k].data);
        WI(f, r->commitCount);
        for (int k = 0; k < r->commitCount; k++) {
            Commit *c = &r->commits[k];
            WS(f, c->id); WS(f, c->message); WI(f, c->time); WI(f, c->fileCount);
            for (int j = 0; j < c->fileCount; j++) { WS(f, c->names[j]); WI(f, c->blobs[j]); }
        }
        WI(f, r->issueCount);
        for (int k = 0; k < r->issueCount; k++) { WS(f, r->issues[k].title); WI(f, r->issues[k].open); WI(f, r->issues[k].time); }
    }
    fclose(f);
    remove(SAVE_FILE);
    rename(SAVE_FILE ".tmp", SAVE_FILE);
}

static bool Load(void)
{
    FILE *f = fopen(SAVE_FILE, "rb");
    if (!f) return false;
    loadErr = 0;
    char magic[32] = {0};
    if (fscanf(f, "%31s", magic) != 1 || strcmp(magic, "MINIGITHUB1") != 0) loadErr = 1;
    int n = loadErr ? 0 : (int)RI(f);
    if (n < 0 || n > MAX_REPOS) loadErr = 1;

    for (int i = 0; i < n && !loadErr; i++) {
        Repo *r = &repos[repoCount++];
        memset(r, 0, sizeof *r);
        RSInto(f, r->name, NAME_LEN);
        RSInto(f, r->desc, TEXT_LEN);
        r->starred = RI(f) != 0;
        r->created = RI(f);

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
            c->time = RI(f);
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
            is->open = RI(f) != 0;
            is->time = RI(f);
        }
    }
    fclose(f);

    if (loadErr) {
        for (int i = 0; i < repoCount; i++) FreeRepo(&repos[i]);
        memset(repos, 0, sizeof repos);
        repoCount = 0;
        remove(SAVE_FILE ".bak");
        rename(SAVE_FILE, SAVE_FILE ".bak");
        Toast("Save file was damaged - kept a copy as minigithub.dat.bak");
        return false;
    }
    return true;
}

/* First run: an example repository so the app isn't empty. */
static void Seed(void)
{
    Repo *r = NewRepo("hello-world", "My first repository on Mini GitHub");
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
    r->commits[0].time -= 2 * 86400;
    r->created         -= 3 * 86400;

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
    r->commits[1].time -= 3 * 3600;

    Issue *a = &r->issues[r->issueCount++];
    snprintf(a->title, TEXT_LEN, "%s", "Add a LICENSE file");
    a->open = true;  a->time = (long long)time(NULL) - 86400;
    Issue *b = &r->issues[r->issueCount++];
    snprintf(b->title, TEXT_LEN, "%s", "Fix typo in README");
    b->open = false; b->time = (long long)time(NULL) - 2 * 86400;
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

    int tx = (int)r.x + 10, ty = (int)(r.y + (r.height - FS) / 2);
    int textW = MeasureText(t->buf, FS);
    int off = textW > (int)r.width - 30 ? textW - (int)r.width + 30 : 0;
    BeginScissorMode((int)r.x + 2, (int)r.y, (int)r.width - 4, (int)r.height);
    if (t->len == 0) DrawText(placeholder, tx, ty, FS, UI_MUTED);
    else             DrawText(t->buf, tx - off, ty, FS, UI_TEXT);
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

/* Draws and runs the editor. Returns true if the text was changed. */
static bool EditorUI(WorkFile *f, Rectangle area)
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
            if (ch >= 32 && ch < 127) { char c = (char)ch; WF_Insert(f, ed.cursor, &c, 1); ed.cursor++; changed = true; }

        if (KeyRep(KEY_ENTER) || KeyRep(KEY_KP_ENTER)) {          /* new line, keeping indentation */
            int ls = LineStart(f->text, ed.cursor), ind = 0;
            while (ls + ind < ed.cursor && f->text[ls + ind] == ' ' && ind < 100) ind++;
            char buf[128];
            buf[0] = '\n';
            memset(buf + 1, ' ', (size_t)ind);
            WF_Insert(f, ed.cursor, buf, ind + 1);
            ed.cursor += ind + 1;
            changed = true;
        }
        if (KeyRep(KEY_TAB))                          { WF_Insert(f, ed.cursor, "    ", 4); ed.cursor += 4; changed = true; }
        if (KeyRep(KEY_BACKSPACE) && ed.cursor > 0)   { WF_Delete(f, ed.cursor - 1, 1); ed.cursor--; changed = true; }
        if (KeyRep(KEY_DELETE) && ed.cursor < f->len) { WF_Delete(f, ed.cursor, 1); changed = true; }
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
/* Navigation                                                         */
/* ------------------------------------------------------------------ */
static void Unfocus(void)
{
    tfSearch.active = tfName.active = tfDesc.active = false;
    tfNewFile.active = tfCommit.active = tfIssue.active = false;
    ed.focused = false;
    armedId = 0;
}
static void GoHome(void) { Unfocus(); screen = SCR_HOME; curRepo = -1; }

static void OpenRepo(int i)
{
    Unfocus();
    curRepo = i;
    screen  = SCR_REPO;
    tab     = TAB_CODE;
    SelectFile(repos[i].fileCount ? 0 : -1);
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
    screen = SCR_COMMIT;
    BuildChanges(&repos[curRepo], ci);
    scChanges = 0;
    SelectChange(changeCount ? 0 : -1);
}

/* ------------------------------------------------------------------ */
/* Screens                                                            */
/* ------------------------------------------------------------------ */
static void DrawHeader(void)
{
    DrawRectangle(0, 0, W, 56, UI_PANEL);
    DrawLine(0, 56, W, 56, UI_BORDER);

    /* logo: a little "branch" icon */
    DrawCircle(38, 28, 18, UI_TEXT);
    DrawLineEx((Vector2){32, 18}, (Vector2){32, 38}, 3, UI_BG);
    DrawLineEx((Vector2){44, 22}, (Vector2){32, 32}, 3, UI_BG);
    DrawCircle(32, 18, 4, UI_BG);
    DrawCircle(32, 38, 4, UI_BG);
    DrawCircle(44, 22, 4, UI_BG);
    DrawBold("Mini GitHub", 68, 18, FS, UI_TEXT);

    Rectangle home = {18, 8, 200, 40};
    if (Hover(home)) wantCursor = MOUSE_CURSOR_POINTING_HAND;
    if (Clicked(home)) GoHome();

    DrawCircle(W - 36, 28, 16, UI_PURPLE);
    DrawText("Y", W - 41, 19, FS, UI_TEXT);
    DrawText("you", W - 64 - MeasureText("you", FS), 18, FS, UI_MUTED);
}

static Color Heat(int c)
{
    if (c <= 0) return (Color){ 33,  38,  45, 255};
    if (c == 1) return (Color){ 14,  68,  41, 255};
    if (c == 2) return (Color){  0, 109,  50, 255};
    if (c <= 4) return (Color){ 38, 166,  65, 255};
    return             (Color){ 57, 211,  83, 255};
}

static void DrawContributions(Rectangle area)
{
    enum { WEEKS = 26 };
    int counts[WEEKS * 7] = {0}, total = 0;
    time_t now = time(NULL);
    struct tm lt = *localtime(&now);
    int wd = lt.tm_wday;
    lt.tm_hour = lt.tm_min = lt.tm_sec = 0;
    time_t midnight = mktime(&lt);
    int todayPos = (WEEKS - 1) * 7 + wd;

    for (int i = 0; i < repoCount; i++)
        for (int k = 0; k < repos[i].commitCount; k++) {
            long long t = repos[i].commits[k].time;
            int daysAgo = t >= (long long)midnight ? 0 : (int)(((long long)midnight - t - 1) / 86400) + 1;
            int pos = todayPos - daysAgo;
            if (pos >= 0) { counts[pos]++; total++; }
        }

    DrawRectangleRec(area, UI_PANEL);
    DrawRectangleLinesEx(area, 1, UI_BORDER);
    DrawText(TextFormat("%d contribution%s in the last 6 months", total, total == 1 ? "" : "s"),
             (int)area.x + 16, (int)area.y + 12, FS, UI_TEXT);
    for (int pos = 0; pos <= todayPos; pos++) {
        int col = pos / 7, row = pos % 7;
        Rectangle cell = {area.x + 16 + col * 15, area.y + 44 + row * 15, 12, 12};
        DrawRectangleRounded(cell, 0.3f, 4, Heat(counts[pos]));
    }
    int lx = (int)(area.x + area.width) - 200, ly = (int)(area.y + area.height) - 30;
    DrawText("Less", lx, ly, FS, UI_MUTED);
    for (int i = 0; i < 5; i++)
        DrawRectangleRounded((Rectangle){(float)lx + 52 + i * 16, (float)ly + 4, 12, 12}, 0.3f, 4, Heat(i == 4 ? 5 : i));
    DrawText("More", lx + 138, ly, FS, UI_MUTED);
}

static void ScreenHome(void)
{
    /* sidebar */
    int sx = 20, sy = 76;
    DrawCircle(sx + 64, sy + 64, 60, UI_PURPLE);
    DrawText("Y", sx + 64 - MeasureText("Y", 60) / 2, sy + 36, 60, UI_TEXT);
    DrawBold("you", sx, sy + 140, 30, UI_TEXT);
    DrawText("Local developer", sx, sy + 176, FS, UI_MUTED);

    int commits = 0, stars = 0, issues = 0;
    for (int i = 0; i < repoCount; i++) {
        commits += repos[i].commitCount;
        stars   += repos[i].starred;
        issues  += OpenIssues(&repos[i]);
    }
    DrawLine(sx, sy + 214, sx + 260, sy + 214, UI_BORDER);
    DrawText(TextFormat("%d repositor%s",  repoCount, repoCount == 1 ? "y" : "ies"), sx, sy + 230, FS, UI_TEXT);
    DrawText(TextFormat("%d commit%s",     commits,   commits == 1 ? "" : "s"),      sx, sy + 260, FS, UI_TEXT);
    DrawText(TextFormat("%d starred",      stars),                                    sx, sy + 290, FS, UI_TEXT);
    DrawText(TextFormat("%d open issue%s", issues,    issues == 1 ? "" : "s"),        sx, sy + 320, FS, UI_TEXT);

    /* main column */
    float mx = 320, mw = (float)W - 340;
    DrawContributions((Rectangle){mx, 72, mw, 160});

    DrawBold("Repositories", (int)mx, 258, FS, UI_TEXT);
    TextFieldUI(&tfSearch, (Rectangle){mx + 160, 248, mw - 160 - 130, 40}, "Find a repository...");
    if (Button((Rectangle){mx + mw - 120, 248, 120, 40}, "New", UI_GREEN)) {
        Unfocus();
        TF_Clear(&tfName);
        TF_Clear(&tfDesc);
        optReadme = true;
        screen = SCR_NEW;
        tfName.active = true;
        return;
    }

    /* newest activity first, filtered by the search box */
    int order[MAX_REPOS], n = 0;
    for (int i = 0; i < repoCount; i++) if (ContainsCI(repos[i].name, tfSearch.buf)) order[n++] = i;
    for (int a = 0; a < n; a++)
        for (int b = a + 1; b < n; b++)
            if (LastActivity(&repos[order[b]]) > LastActivity(&repos[order[a]])) { int t = order[a]; order[a] = order[b]; order[b] = t; }

    Rectangle la = {mx, 302, mw, (float)H - 322};
    float rowH = 110;
    ScrollArea(la, &scHome, n * rowH - 10);
    if (n == 0)
        DrawText(repoCount ? "No repositories match your search." : "No repositories yet - press New to make one.",
                 (int)la.x, (int)la.y + 10, FS, UI_MUTED);

    BeginScissorMode((int)la.x, (int)la.y, (int)la.width, (int)la.height);
    for (int k = 0; k < n; k++) {
        Repo *r = &repos[order[k]];
        Rectangle card = {la.x, la.y + k * rowH - scHome, la.width - 12, rowH - 10};
        if (card.y > la.y + la.height || card.y + card.height < la.y) continue;
        bool hot = Hover(la) && Hover(card);
        if (hot) wantCursor = MOUSE_CURSOR_POINTING_HAND;
        DrawRectangleRec(card, hot ? UI_HOVER : UI_PANEL);
        DrawRectangleLinesEx(card, 1, UI_BORDER);
        DrawBold(r->name, (int)card.x + 18, (int)card.y + 14, FS, UI_LINK);
        if (r->starred) DrawText("starred", (int)card.x + 34 + MeasureText(r->name, FS), (int)card.y + 14, FS, UI_YELLOW);
        DrawFit(r->desc[0] ? r->desc : "No description", (int)card.x + 18, (int)card.y + 42, FS, (int)card.width - 36, UI_MUTED);
        DrawText(TextFormat("%d commit%s   |   %d open issue%s   |   updated %s",
                            r->commitCount, r->commitCount == 1 ? "" : "s",
                            OpenIssues(r), OpenIssues(r) == 1 ? "" : "s",
                            TimeAgo(LastActivity(r))),
                 (int)card.x + 18, (int)card.y + 70, FS, UI_MUTED);
        if (Hover(la) && Clicked(card)) OpenRepo(order[k]);
    }
    EndScissorMode();
    DrawScrollbar(la, scHome, n * rowH - 10);
}

static void TryCreateRepo(void)
{
    char nm[NAME_LEN];
    SanitizeName(tfName.buf, nm);
    if (!nm[0]) { Toast("Please give the repository a name"); return; }
    for (int i = 0; i < repoCount; i++)
        if (strcmp(repos[i].name, nm) == 0) { Toast("You already have a repository with that name"); return; }
    Repo *r = NewRepo(nm, tfDesc.buf);
    if (!r) { Toast("You've reached the maximum number of repositories"); return; }
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
    Rectangle p = {(float)W / 2 - 310, 100, 620, 430};
    DrawRectangleRec(p, UI_PANEL);
    DrawRectangleLinesEx(p, 1, UI_BORDER);
    int x = (int)p.x + 30, y = (int)p.y;

    DrawBold("Create a new repository", x, y + 28, 30, UI_TEXT);
    DrawText("A repository holds your files and their full history.", x, y + 70, FS, UI_MUTED);

    DrawText("Repository name", x, y + 116, FS, UI_TEXT);
    bool e1 = TextFieldUI(&tfName, (Rectangle){(float)x, (float)y + 142, 560, 40}, "e.g. my-project");
    DrawText("Description (optional)", x, y + 198, FS, UI_TEXT);
    bool e2 = TextFieldUI(&tfDesc, (Rectangle){(float)x, (float)y + 224, 560, 40}, "What is this project about?");

    Rectangle cbRow = {(float)x, (float)y + 284, 280, 28};
    Rectangle cb    = {(float)x, (float)y + 287, 22, 22};
    if (Hover(cbRow)) wantCursor = MOUSE_CURSOR_POINTING_HAND;
    DrawRectangleRec(cb, optReadme ? UI_GREEN : UI_BG);
    DrawRectangleLinesEx(cb, 1, optReadme ? UI_GREEN : UI_BORDER);
    if (optReadme) {
        DrawLineEx((Vector2){cb.x + 5, cb.y + 11}, (Vector2){cb.x + 9, cb.y + 16}, 3, UI_TEXT);
        DrawLineEx((Vector2){cb.x + 9, cb.y + 16}, (Vector2){cb.x + 17, cb.y + 6}, 3, UI_TEXT);
    }
    DrawText("Add a README file", x + 34, y + 288, FS, UI_TEXT);
    if (Clicked(cbRow)) optReadme = !optReadme;

    bool create = Button((Rectangle){p.x + p.width - 230, (float)y + 350, 200, 44}, "Create repository", UI_GREEN);
    bool cancel = Button((Rectangle){p.x + p.width - 360, (float)y + 350, 120, 44}, "Cancel", UI_BTN);
    if (cancel) { GoHome(); return; }
    if (create || e1 || e2) TryCreateRepo();
}

/* Returns true if the repository was deleted or we left it (so stop drawing it). */
static bool DrawRepoHeader(Repo *r, int activeTab)
{
    int x = 20, y = 72;
    int w1 = MeasureText("you", 30), w2 = MeasureText(" / ", 30), w3 = MeasureText(r->name, 30);
    Rectangle owner = {(float)x, (float)y, (float)w1, 30};
    if (Hover(owner)) wantCursor = MOUSE_CURSOR_POINTING_HAND;
    DrawText("you", x, y, 30, UI_LINK);
    DrawText(" / ", x + w1, y, 30, UI_MUTED);
    DrawBold(r->name, x + w1 + w2, y, 30, UI_TEXT);
    Rectangle pill = {(float)(x + w1 + w2 + w3 + 16), (float)y + 2, 70, 26};
    DrawRectangleRounded(pill, 0.5f, 6, UI_BTN);
    DrawText("Local", (int)pill.x + 10, (int)pill.y + 3, FS, UI_MUTED);
    DrawFit(r->desc[0] ? r->desc : "No description", x, y + 40, FS, W - 400, UI_MUTED);
    if (Clicked(owner)) { GoHome(); return true; }

    if (Button((Rectangle){(float)W - 150, (float)y, 130, 38}, r->starred ? "Starred" : "Star",
               r->starred ? (Color){110, 80, 20, 255} : UI_BTN)) {
        r->starred = !r->starred;
        MarkDirty();
    }
    if (ConfirmButton((Rectangle){(float)W - 340, (float)y, 180, 38}, "Delete repo", "Really delete?", 1)) {
        int i = curRepo;
        GoHome();
        DeleteRepo(i);
        MarkDirty();
        Toast("Repository deleted");
        return true;
    }

    int tx = 20;
    for (int t = 0; t < 3; t++) {
        const char *label = t == 0 ? TextFormat("Code  %d", r->fileCount)
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

static void TabCode(Repo *r)
{
    float top = 186, bottom = (float)H - 20;

    /* ---- left: file list ---- */
    Rectangle lp = {20, top, 260, bottom - top};
    DrawRectangleRec(lp, UI_PANEL);
    DrawRectangleLinesEx(lp, 1, UI_BORDER);
    DrawBold("Files", (int)lp.x + 14, (int)top + 12, FS, UI_TEXT);

    Rectangle la = {lp.x + 1, top + 44, lp.width - 2, lp.height - 44 - 156};
    float rowH = 30;
    ScrollArea(la, &scFiles, r->fileCount * rowH);
    if (!r->fileCount) DrawText("No files yet", (int)la.x + 14, (int)la.y + 6, FS, UI_MUTED);
    BeginScissorMode((int)la.x, (int)la.y, (int)la.width, (int)la.height);
    for (int i = 0; i < r->fileCount; i++) {
        Rectangle row = {la.x, la.y + i * rowH - scFiles, la.width, rowH};
        bool hot = Hover(la) && Hover(row);
        if (i == curFile) DrawRectangleRec(row, (Color){56, 139, 253, 40});
        else if (hot)     DrawRectangleRec(row, UI_HOVER);
        if (hot) wantCursor = MOUSE_CURSOR_POINTING_HAND;
        DrawFit(r->files[i].name, (int)row.x + 14, (int)row.y + 5, FS, (int)row.width - 54, UI_TEXT);
        char st = FileStatus(r, i);
        if (st) DrawText(st == 'A' ? "A" : "M", (int)(row.x + row.width - 28), (int)row.y + 5, FS, st == 'A' ? UI_GREENT : UI_YELLOW);
        if (Hover(la) && Clicked(row)) SelectFile(i);
    }
    EndScissorMode();
    DrawScrollbar(la, scFiles, r->fileCount * rowH);

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

    /* ---- right: editor + commit bar ---- */
    Rectangle rp  = {300, top, (float)W - 320, bottom - top};
    Rectangle fh  = {rp.x, rp.y, rp.width, 42};
    Rectangle edr = {rp.x, rp.y + 42, rp.width, rp.height - 42 - 62};
    Rectangle cb  = {rp.x, rp.y + rp.height - 48, rp.width, 48};

    if (curFile >= 0 && curFile < r->fileCount) {
        WorkFile *f = &r->files[curFile];
        if (EditorUI(f, edr)) MarkDirty();
        DrawRectangleLinesEx(edr, 1, UI_BORDER);

        DrawRectangleRec(fh, UI_PANEL);
        DrawRectangleLinesEx(fh, 1, UI_BORDER);
        DrawBold(f->name, (int)fh.x + 14, (int)fh.y + 11, FS, UI_TEXT);
        char st = FileStatus(r, curFile);
        if (st) DrawText(st == 'A' ? "new file" : "modified", (int)fh.x + 30 + MeasureText(f->name, FS), (int)fh.y + 11, FS,
                         st == 'A' ? UI_GREENT : UI_YELLOW);
        int ls = LineStart(f->text, ed.cursor);
        const char *info = TextFormat("Ln %d, Col %d   |   %d lines",
                                      LineOf(f->text, ed.cursor) + 1, ed.cursor - ls + 1, LineOf(f->text, f->len) + 1);
        DrawText(info, (int)(fh.x + fh.width) - 14 - MeasureText(info, FS), (int)fh.y + 11, FS, UI_MUTED);
    } else {
        Rectangle e = {rp.x, rp.y, rp.width, rp.height - 62};
        DrawRectangleRec(e, UI_PANEL);
        DrawRectangleLinesEx(e, 1, UI_BORDER);
        const char *m = "Pick a file on the left, or add a new one.";
        DrawText(m, (int)(e.x + e.width / 2) - MeasureText(m, FS) / 2, (int)(e.y + e.height / 2) - 10, FS, UI_MUTED);
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
        DrawText("Edit some files in the Code tab, write a message, then press Commit.", 20, 226, FS, UI_MUTED);
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
        DrawFit(c->message, (int)row.x + 18, (int)row.y + 14, FS, (int)row.width - 260, UI_TEXT);
        DrawText(TextFormat("you committed %s", TimeAgo(c->time)), (int)row.x + 18, (int)row.y + 42, FS, UI_MUTED);

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
            is->open = true;
            is->time = (long long)time(NULL);
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
        DrawText(TextFormat("#%d opened %s", i + 1, TimeAgo(is->time)), (int)row.x + 124, (int)row.y + 38, FS, UI_MUTED);

        if (ButtonEx((Rectangle){row.x + row.width - 140, row.y + 16, 124, 36}, is->open ? "Close" : "Reopen", UI_BTN, Hover(la))) {
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

    if (Button((Rectangle){20, 186, 170, 38}, "< All commits", UI_BTN)) { Unfocus(); screen = SCR_REPO; tab = TAB_COMMITS; return; }
    if (ConfirmButton((Rectangle){(float)W - 330, 186, 310, 38}, "Restore files from here", "Click again to confirm", 2)) {
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
    const char *meta = TextFormat("you committed %s   |   commit %s   |   %d file%s changed   ",
                                  TimeAgo(c->time), c->id, changeCount, changeCount == 1 ? "" : "s");
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

    if (!Load()) { Seed(); MarkDirty(); }

    float saveTimer = 0;
    int   lastCursor = -1;

    while (!WindowShouldClose()) {
        W = GetScreenWidth();
        H = GetScreenHeight();
        clickUsed  = false;
        wantCursor = MOUSE_CURSOR_DEFAULT;
        float dt = GetFrameTime();
        if (toastT > 0) toastT -= dt;
        if (armedId && (armedT -= dt) <= 0) armedId = 0;

        if (IsKeyPressed(KEY_ESCAPE)) {
            bool anyFocus = ed.focused || tfSearch.active || tfName.active || tfDesc.active ||
                            tfNewFile.active || tfCommit.active || tfIssue.active;
            if (anyFocus)                                     Unfocus();
            else if (screen == SCR_NEW || screen == SCR_REPO) GoHome();
            else if (screen == SCR_COMMIT)                    { screen = SCR_REPO; tab = TAB_COMMITS; }
        }

        BeginDrawing();
        ClearBackground(UI_BG);
        DrawHeader();

        switch (screen) {
        case SCR_HOME: ScreenHome(); break;
        case SCR_NEW:  ScreenNew();  break;
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
    for (int i = 0; i < repoCount; i++) FreeRepo(&repos[i]);
    CloseWindow();
    return 0;
}
