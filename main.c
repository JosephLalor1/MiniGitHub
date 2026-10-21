/*
 * Mini GitHub
 * -----------
 * A tiny GitHub-style app that runs entirely on your own computer.
 *
 *   - User accounts: sign up, sign in, sign out, change password, delete account.
 *     Passwords are never stored - only a salted PBKDF2-SHA256 hash of them.
 *   - Password boxes have an "eye" button to show / hide what you typed.
 *   - Forgot your password? Each account gets a one-time recovery code
 *     (shown when you sign up, or made in Account settings). Enter it on the
 *     "Forgot password?" screen to choose a new password.
 *   - Repositories belong to a user: create, star and delete your own
 *   - Browse everyone's repositories (read-only unless they're yours)
 *   - A built-in text editor for the files in your repositories
 *   - Uploads: drag files from your computer onto one of your repositories (or
 *     paste their paths), check the list, and save them as one commit. Text files
 *     can then be edited; pictures are previewed; any other file is stored as-is
 *   - Language detection: reads each file and guesses what it's written in (20
 *     languages), and shows each repository's breakdown of languages
 *   - Downloads: any file, a whole repository (as a .zip), or the files as they
 *     were in any commit - saved to your Downloads folder
 *   - Commits: a snapshot of every file, saved with a message and an author
 *   - Commit history and line-by-line diffs (what was added / removed)
 *   - Issues: anyone can open one; the repo owner or the issue's author can close it
 *   - Forks: make your own copy of someone else's repository (with its whole
 *     history) and change it freely. A fork shows how many commits it is ahead
 *     of / behind the original, and "Sync fork" brings in the original's new
 *     commits: copied straight across when you haven't committed anything
 *     yourself (a fast-forward), or combined with your work in a merge commit
 *     when you have - unless you both changed the same file.
 *   - Profiles: a picture (PNG or JPG, dragged onto the window or by file path), a short
 *     bio, a circle colour, and a choice of animated themes (Classic is the plain look)
 *   - A People page and a profile page for every user. Your own profile is your
 *     home page: it's where you land after signing in, and it lists your
 *     repositories (or everyone's), with a search box and a New button
 *   - A contribution graph of the last six months: hover a day to see what you
 *     did, click it to open a page listing every commit and issue from that day
 *   - Real git: every repository is a real git repository too. Mini GitHub runs
 *     a small git server on this computer only (http://localhost:7979), so
 *     git clone, git pull, git fetch and git push all work. Pushing needs your
 *     Mini GitHub username and password, and only the owner and collaborators
 *     may push. Press "Code" on a repository for its address; Settings > Git
 *     explains the rest. Commits keep git's own fingerprints (SHA-1), files are
 *     kept byte for byte, and folders show as a tree you can open and close.
 *
 * Everything is saved automatically to "minigithub.dat" in the folder the
 * program is run from. Older save files are upgraded automatically (a copy of
 * the old file is kept as minigithub.dat.v6-backup or similar). A save file
 * from before accounts existed: the first account created takes over its
 * repositories.
 *
 * Language: C99    Library: raylib 5.x    One file, nothing else needed.
 */

/* On Windows, rand_s() gives cryptographically secure random numbers.
   This must be defined before <stdlib.h> is included. */
#ifdef _WIN32
#define _CRT_RAND_S
#endif

#include "raylib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <ctype.h>
#include <time.h>
#include <math.h>

/* ------------------------------------------------------------------ */
/* Limits                                                             */
/* ------------------------------------------------------------------ */
#define MAX_USERS    32
#define MAX_REPOS    48
#define MAX_FILES    5000     /* files in one commit                           */
#define MAX_COMMITS  100000   /* commits in one repository                     */
#define MAX_BLOBS    2000000  /* different file versions in one repository     */
#define MAX_DRAFT    500      /* files one person can change before committing */
#define MAX_ISSUES   100
#define NAME_LEN     64
#define PATH_LEN     256      /* longest file path, e.g. "src/ui/button.c"     */
#define TEXT_LEN     160
#define MAX_UPLOAD   (10 * 1024 * 1024)   /* biggest file you can upload: 10 MB */
#define SAVE_FILE    "minigithub.dat"

#define BIO_LEN      150     /* longest bio allowed                           */
#define MIN_PASS     8       /* shortest password allowed                     */
#define PBKDF2_ITER  60000   /* hashing rounds: makes password guessing slow   */

#define FS 20   /* normal font size                 */
#define LH 24   /* row height in the editor / diffs */

/* ------------------------------------------------------------------ */
/* Colours (GitHub's dark theme)                                       */
/* ------------------------------------------------------------------ */
static const Color UI_BASE   = { 13,  17,  23, 255};    /* the window background, always solid */
static Color       UI_BG     = { 13,  17,  23, 255};    /* these three turn slightly see-through */
static Color       UI_PANEL  = { 22,  27,  34, 255};    /* when a theme is showing behind them   */
static Color       UI_HOVER  = { 30,  36,  44, 255};
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

#define FNV_START 2166136261u
static uint32_t Fnv(const char *s, int n, uint32_t h)
{
    for (int i = 0; i < n; i++) { h ^= (unsigned char)s[i]; h *= 16777619u; }
    return h;
}

/* ------------------------------------------------------------------ */
/* Git's building blocks                                              */
/*                                                                    */
/* Real git names every piece of a repository by a SHA-1 fingerprint  */
/* of its contents. There are three kinds of piece ("object"):        */
/*   blob   - one version of one file's bytes                         */
/*   tree   - one folder: a list of names, each pointing at a blob    */
/*            (a file) or another tree (a folder inside it)           */
/*   commit - a tree (the snapshot), the commit(s) before it, who     */
/*            made it, when, and the message                          */
/* Mini GitHub stores things the same way, with the same fingerprints, */
/* so real git can push to it and clone from it.                      */
/* ------------------------------------------------------------------ */

/* An array that grows as needed (doubling). Returns the new pointer. */
static void *GrowArr(void *p, int *cap, int need, size_t item)
{
    if (need <= *cap) return p;
    int c = *cap ? *cap : 16;
    while (c < need) c *= 2;
    void *q = realloc(p, (size_t)c * item);
    if (!q) return p;
    memset((char *)q + (size_t)*cap * item, 0, (size_t)(c - *cap) * item);
    *cap = c;
    return q;
}

/* A growing buffer of bytes. */
typedef struct { uint8_t *d; size_t n, cap; bool oom; } Buf;
static void BufNeed(Buf *b, size_t more)
{
    if (b->oom) return;
    if (b->n + more <= b->cap) return;
    size_t c = b->cap ? b->cap : 256;
    while (c < b->n + more) c *= 2;
    uint8_t *q = realloc(b->d, c);
    if (!q) { b->oom = true; return; }
    b->d = q; b->cap = c;
}
static void BufAdd(Buf *b, const void *p, size_t n) { BufNeed(b, n); if (b->oom) return; memcpy(b->d + b->n, p, n); b->n += n; }
static void BufStr(Buf *b, const char *s) { BufAdd(b, s, strlen(s)); }
static void BufByte(Buf *b, uint8_t c) { BufAdd(b, &c, 1); }
static void BufFree(Buf *b) { free(b->d); memset(b, 0, sizeof *b); }

/* ---------- SHA-1 ---------- */
typedef struct { uint32_t h[5]; uint8_t buf[64]; uint64_t len; int n; } Sha1;
#define ROL(x, n) (((x) << (n)) | ((x) >> (32 - (n))))
static void Sha1Block(Sha1 *s, const uint8_t *p)
{
    uint32_t w[80];
    for (int i = 0; i < 16; i++) w[i] = (uint32_t)p[i * 4] << 24 | (uint32_t)p[i * 4 + 1] << 16 | (uint32_t)p[i * 4 + 2] << 8 | p[i * 4 + 3];
    for (int i = 16; i < 80; i++) w[i] = ROL(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    uint32_t a = s->h[0], b = s->h[1], c = s->h[2], d = s->h[3], e = s->h[4];
    for (int i = 0; i < 80; i++) {
        uint32_t f, k;
        if (i < 20)      { f = (b & c) | (~b & d);           k = 0x5A827999; }
        else if (i < 40) { f = b ^ c ^ d;                    k = 0x6ED9EBA1; }
        else if (i < 60) { f = (b & c) | (b & d) | (c & d);  k = 0x8F1BBCDC; }
        else             { f = b ^ c ^ d;                    k = 0xCA62C1D6; }
        uint32_t t = ROL(a, 5) + f + e + k + w[i];
        e = d; d = c; c = ROL(b, 30); b = a; a = t;
    }
    s->h[0] += a; s->h[1] += b; s->h[2] += c; s->h[3] += d; s->h[4] += e;
}
static void Sha1Init(Sha1 *s)
{
    s->h[0] = 0x67452301; s->h[1] = 0xEFCDAB89; s->h[2] = 0x98BADCFE; s->h[3] = 0x10325476; s->h[4] = 0xC3D2E1F0;
    s->len = 0; s->n = 0;
}
static void Sha1Update(Sha1 *s, const void *data, size_t len)
{
    const uint8_t *p = data;
    s->len += len;
    if (s->n) {
        while (len && s->n < 64) { s->buf[s->n++] = *p++; len--; }
        if (s->n == 64) { Sha1Block(s, s->buf); s->n = 0; }
    }
    while (len >= 64) { Sha1Block(s, p); p += 64; len -= 64; }
    while (len) { s->buf[s->n++] = *p++; len--; }
}
static void Sha1Final(Sha1 *s, uint8_t out[20])
{
    uint64_t bits = s->len * 8;
    uint8_t pad = 0x80, zero = 0;
    Sha1Update(s, &pad, 1);
    while (s->n != 56) Sha1Update(s, &zero, 1);
    uint8_t L[8];
    for (int i = 0; i < 8; i++) L[i] = (uint8_t)(bits >> (56 - 8 * i));
    Sha1Update(s, L, 8);
    for (int i = 0; i < 5; i++) {
        out[i * 4] = (uint8_t)(s->h[i] >> 24); out[i * 4 + 1] = (uint8_t)(s->h[i] >> 16);
        out[i * 4 + 2] = (uint8_t)(s->h[i] >> 8); out[i * 4 + 3] = (uint8_t)s->h[i];
    }
}

/* The fingerprint git gives an object: SHA-1 of "<kind> <size>\0" + the contents. */
static void ObjectSha(const char *kind, const void *data, size_t len, uint8_t out[20])
{
    char head[48];
    int hn = snprintf(head, sizeof head, "%s %llu", kind, (unsigned long long)len);
    Sha1 s;
    Sha1Init(&s);
    Sha1Update(&s, head, (size_t)hn + 1);      /* + the zero byte after it */
    Sha1Update(&s, data, len);
    Sha1Final(&s, out);
}

static const uint8_t ZERO_SHA[20];
static void ShaHex(const uint8_t sha[20], char out[41])
{
    static const char *hx = "0123456789abcdef";
    for (int i = 0; i < 20; i++) { out[i * 2] = hx[sha[i] >> 4]; out[i * 2 + 1] = hx[sha[i] & 15]; }
    out[40] = 0;
}
/* Reads 40 hex characters; false if they aren't. */
static bool ParseHex40(const char *s, uint8_t out[20])
{
    for (int i = 0; i < 40; i++) if (!isxdigit((unsigned char)s[i])) return false;
    for (int i = 0; i < 20; i++) {
        int a = s[i * 2], b = s[i * 2 + 1];
        a = a <= '9' ? a - '0' : (a | 32) - 'a' + 10;
        b = b <= '9' ? b - '0' : (b | 32) - 'a' + 10;
        out[i] = (uint8_t)(a << 4 | b);
    }
    return true;
}

/* ---------- looking things up by fingerprint ----------
   A hash table from a SHA-1 to a number (which blob, which commit...). */
typedef struct { uint8_t sha[20]; int idx; } ShaSlot;
typedef struct { ShaSlot *s; int cap, used; } ShaMap;

static uint32_t ShaKey(const uint8_t *s) { return (uint32_t)s[0] | (uint32_t)s[1] << 8 | (uint32_t)s[2] << 16 | (uint32_t)s[3] << 24; }
static int MapGet(const ShaMap *m, const uint8_t *sha)
{
    if (!m->cap) return -1;
    for (uint32_t i = ShaKey(sha) & (uint32_t)(m->cap - 1);; i = (i + 1) & (uint32_t)(m->cap - 1)) {
        if (m->s[i].idx < 0) return -1;
        if (memcmp(m->s[i].sha, sha, 20) == 0) return m->s[i].idx;
    }
}
static void MapPut(ShaMap *m, const uint8_t *sha, int idx)
{
    if ((m->used + 1) * 2 > m->cap) {                      /* keep it at most half full */
        ShaMap n = {0};
        n.cap = m->cap ? m->cap * 2 : 64;
        n.s = malloc((size_t)n.cap * sizeof(ShaSlot));
        if (!n.s) return;
        for (int i = 0; i < n.cap; i++) n.s[i].idx = -1;
        for (int i = 0; i < m->cap; i++) if (m->s[i].idx >= 0) MapPut(&n, m->s[i].sha, m->s[i].idx);
        free(m->s);
        *m = n;
    }
    for (uint32_t i = ShaKey(sha) & (uint32_t)(m->cap - 1);; i = (i + 1) & (uint32_t)(m->cap - 1)) {
        if (m->s[i].idx < 0) { memcpy(m->s[i].sha, sha, 20); m->s[i].idx = idx; m->used++; return; }
        if (memcmp(m->s[i].sha, sha, 20) == 0) { m->s[i].idx = idx; return; }
    }
}
static void MapFree(ShaMap *m) { free(m->s); memset(m, 0, sizeof *m); }

/* ---------- file paths ----------
   Every path ("src/main.c") is kept once and shared; commits just point at
   it. That saves memory and lets paths be compared by pointer. */
static char **ipool;
static int    ipCount, ipCap, *ipTable, ipTableCap;
static const char *Intern(const char *s)
{
    uint32_t h = Fnv(s, (int)strlen(s), FNV_START);
    if ((ipCount + 1) * 2 > ipTableCap) {
        int nc = ipTableCap ? ipTableCap * 2 : 1024;
        int *t = malloc((size_t)nc * sizeof(int));
        if (!t) return NULL;
        for (int i = 0; i < nc; i++) t[i] = -1;
        for (int k = 0; k < ipCount; k++) {
            uint32_t j = Fnv(ipool[k], (int)strlen(ipool[k]), FNV_START) & (uint32_t)(nc - 1);
            while (t[j] >= 0) j = (j + 1) & (uint32_t)(nc - 1);
            t[j] = k;
        }
        free(ipTable);
        ipTable = t; ipTableCap = nc;
    }
    uint32_t j = h & (uint32_t)(ipTableCap - 1);
    for (; ipTable[j] >= 0; j = (j + 1) & (uint32_t)(ipTableCap - 1))
        if (strcmp(ipool[ipTable[j]], s) == 0) return ipool[ipTable[j]];
    ipool = GrowArr(ipool, &ipCap, ipCount + 1, sizeof(char *));
    if (ipCount >= ipCap) return NULL;
    size_t n = strlen(s);
    char *c = malloc(n + 1);
    if (!c) return NULL;
    memcpy(c, s, n + 1);
    ipool[ipCount] = c;
    ipTable[j] = ipCount++;
    return c;
}

/* Is this a sensible path for a file: "a/b.txt" yes; "", "/a", "a//b", "a/../b", "a/" no. */
static bool ValidPath(const char *p)
{
    size_t n = strlen(p);
    if (!n || n >= PATH_LEN || p[0] == '/' || p[n - 1] == '/') return false;
    const char *s = p;
    for (;;) {
        const char *e = strchr(s, '/');
        size_t len = e ? (size_t)(e - s) : strlen(s);
        if (len == 0) return false;
        if ((len == 1 && s[0] == '.') || (len == 2 && s[0] == '.' && s[1] == '.')) return false;
        if (len == 4 && strncmp(s, ".git", 4) == 0) return false;
        if (!e) break;
        s = e + 1;
    }
    for (size_t i = 0; i < n; i++) if ((unsigned char)p[i] < 32 || p[i] == '\\') return false;
    return true;
}

/* ---------- file modes ----------
   Git remembers whether a file is a normal file, a program you can run,
   or a "symbolic link" (a pointer to another path). */
enum { MODE_FILE, MODE_EXEC, MODE_LINK };
static const char *ModeText(int m) { return m == MODE_EXEC ? "100755" : m == MODE_LINK ? "120000" : "100644"; }

/* ---------- unpacking (DEFLATE) ----------
   Git squeezes every object with zlib (DEFLATE plus a small header and a
   checksum). This undoes that, and also says how many bytes it used -
   git's packs put objects back to back without saying where each ends. */
typedef struct { uint16_t t[1 << 15]; } HTab;      /* lookup: 15 bits in -> symbol and code length */

typedef struct {
    const uint8_t *in; size_t inLen, pos;
    uint64_t bb; int bc;                           /* bits waiting to be used */
    Buf out; size_t maxOut;
    bool err;
} Inf;

static void InfFill(Inf *s)
{
    while (s->bc <= 56) {
        uint64_t byte = s->pos < s->inLen ? s->in[s->pos] : 0;   /* past the end: zeros (checked later) */
        s->pos++;
        s->bb |= byte << s->bc;
        s->bc += 8;
    }
}
static unsigned InfBits(Inf *s, int n)
{
    if (n == 0) return 0;
    if (s->bc < n) InfFill(s);
    unsigned v = (unsigned)(s->bb & ((1ull << n) - 1));
    s->bb >>= n; s->bc -= n;
    return v;
}
/* Builds the lookup table from each symbol's code length. False if the lengths make no sense. */
static bool HBuild(HTab *h, const uint8_t *lens, int n)
{
    int count[16] = {0}, next[16];
    for (int i = 0; i < n; i++) count[lens[i]]++;
    count[0] = 0;
    int left = 1;
    for (int l = 1; l < 16; l++) { left <<= 1; left -= count[l]; if (left < 0) return false; }
    int code = 0;
    next[1] = 0;
    for (int l = 1; l < 15; l++) { code = (code + count[l]) << 1; next[l + 1] = code; }
    memset(h->t, 0, sizeof h->t);
    for (int sym = 0; sym < n; sym++) {
        int l = lens[sym];
        if (!l) continue;
        int c = next[l]++, rev = 0;
        for (int k = 0; k < l; k++) rev |= ((c >> k) & 1) << (l - 1 - k);
        for (int i = rev; i < (1 << 15); i += 1 << l) h->t[i] = (uint16_t)(sym | l << 9);
    }
    return true;
}
static int HDecode(Inf *s, const HTab *h)
{
    if (s->bc < 15) InfFill(s);
    uint16_t e = h->t[s->bb & 0x7FFF];
    int l = e >> 9;
    if (!l) { s->err = true; return -1; }
    s->bb >>= l; s->bc -= l;
    return e & 0x1FF;
}

static const uint16_t LBASE[29] = {3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
static const uint8_t  LEXT[29]  = {0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
static const uint16_t DBASE[30] = {1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
static const uint8_t  DEXT[30]  = {0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};

static HTab *fixedLit, *fixedDist;

static bool InfCodes(Inf *s, const HTab *lit, const HTab *dist)
{
    for (;;) {
        int sym = HDecode(s, lit);
        if (s->err) return false;
        if (sym < 256) { BufByte(&s->out, (uint8_t)sym); }
        else if (sym == 256) return true;
        else {
            sym -= 257;
            if (sym >= 29) return false;
            size_t len = LBASE[sym] + InfBits(s, LEXT[sym]);
            int ds = HDecode(s, dist);
            if (s->err || ds < 0 || ds >= 30) return false;
            size_t d = DBASE[ds] + InfBits(s, DEXT[ds]);
            if (d > s->out.n) return false;
            BufNeed(&s->out, len);
            if (s->out.oom) return false;
            uint8_t *o = s->out.d + s->out.n;
            for (size_t i = 0; i < len; i++) o[i] = o[(ptrdiff_t)i - (ptrdiff_t)d];
            s->out.n += len;
        }
        if (s->out.oom || s->out.n > s->maxOut) return false;
        if (s->pos > s->inLen + 8) return false;      /* ran off the end of the input */
    }
}

/* Raw DEFLATE. *used gets how many input bytes it took. NULL if the data is damaged. */
static uint8_t *InflateRaw(const uint8_t *in, size_t inLen, size_t *used, size_t *outLen, size_t maxOut)
{
    static HTab lit, dist;
    Inf s;
    memset(&s, 0, sizeof s);
    s.in = in; s.inLen = inLen; s.maxOut = maxOut;
    if (!fixedLit) {
        static HTab fl, fd;
        uint8_t l[288];
        for (int i = 0; i < 144; i++) l[i] = 8;
        for (int i = 144; i < 256; i++) l[i] = 9;
        for (int i = 256; i < 280; i++) l[i] = 7;
        for (int i = 280; i < 288; i++) l[i] = 8;
        HBuild(&fl, l, 288);
        for (int i = 0; i < 30; i++) l[i] = 5;
        HBuild(&fd, l, 30);
        fixedLit = &fl; fixedDist = &fd;
    }
    bool last = false, ok = true;
    while (!last && ok) {
        last = InfBits(&s, 1);
        int type = (int)InfBits(&s, 2);
        if (type == 0) {                                   /* stored: no squeezing */
            InfBits(&s, s.bc & 7);                         /* skip to a whole byte */
            unsigned len = InfBits(&s, 16), nlen = InfBits(&s, 16);
            if ((len ^ 0xFFFF) != nlen) { ok = false; break; }
            for (unsigned i = 0; i < len && ok; i++) {
                BufByte(&s.out, (uint8_t)InfBits(&s, 8));
                if (s.pos > s.inLen + 8 || s.out.oom) ok = false;
            }
            if (s.out.n > s.maxOut) ok = false;
        } else if (type == 1) {
            ok = InfCodes(&s, fixedLit, fixedDist);
        } else if (type == 2) {
            int hlit = (int)InfBits(&s, 5) + 257, hdist = (int)InfBits(&s, 5) + 1, hclen = (int)InfBits(&s, 4) + 4;
            static const uint8_t ord[19] = {16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
            uint8_t cl[19] = {0}, lens[320] = {0};
            for (int i = 0; i < hclen; i++) cl[ord[i]] = (uint8_t)InfBits(&s, 3);
            HTab *ct = &lit;                              /* borrow the table for the code-length code */
            if (!HBuild(ct, cl, 19)) { ok = false; break; }
            int n = 0;
            while (n < hlit + hdist && ok) {
                int sym = HDecode(&s, ct);
                if (s.err) { ok = false; break; }
                if (sym < 16) lens[n++] = (uint8_t)sym;
                else {
                    int rep, val = 0;
                    if (sym == 16) { if (!n) { ok = false; break; } val = lens[n - 1]; rep = 3 + (int)InfBits(&s, 2); }
                    else if (sym == 17) rep = 3 + (int)InfBits(&s, 3);
                    else rep = 11 + (int)InfBits(&s, 7);
                    if (n + rep > hlit + hdist) { ok = false; break; }
                    while (rep--) lens[n++] = (uint8_t)val;
                }
            }
            if (!ok || !lens[256]) { ok = false; break; }
            if (!HBuild(&lit, lens, hlit) || !HBuild(&dist, lens + hlit, hdist)) { ok = false; break; }
            ok = InfCodes(&s, &lit, &dist);
        } else ok = false;
    }
    size_t consumed = s.pos - (size_t)(s.bc >> 3);
    if (!ok || s.out.oom || consumed > inLen) { BufFree(&s.out); return NULL; }
    if (used) *used = consumed;
    *outLen = s.out.n;
    BufByte(&s.out, 0);                                    /* a zero after it, handy for text */
    if (s.out.oom) { BufFree(&s.out); return NULL; }
    return s.out.d;
}

static uint32_t Adler32(const uint8_t *d, size_t n)
{
    uint32_t a = 1, b = 0;
    while (n) {
        size_t k = n < 5000 ? n : 5000;
        n -= k;
        while (k--) { a += *d++; b += a; }
        a %= 65521; b %= 65521;
    }
    return b << 16 | a;
}

/* zlib = 2-byte header + DEFLATE + Adler-32 checksum. */
static uint8_t *InflateZlib(const uint8_t *in, size_t inLen, size_t *used, size_t *outLen, size_t maxOut)
{
    if (inLen < 6 || (in[0] & 0x0F) != 8 || ((in[0] << 8) | in[1]) % 31 != 0 || (in[1] & 0x20)) return NULL;
    size_t u = 0;
    uint8_t *o = InflateRaw(in + 2, inLen - 2, &u, outLen, maxOut);
    if (!o) return NULL;
    if (2 + u + 4 > inLen) { free(o); return NULL; }
    const uint8_t *c = in + 2 + u;
    uint32_t want = (uint32_t)c[0] << 24 | (uint32_t)c[1] << 16 | (uint32_t)c[2] << 8 | c[3];
    if (want != Adler32(o, *outLen)) { free(o); return NULL; }
    if (used) *used = 2 + u + 4;
    return o;
}

/* gzip (used by git for big requests) = a header + DEFLATE + a CRC and the size. */
static uint8_t *InflateGzip(const uint8_t *in, size_t inLen, size_t *outLen, size_t maxOut)
{
    if (inLen < 18 || in[0] != 0x1f || in[1] != 0x8b || in[2] != 8) return NULL;
    int flg = in[3];
    size_t p = 10;
    if (flg & 4) { if (p + 2 > inLen) return NULL; p += 2 + (size_t)(in[p] | in[p + 1] << 8); }
    if (flg & 8)  { while (p < inLen && in[p]) p++; p++; }
    if (flg & 16) { while (p < inLen && in[p]) p++; p++; }
    if (flg & 2)  p += 2;
    if (p >= inLen) return NULL;
    return InflateRaw(in + p, inLen - p, NULL, outLen, maxOut);
}

/* ---------- squeezing (DEFLATE) ----------
   Mini GitHub's own compressor (raylib's one can write past the end of its
   buffer on big files that don't squeeze). It finds repeats with a hash of
   every 3 bytes, and writes them with DEFLATE's fixed codes: simple, quick,
   and it never writes more than "limit" bytes - if the result would be
   bigger than that, it gives up (returns false) and the caller stores the
   data unsqueezed instead. */
typedef struct { Buf *o; uint64_t acc; int n; size_t limit, start; bool full; } BitOut;
static void PutBits(BitOut *b, uint32_t v, int n)
{
    b->acc |= (uint64_t)v << b->n;
    b->n += n;
    while (b->n >= 8) { BufByte(b->o, (uint8_t)b->acc); b->acc >>= 8; b->n -= 8; }
    if (b->o->n - b->start > b->limit || b->o->oom) b->full = true;
}
/* Huffman codes go in most-significant bit first, so they're reversed. */
static void PutCode(BitOut *b, uint32_t code, int len)
{
    uint32_t r = 0;
    for (int i = 0; i < len; i++) r |= ((code >> i) & 1) << (len - 1 - i);
    PutBits(b, r, len);
}
static void PutSym(BitOut *b, int sym)                   /* DEFLATE's fixed literal / length codes */
{
    if (sym < 144)      PutCode(b, 0x30 + (uint32_t)sym, 8);
    else if (sym < 256) PutCode(b, 0x190 + (uint32_t)(sym - 144), 9);
    else if (sym < 280) PutCode(b, (uint32_t)(sym - 256), 7);
    else                PutCode(b, 0xC0 + (uint32_t)(sym - 280), 8);
}
static bool RawDeflate(const uint8_t *d, size_t n, Buf *out, size_t limit)
{
    enum { HBITS = 15, WIN = 32768, CHAIN = 24 };
    BitOut b = {out, 0, 0, limit, out->n, false};
    int *head = malloc(sizeof(int) << HBITS), *prev = malloc(sizeof(int) * WIN);
    if (!head || !prev) { free(head); free(prev); return false; }
    for (int i = 0; i < (1 << HBITS); i++) head[i] = -1;
    PutBits(&b, 1, 1); PutBits(&b, 1, 2);                /* the last block, fixed codes */
    size_t i = 0;
    size_t check = 262144;
    while (i < n && !b.full) {
        if (i >= check) {                                    /* not squeezing at all (e.g. a zip or a photo)? stop early */
            if (out->n - b.start > i - i / 64) { b.full = true; break; }
            check += 262144;
        }
        int bestLen = 0, bestDist = 0;
        if (i + 3 <= n) {
            uint32_t h = ((uint32_t)d[i] << 16 | (uint32_t)d[i + 1] << 8 | d[i + 2]) * 2654435761u >> (32 - HBITS);
            int c = head[h], tries = 0;
            size_t maxLen = n - i < 258 ? n - i : 258;
            while (c >= 0 && i - (size_t)c <= WIN && tries++ < CHAIN) {
                size_t l = 0;
                while (l < maxLen && d[(size_t)c + l] == d[i + l]) l++;
                if ((int)l > bestLen) { bestLen = (int)l; bestDist = (int)(i - (size_t)c); if (l == maxLen) break; }
                int nx = prev[c % WIN];
                if (nx >= c) break;
                c = nx;
            }
            prev[i % WIN] = head[h]; head[h] = (int)i;
        }
        if (bestLen >= 3) {
            int lc = 28;                                     /* length code */
            while (lc > 0 && LBASE[lc] > bestLen) lc--;
            PutSym(&b, 257 + lc);
            if (LEXT[lc]) PutBits(&b, (uint32_t)(bestLen - LBASE[lc]), LEXT[lc]);
            int dc = 29;                                     /* distance code */
            while (dc > 0 && DBASE[dc] > bestDist) dc--;
            PutCode(&b, (uint32_t)dc, 5);
            if (DEXT[dc]) PutBits(&b, (uint32_t)(bestDist - DBASE[dc]), DEXT[dc]);
            for (size_t k = 1; k < (size_t)bestLen && i + k + 3 <= n; k++) {   /* remember the bytes inside the match too */
                size_t j = i + k;
                uint32_t h = ((uint32_t)d[j] << 16 | (uint32_t)d[j + 1] << 8 | d[j + 2]) * 2654435761u >> (32 - HBITS);
                prev[j % WIN] = head[h]; head[h] = (int)j;
            }
            i += (size_t)bestLen;
        } else PutSym(&b, d[i++]);
    }
    PutSym(&b, 256);                                         /* end of block */
    if (b.n) PutBits(&b, 0, 8 - b.n);
    free(head); free(prev);
    if (b.full) { out->n = b.start; return false; }
    return true;
}

/* Squeezes data into zlib format: DEFLATE when that makes it smaller,
   otherwise "stored" blocks (no squeezing, still valid). */
static void ZlibCompress(Buf *out, const uint8_t *d, size_t n)
{
    BufByte(out, 0x78); BufByte(out, 0x01);
    if (n < 64 || !RawDeflate(d, n, out, n - 8)) {
        size_t at = 0;
        do {
            size_t k = n - at > 65535 ? 65535 : n - at;
            BufByte(out, at + k >= n ? 1 : 0);
            BufByte(out, (uint8_t)(k & 0xFF)); BufByte(out, (uint8_t)(k >> 8));
            BufByte(out, (uint8_t)(~k & 0xFF)); BufByte(out, (uint8_t)((~k >> 8) & 0xFF));
            BufAdd(out, d + at, k);
            at += k;
        } while (at < n);
    }
    uint32_t a = Adler32(d, n);
    BufByte(out, (uint8_t)(a >> 24)); BufByte(out, (uint8_t)(a >> 16)); BufByte(out, (uint8_t)(a >> 8)); BufByte(out, (uint8_t)a);
}

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
    char      rsalt[33];             /* recovery code: salt and hash,     */
    char      rhash[65];             /* stored like the password ("" = none) */
    long long created;
    char      bio[TEXT_LEN];
    char      email[TEXT_LEN];       /* put on commits made here, so git shows it ("" = none set) */
    int       theme;                 /* one of THEME_...                  */
    int       avatarColor;           /* 0 = automatic, 1..8 = chosen      */
    char     *avatar;                /* profile picture: PNG as base64 text, or NULL */
    Texture2D tex;                   /* the picture loaded for drawing (not saved)   */
    int       texState;              /* 0 not loaded yet, 1 loaded, -1 broken        */
} User;

/* A blob is one unique version of a file's contents. Identical
   contents are stored only once, just like real git - and with the same
   SHA-1 fingerprint git gives it.
   "Binary" files (pictures, zips, programs...) are stored byte for byte;
   they just can't be shown as text or edited. */
typedef struct LangCache LangCache;   /* a file's language clues, worked out once (see "Language detection") */
typedef struct { uint8_t sha[20]; char *data; int len; bool binary; LangCache *lang; } Blob;

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
    const char **names;              /* sorted paths (shared strings, see Intern)            */
    int       *blobs;
    uint8_t   *modes;                /* MODE_FILE, MODE_EXEC or MODE_LINK                    */
    char      *raw;                  /* the git commit object: tree, parents, author, message */
    int       rawLen;
    bool      copied;                /* came from the original repository into a fork: */
                                     /* it isn't counted again as someone's contribution */
    bool      statsDone;             /* the three below are worked out when first needed, */
    int       sFiles, sAdds, sDels;  /* not saved: files changed, lines added / removed     */
} Commit;

/* A file someone is editing. In a draft (below) it also remembers which
   version it started from, so we can spot when someone else changed the
   same file in the meantime. */
typedef struct {
    char name[PATH_LEN]; char *text; int len, cap; bool binary;
    unsigned ver, langVer;           /* ver goes up with every change; langVer = the version lang was worked out for */
    LangCache *lang;
    bool deleted;                    /* in a draft: "I've deleted this file"               */
    int  base;                       /* in a draft: blob it started from (-1 = a new file) */
} WorkFile;

/* Each person who can edit a repository has their own draft: just the files
   they've changed and not committed yet. Everything else they see is the
   latest commit - so nobody sees anyone else's half-finished work, and a
   commit only ever contains the changes of the person who made it. */
typedef struct {
    int  user; int count; WorkFile *files;
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
    char      desc[TEXT_LEN];
    char      branch[NAME_LEN];      /* the name the first branch gets (while the repository is empty) */
    int       branchCount;
    Branch    branches[MAX_BRANCHES];
    int       defBranch;             /* the default branch (what git clone checks out)   */
    int       curBranch;             /* the branch being looked at / worked on right now */
    long long created;
    int       starCount;   int      starredBy[MAX_USERS];
    int       collabCount; int      collabs[MAX_USERS];       /* people invited who accepted   */
    int       inviteCount; int      invites[MAX_USERS];       /* invited, not answered yet     */
    long long inviteTime[MAX_USERS];
    int       draftCount;  Draft    drafts[MAX_DRAFTS];
    int       commitCount, commitCap; Commit *commits;        /* oldest first; the last is the latest */
    int       blobCount,   blobCap;   Blob   *blobs;
    ShaMap    commitMap, blobMap;    /* fingerprint -> index, for quick lookups */
    unsigned  gen;                   /* goes up whenever the commits change */
    int       issueCount;  Issue    issues[MAX_ISSUES];
    /* forks: the original is found by its owner + name (names are unique per owner) */
    bool      isFork;
    int       forkOwner;             /* user id of the original's owner              */
    char      forkName[NAME_LEN];    /* the original's name                           */
    char      forkBase[41];          /* fingerprint of the newest commit of the original */
                                     /* this fork has taken in ("" = the original was empty) */
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
typedef enum { SCR_LOGIN, SCR_REGISTER, SCR_FORGOT, SCR_RECOVERY, SCR_NEW, SCR_REPO, SCR_COMMIT, SCR_ACCOUNT, SCR_DAY, SCR_PROFILE, SCR_PEOPLE, SCR_UPLOAD, SCR_LOADFAIL } Screen;
typedef enum { TAB_CODE, TAB_COMMITS, TAB_ISSUES, TAB_PEOPLE } Tab;

typedef struct { char buf[TEXT_LEN]; int len; bool active; bool secret; bool reveal; } TextField;
typedef struct { int cursor; float scrollY, scrollX; bool focused; } EditorState;

static Screen screen = SCR_LOGIN;
static Tab    tab    = TAB_CODE;
static int    curRepo = -1, curCommit = -1, curChange = -1;
static char   selName[PATH_LEN];      /* the file open in the Code tab ("" = pick the first) */

static TextField tfSearch, tfName, tfDesc, tfNewFile, tfCommit, tfIssue, tfUser, tfBio, tfAvatar, tfCode, tfUpPath, tfUpMsg, tfInvite, tfEmail, tfBranch;
static TextField tfPass    = {.secret = true}, tfPass2    = {.secret = true};
static TextField tfOldPass = {.secret = true}, tfNewPass  = {.secret = true};
static TextField tfNewPass2= {.secret = true}, tfDelPass  = {.secret = true};
static EditorState ed;
static bool  optReadme = true, optExample = true;
static int   homeView;                /* 0 = your repositories, 1 = everyone's */
static float scGraph, scFiles, scCommits, scIssues, scChanges, scDiff, scDay, scDayRepos;
static char  shownCode[24];           /* recovery code on screen right now (never saved) */
static bool  codeAfterReset;          /* the code screen was reached by resetting a password */
static float scProfile, scPeople, scSettings, scPeopleTab;
static int   viewUser = -1;           /* whose profile page is open             */
static int   dayUser  = -1;           /* whose contributions the day page shows */
static int   settingsTab;             /* 0 = profile, 1 = password & account, 2 = git */
static Color uiAccent = {247, 129, 102, 255};   /* the signed-in user's theme colour */
static long long curDay;              /* midnight at the start of the day being viewed      */
static bool  fromDay;                 /* commit page was opened from a day page: go back there */
static int   failedLogins;
static float lockT;                   /* sign-in paused after too many wrong passwords */

static int   W, H;
static bool  clickUsed;       /* stops one click triggering two things */
static int   wantCursor;
static int   gitPort;                 /* the git server's port (see "Talking to real git"), 0 = off */
static char  gitProblem[TEXT_LEN];    /* why it isn't running                                       */
static bool  dirty;           /* something changed and needs saving    */
static char  toastMsg[TEXT_LEN];
static float toastT;
static int   armedId;         /* for "click again to confirm" buttons  */
static float armedT;

/* ------------------------------------------------------------------ */
/* Small helpers                                                      */
/* ------------------------------------------------------------------ */

static void MarkDirty(void) { dirty = true; }
static void Toast(const char *s) { snprintf(toastMsg, sizeof toastMsg, "%s", s); toastT = 3.0f; }

static bool KeyRep(int k) { return IsKeyPressed(k) || IsKeyPressedRepeat(k); }
/* An open drop-down menu covers part of the page: things under it act as if the mouse weren't there. */
static Rectangle hoverBlock;
static bool      hoverBlocked;
static bool Hover(Rectangle r)
{
    Vector2 m = GetMousePosition();
    if (hoverBlocked && CheckCollisionPointRec(m, hoverBlock)) return false;
    return CheckCollisionPointRec(m, r);
}
static bool Clicked(Rectangle r)
{
    if (!clickUsed && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && Hover(r)) { clickUsed = true; return true; }
    return false;
}

/* The first n bytes of s as text to draw. Tab characters are shown as
   spaces up to the next multiple of 4 columns (raylib can't draw a tab),
   and the \r of Windows line endings is left out. */
static char sliceBuf[2048];
static const char *Slice(const char *s, int n)
{
    if (n < 0) n = 0;
    int j = 0;
    for (int i = 0; i < n && j < 2000; i++) {
        if (s[i] == '\t') { do sliceBuf[j++] = ' '; while (j % 4 && j < 2000); }
        else if (s[i] == '\r') continue;
        else sliceBuf[j++] = s[i];
    }
    sliceBuf[j] = 0;
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

/* A file path someone typed: like SanitizeName, but "/" makes folders
   ("docs/notes.txt"). Doubled or stray slashes are dropped. */
static void SanitizePath(const char *in, char *out)
{
    int j = 0;
    for (int i = 0; in[i] && j < PATH_LEN - 1; i++) {
        char c = in[i] == '\\' ? '/' : in[i];
        if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.') out[j++] = c;
        else if (c == ' ') out[j++] = '-';
        else if (c == '/' && j > 0 && out[j - 1] != '/') out[j++] = '/';
    }
    while (j > 0 && out[j - 1] == '/') j--;
    out[j] = 0;
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

/* The same rule real git uses: a file is "binary" (not text) if there's a
   zero byte anywhere in its first 8000 bytes. Text never contains one. */
static bool LooksBinary(const char *d, int len)
{
    int n = len < 8000 ? len : 8000;
    return memchr(d, 0, (size_t)n) != NULL;
}

/* Goes up by one whenever file contents are replaced or freed, so a picture
   preview made earlier is never mistaken for the new contents. */
static int filesGen;

/* Sets a working file to any bytes (text or binary). */
static void WF_SetN(WorkFile *f, const char *data, int n)
{
    filesGen++;
    f->ver++;
    if (f->cap < n + 1) { f->cap = n + 64; f->text = realloc(f->text, (size_t)f->cap); }
    memcpy(f->text, data, (size_t)n);
    f->text[n] = 0;                                  /* always ends in a zero, handy for text */
    f->len = n;
    f->binary = LooksBinary(data, n);
}
static void WF_Set(WorkFile *f, const char *text) { WF_SetN(f, text, (int)strlen(text)); }
static void WF_Insert(WorkFile *f, int at, const char *s, int n)
{
    if (f->len + n + 1 > f->cap) { f->cap = (f->len + n + 1) * 2; f->text = realloc(f->text, (size_t)f->cap); }
    memmove(f->text + at + n, f->text + at, (size_t)(f->len - at + 1));
    memcpy(f->text + at, s, (size_t)n);
    f->len += n;
    f->ver++;
}
static void WF_Delete(WorkFile *f, int at, int n)
{
    memmove(f->text + at, f->text + at + n, (size_t)(f->len - at - n + 1));
    f->len -= n;
    f->ver++;
}
static void WF_Free(WorkFile *f) { free(f->text); f->text = NULL; f->len = f->cap = 0; free(f->lang); f->lang = NULL; filesGen++; }

/* ------------------------------------------------------------------ */
/* Repositories, blobs and commits                                    */
/* ------------------------------------------------------------------ */
static const char *UserName(int id);
static const char *UserEmail(int id);

static Repo *NewRepo(int owner, const char *name, const char *desc)
{
    if (repoCount >= MAX_REPOS) return NULL;
    Repo *r = &repos[repoCount++];
    memset(r, 0, sizeof *r);
    r->owner = owner;
    snprintf(r->name, NAME_LEN, "%s", name);
    snprintf(r->desc, TEXT_LEN, "%s", desc);
    snprintf(r->branch, NAME_LEN, "main");
    r->created = (long long)time(NULL);
    return r;
}

static void FreeDraft(Draft *d)
{
    for (int i = 0; i < d->count; i++) WF_Free(&d->files[i]);
    free(d->files);
    d->files = NULL;
    d->count = 0;
}

static void FreeCommit(Commit *c)
{
    free(c->names); free(c->blobs); free(c->modes); free(c->raw);
    c->names = NULL; c->blobs = NULL; c->modes = NULL; c->raw = NULL;
}

static void FreeRepo(Repo *r)
{
    for (int i = 0; i < r->draftCount; i++) FreeDraft(&r->drafts[i]);
    r->draftCount = 0;
    for (int i = 0; i < r->blobCount; i++) { free(r->blobs[i].data); free(r->blobs[i].lang); }
    for (int i = 0; i < r->commitCount; i++) FreeCommit(&r->commits[i]);
    free(r->blobs); free(r->commits);
    r->blobs = NULL; r->commits = NULL;
    r->blobCount = r->blobCap = r->commitCount = r->commitCap = 0;
    MapFree(&r->commitMap); MapFree(&r->blobMap);
    filesGen++;
}

static void DeleteRepo(int i)
{
    FreeRepo(&repos[i]);
    memmove(&repos[i], &repos[i + 1], (size_t)(repoCount - i - 1) * sizeof(Repo));
    repoCount--;
    memset(&repos[repoCount], 0, sizeof(Repo));
}

/* Store contents once; returns the blob's index (or -1 if out of room).
   If sha is given it must be the contents' git fingerprint (saves working it out again). */
static int InternBlobSha(Repo *r, const char *data, int len, const uint8_t *known)
{
    uint8_t sha[20];
    if (known) memcpy(sha, known, 20);
    else ObjectSha("blob", data, (size_t)len, sha);
    int have = MapGet(&r->blobMap, sha);
    if (have >= 0) return have;
    if (r->blobCount >= MAX_BLOBS) return -1;
    r->blobs = GrowArr(r->blobs, &r->blobCap, r->blobCount + 1, sizeof(Blob));
    if (r->blobCount >= r->blobCap) return -1;
    Blob *b = &r->blobs[r->blobCount];
    b->data = malloc((size_t)len + 1);
    if (!b->data) return -1;
    memcpy(b->data, data, (size_t)len);
    b->data[len] = 0;
    b->len = len;
    b->binary = LooksBinary(data, len);
    b->lang = NULL;
    memcpy(b->sha, sha, 20);
    MapPut(&r->blobMap, sha, r->blobCount);
    return r->blobCount++;
}
static int InternBlob(Repo *r, const char *data, int len) { return InternBlobSha(r, data, len, NULL); }

static const char *BlobText(Repo *r, int b)   { return b >= 0 ? r->blobs[b].data : ""; }
static int         BlobLen(Repo *r, int b)    { return b >= 0 ? r->blobs[b].len : 0; }
static bool        BlobBinary(Repo *r, int b) { return b >= 0 && r->blobs[b].binary; }
/* The branch you're on, and its newest commit ("head"). An empty repository has no branches yet. */
static int HeadIndex(Repo *r)
{
    if (!r->branchCount) return -1;
    if (r->curBranch < 0 || r->curBranch >= r->branchCount) r->curBranch = r->defBranch;
    return r->branches[r->curBranch].tip;
}
static Commit *Head(Repo *r) { int h = HeadIndex(r); return h >= 0 ? &r->commits[h] : NULL; }
static const char *CurBranchName(Repo *r) { HeadIndex(r); return r->branchCount ? r->branches[r->curBranch].name : r->branch; }
static const char *DefBranchName(Repo *r) { return r->branchCount ? r->branches[r->defBranch].name : r->branch; }
static int DefaultTip(Repo *r) { return r->branchCount ? r->branches[r->defBranch].tip : -1; }
static long long LastActivity(Repo *r)
{
    long long t = r->created;
    for (int b = 0; b < r->branchCount; b++) if (r->commits[r->branches[b].tip].time > t) t = r->commits[r->branches[b].tip].time;
    return t;
}
static void MarkAnc(Repo *r, int tip, uint8_t *mark, uint8_t bit);
static int FindBranch(Repo *r, const char *name)
{
    for (int b = 0; b < r->branchCount; b++) if (strcmp(r->branches[b].name, name) == 0) return b;
    return -1;
}
static int AddBranch(Repo *r, const char *name, int tip)
{
    if (r->branchCount >= MAX_BRANCHES) return -1;
    char copy[NAME_LEN];                       /* name may be part of the same repository (r->branch) */
    snprintf(copy, sizeof copy, "%s", name);
    Branch *b = &r->branches[r->branchCount];
    memcpy(b->name, copy, sizeof copy);
    b->tip = tip;
    return r->branchCount++;
}
/* The commit this one was built on (the first one, for a merge), or NULL. */
static Commit *ParentOf(Repo *r, int ci)
{
    Commit *c = &r->commits[ci];
    return c->parentCount ? &r->commits[c->parents[0]] : NULL;
}
static int CommitBySha(Repo *r, const uint8_t *sha) { return MapGet(&r->commitMap, sha); }

/* Commits keep their files sorted by path, so finding one is quick. */
static int FindInCommit(Commit *c, const char *name)
{
    int lo = 0, hi = c->fileCount - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2, d = strcmp(c->names[mid], name);
        if (!d) return mid;
        if (d < 0) lo = mid + 1; else hi = mid - 1;
    }
    return -1;
}

/* ---------- snapshots being put together ----------
   A list of files (kept sorted) that becomes a new commit. */
typedef struct { int n, cap; const char **names; int *blobs; uint8_t *modes; } Snap;

static void SnapFree(Snap *s) { free(s->names); free(s->blobs); free(s->modes); memset(s, 0, sizeof *s); }
static bool SnapRoom(Snap *s, int need)
{
    if (need <= s->cap) return true;
    int c = s->cap ? s->cap : 16;
    while (c < need) c *= 2;
    const char **n = realloc(s->names, (size_t)c * sizeof *n);
    if (n) s->names = n;
    int *b = realloc(s->blobs, (size_t)c * sizeof *b);
    if (b) s->blobs = b;
    uint8_t *m = realloc(s->modes, (size_t)c);
    if (m) s->modes = m;
    if (!n || !b || !m) return false;
    s->cap = c;
    return true;
}
/* Starts from a commit's files (or from nothing if c is NULL). */
static bool SnapFrom(Snap *s, Commit *c)
{
    memset(s, 0, sizeof *s);
    int n = c ? c->fileCount : 0;
    if (!SnapRoom(s, n > 0 ? n : 1)) return false;
    if (n) {
        memcpy(s->names, c->names, (size_t)n * sizeof *s->names);
        memcpy(s->blobs, c->blobs, (size_t)n * sizeof *s->blobs);
        memcpy(s->modes, c->modes, (size_t)n);
    }
    s->n = n;
    return true;
}
/* Where name is, or where it would go (as -1 - position). */
static int SnapFind(Snap *s, const char *name)
{
    int lo = 0, hi = s->n - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2, d = strcmp(s->names[mid], name);
        if (!d) return mid;
        if (d < 0) lo = mid + 1; else hi = mid - 1;
    }
    return -1 - lo;
}
/* Puts a file in (or replaces it). mode -1 = keep its mode (a normal file if it's new). */
static bool SnapSet(Snap *s, const char *name, int blob, int mode)
{
    int k = SnapFind(s, name);
    if (k >= 0) { s->blobs[k] = blob; if (mode >= 0) s->modes[k] = (uint8_t)mode; return true; }
    if (s->n >= MAX_FILES) return false;
    const char *in = Intern(name);
    if (!in || !SnapRoom(s, s->n + 1)) return false;
    k = -1 - k;
    memmove(&s->names[k + 1], &s->names[k], (size_t)(s->n - k) * sizeof *s->names);
    memmove(&s->blobs[k + 1], &s->blobs[k], (size_t)(s->n - k) * sizeof *s->blobs);
    memmove(&s->modes[k + 1], &s->modes[k], (size_t)(s->n - k));
    s->names[k] = in; s->blobs[k] = blob; s->modes[k] = (uint8_t)(mode >= 0 ? mode : MODE_FILE);
    s->n++;
    return true;
}
static void SnapRemove(Snap *s, const char *name)
{
    int k = SnapFind(s, name);
    if (k < 0) return;
    memmove(&s->names[k], &s->names[k + 1], (size_t)(s->n - k - 1) * sizeof *s->names);
    memmove(&s->blobs[k], &s->blobs[k + 1], (size_t)(s->n - k - 1) * sizeof *s->blobs);
    memmove(&s->modes[k], &s->modes[k + 1], (size_t)(s->n - k - 1));
    s->n--;
}

/* ---------- trees (folders) ----------
   Git stores each folder as a "tree": for every entry, its mode, name and
   fingerprint. Because the paths are sorted, each folder's files sit next
   to each other in the list, in exactly the order git wants. */
typedef void (*TreeEmit)(void *ctx, const uint8_t sha[20], const uint8_t *data, size_t len);

/* Works out the fingerprint of the folder holding entries lo..hi-1 (paths all
   starting with the same plen characters, e.g. "src/"). shas[i] is each
   file's blob fingerprint. emit, if given, is handed every tree made. */
static bool BuildTree(const char *const *names, const uint8_t (*shas)[20], const uint8_t *modes,
                      int lo, int hi, int plen, uint8_t out[20], TreeEmit emit, void *ctx)
{
    Buf t = {0};
    int i = lo;
    while (i < hi) {
        const char *rest = names[i] + plen;
        const char *slash = strchr(rest, '/');
        if (!slash) {
            BufStr(&t, ModeText(modes[i])); BufByte(&t, ' ');
            BufAdd(&t, rest, strlen(rest) + 1);
            BufAdd(&t, shas[i], 20);
            i++;
            continue;
        }
        size_t nl = (size_t)(slash - rest);
        int j = i + 1;
        while (j < hi && strncmp(names[j] + plen, rest, nl + 1) == 0) j++;
        uint8_t sub[20];
        if (!BuildTree(names, shas, modes, i, j, plen + (int)nl + 1, sub, emit, ctx)) { BufFree(&t); return false; }
        BufStr(&t, "40000 ");
        BufAdd(&t, rest, nl); BufByte(&t, 0);
        BufAdd(&t, sub, 20);
        i = j;
    }
    if (t.oom) { BufFree(&t); return false; }
    ObjectSha("tree", t.d ? t.d : (const uint8_t *)"", t.n, out);
    if (emit) emit(ctx, out, t.d ? t.d : (const uint8_t *)"", t.n);
    BufFree(&t);
    return true;
}
static bool CommitTree(Repo *r, Commit *c, uint8_t out[20], TreeEmit emit, void *ctx)
{
    uint8_t (*shas)[20] = malloc((size_t)(c->fileCount ? c->fileCount : 1) * 20);
    if (!shas) return false;
    for (int i = 0; i < c->fileCount; i++) memcpy(shas[i], r->blobs[c->blobs[i]].sha, 20);
    bool ok = BuildTree(c->names, (const uint8_t (*)[20])shas, c->modes, 0, c->fileCount, 0, out, emit, ctx);
    free(shas);
    return ok;
}

/* ---------- the git commit object ---------- */

/* Minutes ahead of UTC for local time at moment t (e.g. +60 for Paris in winter). */
static int TzOffset(long long t)
{
    time_t tt = (time_t)t;
    struct tm lt = *localtime(&tt), gt = *gmtime(&tt);
    int d = (lt.tm_hour - gt.tm_hour) * 60 + (lt.tm_min - gt.tm_min);
    int day = lt.tm_year != gt.tm_year ? (lt.tm_year > gt.tm_year ? 1 : -1) : lt.tm_yday - gt.tm_yday;
    return d + day * 1440;
}

/* What git calls the author: "name <email> time zone". */
static void AuthorLine(Buf *b, const char *who, const char *email, long long t)
{
    int tz = TzOffset(t), a = tz < 0 ? -tz : tz;
    BufStr(b, who); BufStr(b, " <"); BufStr(b, email); BufStr(b, "> ");
    char when[48];
    snprintf(when, sizeof when, "%lld %c%02d%02d", t, tz < 0 ? '-' : '+', a / 60, a % 60);
    BufStr(b, when);
}

static char *MakeRawCommit(const uint8_t tree[20], const uint8_t *const *parents, int np,
                           const char *who, const char *email, long long t, const char *msg, int *len)
{
    Buf b = {0};
    char hx[41];
    ShaHex(tree, hx);
    BufStr(&b, "tree "); BufStr(&b, hx); BufByte(&b, '\n');
    for (int i = 0; i < np; i++) { ShaHex(parents[i], hx); BufStr(&b, "parent "); BufStr(&b, hx); BufByte(&b, '\n'); }
    BufStr(&b, "author ");    AuthorLine(&b, who, email, t); BufByte(&b, '\n');
    BufStr(&b, "committer "); AuthorLine(&b, who, email, t); BufByte(&b, '\n');
    BufByte(&b, '\n');
    BufStr(&b, msg);
    BufByte(&b, '\n');
    if (b.oom) { BufFree(&b); return NULL; }
    *len = (int)b.n;
    BufByte(&b, 0);
    return (char *)b.d;
}

/* The parts of a git commit object Mini GitHub uses. */
#define MAX_PARENTS 16
typedef struct {
    uint8_t   tree[20];
    int       np;
    uint8_t   parents[MAX_PARENTS][20];
    char      aname[NAME_LEN * 2], aemail[TEXT_LEN];
    long long atime;
    char      summary[TEXT_LEN];
} CommitInfo;

static bool ParseCommit(const char *raw, int len, CommitInfo *ci)
{
    memset(ci, 0, sizeof *ci);
    int p = 0;
    bool tree = false;
    while (p < len) {
        int e = p;
        while (e < len && raw[e] != '\n') e++;
        if (e == p) { p = e + 1; break; }                 /* blank line: the message follows */
        const char *l = raw + p;
        int n = e - p;
        if (n == 45 && strncmp(l, "tree ", 5) == 0) tree = ParseHex40(l + 5, ci->tree);
        else if (n == 47 && strncmp(l, "parent ", 7) == 0) {
            if (ci->np >= MAX_PARENTS || !ParseHex40(l + 7, ci->parents[ci->np])) return false;
            ci->np++;
        } else if (n > 7 && strncmp(l, "author ", 7) == 0) {
            const char *lt = memchr(l, '<', (size_t)n), *gt = lt ? memchr(lt, '>', (size_t)(n - (lt - l))) : NULL;
            if (lt && gt) {
                int nl = (int)(lt - (l + 7));
                while (nl > 0 && l[7 + nl - 1] == ' ') nl--;
                snprintf(ci->aname, sizeof ci->aname, "%.*s", nl, l + 7);
                snprintf(ci->aemail, sizeof ci->aemail, "%.*s", (int)(gt - lt - 1), lt + 1);
                ci->atime = strtoll(gt + 1, NULL, 10);
            }
        }
        p = e + 1;
    }
    /* the summary: the message's first line that isn't blank */
    while (p < len && (raw[p] == '\n' || raw[p] == ' ' || raw[p] == '\r')) p++;
    int e = p;
    while (e < len && raw[e] != '\n') e++;
    while (e > p && (raw[e - 1] == '\r' || raw[e - 1] == ' ')) e--;
    snprintf(ci->summary, sizeof ci->summary, "%.*s", e - p, raw + p);
    if (!ci->summary[0]) snprintf(ci->summary, sizeof ci->summary, "(no message)");
    return tree;
}

static bool SameTextCI(const char *a, const char *b, int n)
{
    for (int i = 0; i < n; i++) if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) return false;
    return true;
}
/* Who to show as a commit's author: the Mini GitHub user, plus the name
   git has if that's different (e.g. "Jo Lalor (jo)" for a pushed commit). */
static const char *CommitAuthorName(Commit *c)
{
    const char *u = UserName(c->author);
    const char *p = c->raw ? strstr(c->raw, "\nauthor ") : NULL;
    if (!p) return u;
    p += 8;
    const char *lt = strchr(p, '<'), *nl = strchr(p, '\n');
    if (!lt || (nl && lt > nl)) return u;
    int n = (int)(lt - p);
    while (n > 0 && p[n - 1] == ' ') n--;
    if (n <= 0 || n > 60 || ((int)strlen(u) == n && SameTextCI(p, u, n))) return u;
    return TextFormat("%.*s (%s)", n, p, u);
}

/* Adds a finished commit to the end of the history (the repository owns its lists from now on). */
static bool AppendCommit(Repo *r, Commit *c)
{
    if (r->commitCount >= MAX_COMMITS) return false;
    r->commits = GrowArr(r->commits, &r->commitCap, r->commitCount + 1, sizeof(Commit));
    if (r->commitCount >= r->commitCap) return false;
    ObjectSha("commit", c->raw, (size_t)c->rawLen, c->sha);
    char hx[41];
    ShaHex(c->sha, hx);
    snprintf(c->id, sizeof c->id, "%.7s", hx);
    c->statsDone = false;
    MapPut(&r->commitMap, c->sha, r->commitCount);
    r->commits[r->commitCount++] = *c;
    r->gen++;
    return true;
}

/* A file can't have the same name as a folder ("docs" and "docs/guide.md"):
   git couldn't store that. Returns the clashing name, or NULL. */
static const char *PathClash(const char *const *names, int n)
{
    char pre[PATH_LEN + 2];
    for (int i = 0; i < n; i++) {
        snprintf(pre, sizeof pre, "%s/", names[i]);
        size_t pl = strlen(pre);
        int lo = i + 1, hi = n;                              /* the first name >= "name/" */
        while (lo < hi) { int mid = (lo + hi) / 2; if (strcmp(names[mid], pre) < 0) lo = mid + 1; else hi = mid; }
        if (lo < n && strncmp(names[lo], pre, pl) == 0) return names[i];
    }
    return NULL;
}

/* Turns a snapshot into a new commit by the signed-in user, on top of the
   latest commit (and, for a merge, also on top of commit "also" - or -1).
   The snapshot is used up either way. Returns false if there was no room. */
static bool FinishCommit(Repo *r, Snap *s, const char *msg, int also)
{
    Commit c;
    memset(&c, 0, sizeof c);
    c.fileCount = s->n;
    c.names = s->names; c.blobs = s->blobs; c.modes = s->modes;
    memset(s, 0, sizeof *s);
    if (!c.names) { c.names = malloc(sizeof *c.names); c.blobs = malloc(sizeof *c.blobs); c.modes = malloc(1); }
    c.author = curUser;
    c.time = (long long)time(NULL);
    snprintf(c.message, TEXT_LEN, "%s", msg);
    const uint8_t *par[2];
    int np = 0;
    int head = HeadIndex(r);
    if (head >= 0)      { c.parents[np] = head;               par[np++] = r->commits[head].sha; }
    if (also >= 0)      { c.parents[np] = also;               par[np++] = r->commits[also].sha; }
    c.parentCount = np;
    uint8_t tree[20];
    if (c.names && CommitTree(r, &c, tree, NULL, NULL))
        c.raw = MakeRawCommit(tree, par, np, UserName(curUser), UserEmail(curUser), c.time, msg, &c.rawLen);
    if (!c.raw || !AppendCommit(r, &c)) { FreeCommit(&c); return false; }
    /* the branch moves forward to the new commit (the very first commit makes the first branch) */
    if (r->branchCount) r->branches[r->curBranch].tip = r->commitCount - 1;
    else { AddBranch(r, r->branch, r->commitCount - 1); r->defBranch = r->curBranch = 0; }
    return true;
}

/* ---------- drafts ---------- */

/* The blob a file has in the latest commit, or -1 if it isn't there. */
static int HeadBlobOf(Repo *r, const char *name)
{
    Commit *h = Head(r);
    int k = h ? FindInCommit(h, name) : -1;
    return k >= 0 ? h->blobs[k] : -1;
}

static bool SameAsBlob(Repo *r, WorkFile *f, int b)
{
    return b >= 0 && r->blobs[b].len == f->len && memcmp(r->blobs[b].data, f->text, (size_t)f->len) == 0;
}

/* uid's changes on the branch being worked on. */
static Draft *GetDraft(Repo *r, int uid, bool create)
{
    const char *br = CurBranchName(r);
    for (int i = 0; i < r->draftCount; i++) if (r->drafts[i].user == uid && strcmp(r->drafts[i].branch, br) == 0) return &r->drafts[i];
    if (!create || r->draftCount >= MAX_DRAFTS) return NULL;
    WorkFile *f = calloc(MAX_DRAFT, sizeof(WorkFile));
    if (!f) return NULL;
    Draft *d = &r->drafts[r->draftCount++];
    memset(d, 0, sizeof *d);
    d->user = uid; d->count = 0; d->files = f;
    snprintf(d->branch, NAME_LEN, "%s", br);
    return d;
}

static int DraftIndex(Draft *d, const char *name)
{
    for (int i = 0; d && i < d->count; i++) if (strcmp(d->files[i].name, name) == 0) return i;
    return -1;
}
static WorkFile *DraftFind(Repo *r, int uid, const char *name)
{
    Draft *d = GetDraft(r, uid, false);
    int i = DraftIndex(d, name);
    return i >= 0 ? &d->files[i] : NULL;
}

static void DraftDrop(Draft *d, int i)
{
    WF_Free(&d->files[i]);
    memmove(&d->files[i], &d->files[i + 1], (size_t)(d->count - i - 1) * sizeof(WorkFile));
    d->count--;
    memset(&d->files[d->count], 0, sizeof(WorkFile));
}
static void DraftDropName(Repo *r, int uid, const char *name)
{
    Draft *d = GetDraft(r, uid, false);
    int i = DraftIndex(d, name);
    if (i >= 0) DraftDrop(d, i);
}
static void DraftClear(Repo *r, int uid)
{
    Draft *d = GetDraft(r, uid, false);
    while (d && d->count) DraftDrop(d, d->count - 1);
    if (d) d->merging = false;
}
/* Throws away drafts: uid's (on every branch) if branch is NULL, or everyone's on that branch if uid is -1. */
static void RemoveDrafts(Repo *r, int uid, const char *branch)
{
    for (int i = r->draftCount - 1; i >= 0; i--)
        if ((uid < 0 || r->drafts[i].user == uid) && (!branch || strcmp(r->drafts[i].branch, branch) == 0)) {
            FreeDraft(&r->drafts[i]);
            memmove(&r->drafts[i], &r->drafts[i + 1], (size_t)(r->draftCount - i - 1) * sizeof(Draft));
            r->draftCount--;
            memset(&r->drafts[r->draftCount], 0, sizeof(Draft));
        }
}
static void RemoveDraft(Repo *r, int uid) { RemoveDrafts(r, uid, NULL); }

/* Your own editable copy of a file: made from the latest commit the first
   time you change it (or empty, for a new file). NULL if your draft is full. */
static WorkFile *DraftEdit(Repo *r, int uid, const char *name)
{
    Draft *d = GetDraft(r, uid, true);
    if (!d) return NULL;
    int i = DraftIndex(d, name);
    if (i >= 0) return &d->files[i];
    if (d->count >= MAX_DRAFT || strlen(name) >= PATH_LEN) return NULL;
    WorkFile *f = &d->files[d->count++];
    memset(f, 0, sizeof *f);
    snprintf(f->name, PATH_LEN, "%s", name);
    f->base = HeadBlobOf(r, name);
    if (f->base >= 0) WF_SetN(f, r->blobs[f->base].data, r->blobs[f->base].len);
    else              WF_SetN(f, "", 0);
    return f;
}
static void DraftSetText(Repo *r, int uid, const char *name, const char *text)
{
    WorkFile *f = DraftEdit(r, uid, name);
    if (f) { f->deleted = false; WF_Set(f, text); }
}

/* What a file in your draft means:
   'A' a new file, 'M' modified, 'D' deleted, 0 no real change,
   'C' a conflict - someone committed a different version of this file
       after you started changing it, so committing yours would wipe theirs. */
static char DraftStatus(Repo *r, WorkFile *e)
{
    int cur = HeadBlobOf(r, e->name);
    if (e->deleted) {
        if (cur < 0) return 0;                            /* it's gone anyway */
        return e->base == cur ? 'D' : 'C';                /* changed by someone after you deleted it */
    }
    if (SameAsBlob(r, e, cur)) return 0;                  /* yours matches the latest version */
    if (e->base >= 0 && SameAsBlob(r, e, e->base)) return 0;   /* you haven't actually changed it */
    if (e->base != cur) return 'C';
    return cur < 0 ? 'A' : 'M';
}

/* Throws away draft files that aren't real changes, so they quietly follow
   the latest commit again. */
static void DraftTidy(Repo *r, int uid)
{
    Draft *d = GetDraft(r, uid, false);
    if (!d) return;
    for (int i = d->count - 1; i >= 0; i--) if (!DraftStatus(r, &d->files[i])) DraftDrop(d, i);
}

static void DraftCounts(Repo *r, int uid, int *changes, int *conflicts)
{
    *changes = *conflicts = 0;
    Draft *d = GetDraft(r, uid, false);
    for (int i = 0; d && i < d->count; i++) {
        char st = DraftStatus(r, &d->files[i]);
        if (st == 'C') (*conflicts)++;
        else if (st)   (*changes)++;
    }
}

/* Who most recently committed a change to this file (or -1): follows the
   latest commit back through the commits it was built on. */
static int LastChangedBy(Repo *r, const char *name)
{
    int k = HeadIndex(r);
    while (k >= 0) {
        Commit *c = &r->commits[k], *p = ParentOf(r, k);
        int now = FindInCommit(c, name);
        int nb = now >= 0 ? c->blobs[now] : -1, before = -1;
        if (p) { int q = FindInCommit(p, name); before = q >= 0 ? p->blobs[q] : -1; }
        if (nb != before) return c->author;
        k = c->parentCount ? c->parents[0] : -1;
    }
    return -1;
}

/* What you see in a repository: the latest commit with your own draft laid
   on top. With uid = -1 it's just the latest commit (for read-only visitors). */
typedef struct { const char *name; const char *data; int len; bool binary; char status; WorkFile *mine; int blob; } ViewFile;
static ViewFile *view;
static int       viewCount, viewCap;

static int CmpView(const void *a, const void *b) { return strcmp(((const ViewFile *)a)->name, ((const ViewFile *)b)->name); }

static void BuildView(Repo *r, int uid)
{
    viewCount = 0;
    Commit *h = Head(r);
    Draft *d = uid >= 0 ? GetDraft(r, uid, false) : NULL;
    int most = (h ? h->fileCount : 0) + (d ? d->count : 0);
    view = GrowArr(view, &viewCap, most + 1, sizeof(ViewFile));
    if (viewCap < most) return;
    for (int k = 0; h && k < h->fileCount; k++) {
        ViewFile *v = &view[viewCount++];
        memset(v, 0, sizeof *v);
        v->name = h->names[k];
        v->blob = h->blobs[k];
        int i = DraftIndex(d, v->name);
        WorkFile *e = i >= 0 ? &d->files[i] : NULL;
        char st = e ? DraftStatus(r, e) : 0;
        if (e && st) { v->mine = e; v->status = st; v->data = e->text; v->len = e->len; v->binary = e->binary; }
        else { v->data = r->blobs[v->blob].data; v->len = r->blobs[v->blob].len; v->binary = r->blobs[v->blob].binary; }
    }
    bool added = false;
    for (int i = 0; d && i < d->count; i++) {                 /* your new files */
        WorkFile *e = &d->files[i];
        if (HeadBlobOf(r, e->name) >= 0) continue;
        char st = DraftStatus(r, e);
        if (!st) continue;
        const char *nm = Intern(e->name);
        if (!nm) continue;
        ViewFile *v = &view[viewCount++];
        memset(v, 0, sizeof *v);
        v->name = nm;
        v->blob = -1; v->mine = e; v->status = st; v->data = e->text; v->len = e->len; v->binary = e->binary;
        added = true;
    }
    if (added) qsort(view, (size_t)viewCount, sizeof(ViewFile), CmpView);   /* keep folders together */
}
static int ViewIndex(const char *name)
{
    int lo = 0, hi = viewCount - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2, d = strcmp(view[mid].name, name);
        if (!d) return mid;
        if (d < 0) lo = mid + 1; else hi = mid - 1;
    }
    return -1;
}
static bool ViewDeleted(ViewFile *v) { return v->mine && v->mine->deleted; }

static int OpenIssues(Repo *r)
{
    int n = 0;
    for (int i = 0; i < r->issueCount; i++) if (r->issues[i].open) n++;
    return n;
}

static const char *MarkersLeft(Repo *r);

/* Returns NULL on success, or a message explaining what went wrong. */
static const char *DoCommit(Repo *r, const char *msg)
{
    if (!msg[0]) return "Write a commit message first";
    DraftTidy(r, curUser);
    int changes, conflicts;
    DraftCounts(r, curUser, &changes, &conflicts);
    Draft *md = GetDraft(r, curUser, false);
    bool merging = md && md->merging;
    int also = merging ? CommitBySha(r, md->mergeSha) : -1;
    if (conflicts)                     return "Sort out the conflicts first (files marked !)";
    if (!changes && !merging)          return "Nothing to commit - no changes since the last commit";
    if (r->commitCount >= MAX_COMMITS) return "This repository has reached its commit limit";
    if (merging && also < 0)           return "The branch being merged has gone - abort the merge";
    if (merging && MarkersLeft(r))     return TextFormat("%s still has conflict markers (<<<<<<<) - fix those parts first", MarkersLeft(r));

    /* start from the latest commit, then apply your changes */
    Draft *d = GetDraft(r, curUser, false);
    Snap s;
    if (!SnapFrom(&s, Head(r))) return "Out of memory";
    for (int i = 0; d && i < d->count; i++) {
        WorkFile *e = &d->files[i];
        char st = DraftStatus(r, e);
        if (st == 'D') { SnapRemove(&s, e->name); continue; }
        int b = InternBlob(r, e->text, e->len);
        if (b < 0) { SnapFree(&s); return "This repository is out of storage space"; }
        if (!SnapSet(&s, e->name, b, -1)) { SnapFree(&s); return TextFormat("A repository can hold up to %d files", MAX_FILES); }
    }
    const char *clash = PathClash(s.names, s.n);
    if (clash) { SnapFree(&s); return TextFormat("%s is both a file and a folder - rename one of them first", clash); }
    if (!FinishCommit(r, &s, msg, also)) return "Couldn't save the commit (out of memory?)";
    DraftClear(r, curUser);                 /* everything is committed now (and a merge is finished) */
    return NULL;
}

/* Replace the working files with the files from an old commit. */
static void RestoreCommit(Repo *r, int ci)
{
    DraftClear(r, curUser);
    Commit *c = &r->commits[ci], *h = Head(r);
    for (int k = 0; k < c->fileCount; k++) {
        if (HeadBlobOf(r, c->names[k]) == c->blobs[k]) continue;        /* already the same */
        WorkFile *e = DraftEdit(r, curUser, c->names[k]);
        if (e) WF_SetN(e, r->blobs[c->blobs[k]].data, r->blobs[c->blobs[k]].len);
    }
    for (int k = 0; h && k < h->fileCount; k++)                          /* files that didn't exist back then */
        if (FindInCommit(c, h->names[k]) < 0) {
            WorkFile *e = DraftEdit(r, curUser, h->names[k]);
            if (e) e->deleted = true;
        }
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
/* The email git puts on commits made here: the one set in your profile, or a made-up one. */
static const char *UserEmail(int id)
{
    static char made[NAME_LEN + 24];
    User *u = FindUserById(id);
    if (u && u->email[0]) return u->email;
    snprintf(made, sizeof made, "%s@minigithub.local", u ? u->name : "ghost");
    return made;
}

static bool IsMine(Repo *r) { return curUser >= 0 && r->owner == curUser; }

/* ---------- collaborators ---------- */
static bool IsCollab(Repo *r, int uid)
{
    for (int i = 0; i < r->collabCount; i++) if (r->collabs[i] == uid) return true;
    return false;
}
static bool IsInvited(Repo *r, int uid)
{
    for (int i = 0; i < r->inviteCount; i++) if (r->invites[i] == uid) return true;
    return false;
}
/* Owners and collaborators can change a repository; everyone else can only look. */
static bool CanEditBy(Repo *r, int uid) { return uid >= 0 && (r->owner == uid || IsCollab(r, uid)); }
static bool CanEdit(Repo *r)            { return CanEditBy(r, curUser); }

static void RemoveInvite(Repo *r, int uid)
{
    for (int i = 0; i < r->inviteCount; i++)
        if (r->invites[i] == uid) {
            memmove(&r->invites[i], &r->invites[i + 1], (size_t)(r->inviteCount - i - 1) * sizeof(int));
            memmove(&r->inviteTime[i], &r->inviteTime[i + 1], (size_t)(r->inviteCount - i - 1) * sizeof(long long));
            r->inviteCount--;
            return;
        }
}
/* Takes someone off a repository: their collaborator place, any invitation,
   and their uncommitted draft. Their commits stay in the history. */
static void RemoveMember(Repo *r, int uid)
{
    for (int i = 0; i < r->collabCount; i++)
        if (r->collabs[i] == uid) {
            memmove(&r->collabs[i], &r->collabs[i + 1], (size_t)(r->collabCount - i - 1) * sizeof(int));
            r->collabCount--;
            break;
        }
    RemoveInvite(r, uid);
    RemoveDraft(r, uid);
}

/* Returns NULL if the invitation was sent, or a message saying why not. */
static const char *InviteUser(Repo *r, const char *name)
{
    if (!IsMine(r)) return "Only the owner can invite people";
    char nm[TEXT_LEN];                                    /* the name without spaces at either end */
    int a = 0, n = (int)strlen(name);
    while (a < n && name[a] == ' ') a++;
    while (n > a && name[n - 1] == ' ') n--;
    snprintf(nm, sizeof nm, "%.*s", n - a, name + a);
    if (!nm[0]) return "Type the username of the person to invite";
    User *u = FindUserByName(nm);
    if (!u)                       return TextFormat("There's no user called \"%.40s\" on this computer", nm);
    if (u->id == r->owner)        return "You already own this repository";
    if (IsCollab(r, u->id))       return TextFormat("%s is already a collaborator", u->name);
    if (IsInvited(r, u->id))      return TextFormat("%s has already been invited", u->name);
    if (r->inviteCount + r->collabCount >= MAX_USERS - 1) return "This repository can't have any more collaborators";
    r->invites[r->inviteCount] = u->id;
    r->inviteTime[r->inviteCount] = (long long)time(NULL);
    r->inviteCount++;
    return NULL;
}
static void AcceptInvite(Repo *r, int uid)
{
    if (!IsInvited(r, uid)) return;
    RemoveInvite(r, uid);
    if (!IsCollab(r, uid) && r->collabCount < MAX_USERS) r->collabs[r->collabCount++] = uid;
}
static int InvitationCount(int uid)
{
    int n = 0;
    for (int i = 0; i < repoCount; i++) if (IsInvited(&repos[i], uid)) n++;
    return n;
}

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

/* How many files you see: the latest commit plus your own draft if you can
   edit; just the latest commit if you're a visitor. */
static int ShownCount(Repo *r)
{
    BuildView(r, CanEdit(r) ? curUser : -1);
    int n = 0;
    for (int i = 0; i < viewCount; i++) if (!ViewDeleted(&view[i])) n++;
    return n;
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
        Draft *d = GetDraft(r, -1, false);
        if (d) d->user = uid;
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
    for (int i = 0; i < repoCount; i++) {
        RemoveStar(&repos[i], uid);
        RemoveMember(&repos[i], uid);                  /* collaborator places and invitations */
    }
    for (int i = 0; i < userCount; i++)
        if (users[i].id == uid) {
            if (users[i].texState == 1) UnloadTexture(users[i].tex);
            free(users[i].avatar);
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
#ifdef _WIN32
    bool good = true;
    for (int i = 0; i < n && good; i++) {
        unsigned int v;
        if (rand_s(&v) != 0) good = false;
        else out[i] = (uint8_t)v;
    }
    if (good) return;
#endif
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
/* Recovery codes ("forgot password")                                 */
/*                                                                    */
/* There's no email on a local app, so instead every account can have */
/* a recovery code like  K7QM-2XWD-9PRT-H4NC . It is shown once, and   */
/* only its hash is saved - exactly like the password. Using it to    */
/* reset the password uses it up, and a fresh one is shown.           */
/* ------------------------------------------------------------------ */
#define CODE_CHARS 16
/* 32 letters/digits with the easily confused ones (0 O 1 I) left out */
static const char CODE_ALPHABET[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";

/* Keep only letters and digits, in capitals, so "k7qm 2xwd-..." still works.
   Returns how many characters were kept. */
static int NormalizeCode(const char *in, char out[CODE_CHARS + 1])
{
    int n = 0;
    for (int i = 0; in[i]; i++) {
        if (!isalnum((unsigned char)in[i])) continue;
        if (n == CODE_CHARS) return CODE_CHARS + 1;          /* too long */
        out[n++] = (char)toupper((unsigned char)in[i]);
    }
    out[n] = 0;
    return n;
}

static bool HasRecoveryCode(User *u) { return u->rhash[0] != 0; }

/* Makes a new code for u (the old one stops working) and writes the
   readable "XXXX-XXXX-XXXX-XXXX" form into shown. */
static void NewRecoveryCode(User *u, char shown[24])
{
    uint8_t rnd[CODE_CHARS], salt[16];
    char plain[CODE_CHARS + 1];
    RandomBytes(rnd, CODE_CHARS);
    for (int i = 0; i < CODE_CHARS; i++) plain[i] = CODE_ALPHABET[rnd[i] % 32];   /* 256 % 32 == 0: no bias */
    plain[CODE_CHARS] = 0;
    snprintf(shown, 24, "%.4s-%.4s-%.4s-%.4s", plain, plain + 4, plain + 8, plain + 12);

    RandomBytes(salt, 16);
    ToHex(salt, 16, u->rsalt);
    HashPassword(plain, u->rsalt, u->rhash);
    memset(plain, 0, sizeof plain);
    memset(rnd, 0, sizeof rnd);
}

static bool CheckRecoveryCode(User *u, const char *typed)
{
    char norm[CODE_CHARS + 1], h[65];
    if (!HasRecoveryCode(u) || NormalizeCode(typed, norm) != CODE_CHARS) return false;
    HashPassword(norm, u->rsalt, h);
    unsigned diff = 0;
    for (int i = 0; i < 64; i++) diff |= (unsigned)(h[i] ^ u->rhash[i]);
    return diff == 0;
}


/* ------------------------------------------------------------------ */
/* Diff: which lines were added, removed or kept                       */
/* (longest common subsequence of lines)                               */
/* ------------------------------------------------------------------ */
typedef struct { const char *s; int n; } LineRef;
typedef struct { char kind; int a, b; } DiffOp;   /* kind: ' ', '+', '-' */

/* These grow as needed, so files of any length can be compared. */
static LineRef *dA, *dB;
static DiffOp  *dOps;
static int      dACap, dBCap, dOpsCap, dOpCount;

static void *Grow(void *p, int *cap, int need, size_t item)
{
    if (need <= *cap) return p;
    int c = *cap ? *cap : 256;
    while (c < need) c *= 2;
    void *q = realloc(p, (size_t)c * item);
    if (!q) return p;                         /* out of memory: keep the old one */
    *cap = c;
    return q;
}

/* Cuts text into lines (without the newline characters). Returns how many. */
static int SplitLines(const char *t, LineRef **out, int *cap)
{
    int c = 0;
    const char *p = t;
    while (*p) {
        *out = Grow(*out, cap, c + 1, sizeof(LineRef));
        if (c >= *cap) break;
        const char *e = strchr(p, '\n');
        (*out)[c].s = p;
        (*out)[c].n = e ? (int)(e - p) : (int)strlen(p);
        c++;
        if (!e) break;
        p = e + 1;
    }
    return c;
}
static bool SameLine(LineRef x, LineRef y) { return x.n == y.n && memcmp(x.s, y.s, (size_t)x.n) == 0; }

/* Works out the smallest set of added (+) and removed (-) lines that turns
   oldT into newT, using the "longest common subsequence" method: find the
   longest list of lines both versions share in order; everything else was
   added or removed.

   Lines that are the same at the very start and very end are matched first,
   so a small change in a big file is still quick. If the part in the middle
   is still huge, it's simply shown as all removed then all added. */
static void ComputeDiff(const char *oldT, const char *newT, int *adds, int *dels)
{
    int n = SplitLines(oldT, &dA, &dACap), m = SplitLines(newT, &dB, &dBCap);
    int ad = 0, de = 0;
    dOps = Grow(dOps, &dOpsCap, n + m + 1, sizeof(DiffOp));
    dOpCount = 0;
    if (dOpsCap < n + m + 1) { if (adds) *adds = 0; if (dels) *dels = 0; return; }

    int pre = 0, suf = 0;
    while (pre < n && pre < m && SameLine(dA[pre], dB[pre])) pre++;
    while (suf < n - pre && suf < m - pre && SameLine(dA[n - 1 - suf], dB[m - 1 - suf])) suf++;
    for (int k = 0; k < pre; k++) dOps[dOpCount++] = (DiffOp){' ', k, k};

    /* the middle part: rows i = pre .. n-suf-1 of the old, j = pre .. m-suf-1 of the new */
    int N = n - pre - suf, M = m - pre - suf, i = 0, j = 0;
    int *L = NULL;
    if (N > 0 && M > 0 && (long long)(N + 1) * (M + 1) <= 6000000LL)
        L = calloc((size_t)(N + 1) * (size_t)(M + 1), sizeof(int));
    if (L) {
        int w = M + 1;
        for (int x = N - 1; x >= 0; x--)
            for (int y = M - 1; y >= 0; y--) {
                if (SameLine(dA[pre + x], dB[pre + y])) L[x * w + y] = L[(x + 1) * w + y + 1] + 1;
                else {
                    int down = L[(x + 1) * w + y], right = L[x * w + y + 1];
                    L[x * w + y] = down > right ? down : right;
                }
            }
        while (i < N && j < M) {
            if (SameLine(dA[pre + i], dB[pre + j]))          { dOps[dOpCount++] = (DiffOp){' ', pre + i, pre + j}; i++; j++; }
            else if (L[(i + 1) * w + j] >= L[i * w + j + 1]) { dOps[dOpCount++] = (DiffOp){'-', pre + i, -1}; i++; de++; }
            else                                             { dOps[dOpCount++] = (DiffOp){'+', -1, pre + j}; j++; ad++; }
        }
        free(L);
    }
    while (i < N) { dOps[dOpCount++] = (DiffOp){'-', pre + i, -1}; i++; de++; }
    while (j < M) { dOps[dOpCount++] = (DiffOp){'+', -1, pre + j}; j++; ad++; }
    for (int k = 0; k < suf; k++) dOps[dOpCount++] = (DiffOp){' ', n - suf + k, m - suf + k};
    if (adds) *adds = ad;
    if (dels) *dels = de;
}

/* The list of files a commit changed, compared with the commit before it. */
typedef struct { const char *name; int oldB, newB; char kind; int adds, dels; bool binary; } Change;
static Change changes[MAX_FILES * 2];
static int    changeCount, totalAdds, totalDels;

/* Walks two commits' file lists side by side (both are sorted by path) and
   calls back for each file that differs: old or new blob is -1 if it isn't there. */
typedef void (*DiffEach)(void *ctx, const char *name, int oldB, int newB);
static void WalkChanges(Commit *p, Commit *c, DiffEach fn, void *ctx)
{
    int i = 0, j = 0, pn = p ? p->fileCount : 0, cn = c->fileCount;
    while (i < pn || j < cn) {
        int d = i >= pn ? 1 : j >= cn ? -1 : p->names[i] == c->names[j] ? 0 : strcmp(p->names[i], c->names[j]);
        if (d == 0) { if (p->blobs[i] != c->blobs[j] || p->modes[i] != c->modes[j]) fn(ctx, c->names[j], p->blobs[i], c->blobs[j]); i++; j++; }
        else if (d < 0) { fn(ctx, p->names[i], p->blobs[i], -1); i++; }
        else            { fn(ctx, c->names[j], -1, c->blobs[j]); j++; }
    }
}

static void AddChange(void *ctx, const char *name, int oldB, int newB)
{
    (void)ctx;
    if (changeCount >= MAX_FILES * 2) return;
    Change *ch = &changes[changeCount++];
    memset(ch, 0, sizeof *ch);
    ch->name = name; ch->oldB = oldB; ch->newB = newB;
    ch->kind = oldB < 0 ? 'A' : newB < 0 ? 'D' : 'M';
}

/* The list of files a commit changed, compared with the commit it was built on. */
static void BuildChanges(Repo *r, int ci)
{
    changeCount = totalAdds = totalDels = 0;
    WalkChanges(ParentOf(r, ci), &r->commits[ci], AddChange, NULL);
    for (int k = 0; k < changeCount; k++) {
        Change *ch = &changes[k];
        ch->binary = BlobBinary(r, ch->oldB) || BlobBinary(r, ch->newB);
        if (ch->binary) { ch->adds = ch->dels = 0; continue; }     /* no lines to compare */
        if (ch->oldB == ch->newB) continue;                        /* only the mode changed */
        ComputeDiff(BlobText(r, ch->oldB), BlobText(r, ch->newB), &ch->adds, &ch->dels);
        totalAdds += changes[k].adds;
        totalDels += changes[k].dels;
    }
}

/* ------------------------------------------------------------------ */
/* Themes                                                             */
/*                                                                    */
/* Each theme is drawn with simple shapes every frame, so it can move. */
/* "Random" positions come from a hash of the item's number, so they   */
/* are the same every frame (things don't jump around).               */
/* ------------------------------------------------------------------ */
enum { THEME_CLASSIC, THEME_CODE, THEME_GARDEN, THEME_NIGHT, THEME_CIRCUIT, THEME_OCEAN, THEME_AUTUMN, THEME_COUNT };

typedef struct { const char *name, *blurb; Color accent; } ThemeInfo;
static const ThemeInfo THEMES[THEME_COUNT] = {
    {"Classic",       "Plain and dark",       {247, 129, 102, 255}},
    {"Code rain",     "Falling green code",                 { 63, 185,  80, 255}},
    {"Flower garden", "Flowers and petals", {255, 143, 171, 255}},
    {"Night sky",     "Stars and a moon", {200, 170, 255, 255}},
    {"Circuit board", "Glowing circuits",           { 45, 212, 191, 255}},
    {"Ocean",         "Waves and bubbles",          { 88, 166, 255, 255}},
    {"Autumn",        "Falling leaves",                     {230, 126,  34, 255}},
};

/* A repeatable "random" number from 0 to 1 for item a, property b. */
static float Rnd(int a, int b)
{
    uint32_t h = (uint32_t)a * 374761393u + (uint32_t)b * 668265263u + 0x9e3779b9u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return (float)(h & 0xFFFFFF) / 16777216.0f;
}

static Color Alpha(Color c, float a)
{
    if (a < 0) a = 0;
    if (a > 1) a = 1;
    c.a = (unsigned char)(c.a * a);
    return c;
}

/* Colour fades drawn as thin strips. (raylib has functions for this, but their
   arguments have changed between raylib versions, so these work with any of them.) */
static Color Mix(Color a, Color b, float t)
{
    return (Color){(unsigned char)(a.r + (b.r - a.r) * t), (unsigned char)(a.g + (b.g - a.g) * t),
                   (unsigned char)(a.b + (b.b - a.b) * t), (unsigned char)(a.a + (b.a - a.a) * t)};
}
static void GradientV(int x, int y, int w, int h, Color top, Color bottom)
{
    for (int i = 0; i < h; i += 4) DrawRectangle(x, y + i, w, h - i < 4 ? h - i : 4, Mix(top, bottom, h > 1 ? (float)i / (h - 1) : 0));
}
static void GradientH(int x, int y, int w, int h, Color left, Color right)
{
    for (int i = 0; i < w; i += 4) DrawRectangle(x + i, y, w - i < 4 ? w - i : 4, h, Mix(left, right, w > 1 ? (float)i / (w - 1) : 0));
}

/* raylib only fills triangles whose corners go anticlockwise on screen, so fix the order. */
static void Tri(Vector2 a, Vector2 b, Vector2 c, Color col)
{
    float cross = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    if (cross > 0) DrawTriangle(a, c, b, col);
    else           DrawTriangle(a, b, c, col);
}

static int ItemCount(Rectangle r, float areaPer, int cap)
{
    int n = (int)(r.width * r.height / areaPer);
    return n < 6 ? 6 : n > cap ? cap : n;
}

static void ThemeCode(Rectangle r, float t, float k)
{
    static const char glyphs[] = "01{}[]()<>=+-*/;:#&|!?$%^~ifelsfnvarint";
    int ng = (int)sizeof glyphs - 1, cols = (int)(r.width / 20) + 1;
    DrawRectangleRec(r, Alpha((Color){0, 30, 10, 255}, 0.35f * k));
    for (int c = 0; c < cols; c++) {
        float speed = 50 + Rnd(c, 1) * 110, len = 6 + Rnd(c, 2) * 16, period = r.height + len * 20 + 200;
        float head = fmodf(t * speed + Rnd(c, 3) * period, period) - 100;
        for (int i = 0; i < (int)len; i++) {
            float y = head - i * 20;
            if (y < -20 || y > r.height) continue;
            int cell = (int)floorf((y + 10000) / 20);
            int g = (int)(Rnd(c * 977 + cell, (int)(t * 1.5f + Rnd(c, cell) * 20)) * ng) % ng;
            char s[2] = {glyphs[g], 0};
            float a = (1.0f - (float)i / len) * 0.75f * k;
            Color col = i == 0 ? (Color){200, 255, 210, 255} : (Color){40, 200, 90, 255};
            DrawText(s, (int)(r.x + c * 20 + 4), (int)(r.y + y), 18, Alpha(col, a));
        }
    }
}

static void Flower(Vector2 p, float s, float rot, Color petal, float a)
{
    for (int i = 0; i < 5; i++) {
        float ang = rot + i * 72 * DEG2RAD;
        Vector2 q = {p.x + cosf(ang) * s * 0.85f, p.y + sinf(ang) * s * 0.85f};
        DrawCircleV(q, s * 0.6f, Alpha(petal, a));
    }
    DrawCircleV(p, s * 0.42f, Alpha((Color){255, 214, 102, 255}, a * 1.2f));
    DrawCircleV(p, s * 0.2f, Alpha((Color){180, 110, 40, 255}, a));
}

static void ThemeGarden(Rectangle r, float t, float k)
{
    static const Color pal[5] = {{255, 143, 171, 255}, {196, 160, 255, 255}, {255, 190, 140, 255},
                                 {240, 240, 250, 255}, {255, 214, 102, 255}};
    GradientV((int)r.x, (int)r.y, (int)r.width, (int)r.height,
                           Alpha((Color){40, 20, 50, 255}, 0.3f * k), Alpha((Color){20, 60, 35, 255}, 0.45f * k));
    int n = ItemCount(r, 9000, 220);
    for (int i = 0; i < n; i++) {
        Vector2 p = {r.x + Rnd(i, 1) * r.width, r.y + Rnd(i, 2) * r.height};
        float s = 5 + Rnd(i, 3) * 9;
        float sway = sinf(t * 0.9f + Rnd(i, 4) * 6.28f) * 10 * DEG2RAD;
        DrawEllipse((int)(p.x - s * 1.3f), (int)(p.y + s * 1.1f), s * 0.9f, s * 0.35f, Alpha((Color){70, 160, 90, 255}, 0.5f * k));
        Flower(p, s, Rnd(i, 5) * 6.28f + sway, pal[(int)(Rnd(i, 6) * 5) % 5], 0.85f * k);
    }
    for (int i = 0; i < 30; i++) {                     /* petals drifting in the breeze */
        float x = r.x + fmodf(Rnd(i, 11) * r.width + t * (20 + Rnd(i, 12) * 25), r.width);
        float y = r.y + fmodf(Rnd(i, 13) * r.height + t * (25 + Rnd(i, 14) * 20), r.height) + sinf(t + i) * 6;
        DrawEllipse((int)x, (int)y, 4, 2.5f, Alpha(pal[i % 3], 0.6f * k));
    }
}

static void ThemeNight(Rectangle r, float t, float k)
{
    GradientV((int)r.x, (int)r.y, (int)r.width, (int)r.height,
                           Alpha((Color){10, 20, 60, 255}, 0.75f * k), Alpha((Color){50, 25, 80, 255}, 0.5f * k));
    int n = ItemCount(r, 2200, 650);
    for (int i = 0; i < n; i++) {
        Vector2 p = {r.x + Rnd(i, 1) * r.width, r.y + Rnd(i, 2) * r.height};
        float tw = 0.35f + 0.65f * (0.5f + 0.5f * sinf(t * (0.8f + Rnd(i, 3) * 2.5f) + Rnd(i, 4) * 6.28f));
        float s = 0.8f + Rnd(i, 5) * 1.6f;
        Color c = Rnd(i, 6) < 0.7f ? (Color){255, 255, 255, 255} : Rnd(i, 6) < 0.85f ? (Color){180, 200, 255, 255}
                                                                                      : (Color){255, 230, 170, 255};
        DrawCircleV(p, s, Alpha(c, tw * k));
        if (s > 2.1f) DrawCircleV(p, s * 3, Alpha(c, tw * 0.12f * k));
    }
    /* crescent moon */
    float mr = r.height < 300 ? 26 : 34;
    Vector2 m = {r.x + r.width * 0.84f, r.y + (r.height < 300 ? r.height * 0.4f : 90)};
    for (int g = 8; g >= 1; g--)                       /* soft glow: faint circles, smaller ones on top */
        DrawCircleV(m, mr * (1.0f + 0.19f * g), Alpha((Color){255, 245, 200, 255}, 0.03f * k));
    /* the crescent: the moon's disc minus a slightly smaller disc shifted up and right,
       filled in one thin horizontal line at a time */
    {
        Color mc = Alpha((Color){250, 240, 205, 255}, 0.95f * k);
        float ox = mr * 0.45f, oy = -mr * 0.25f, rb = mr * 0.9f;
        for (float y = -mr; y < mr; y += 1) {
            float yc = y + 0.5f, sa = sqrtf(mr * mr - yc * yc);
            float dy = yc - oy;
            if (fabsf(dy) >= rb) { DrawRectangleRec((Rectangle){m.x - sa, m.y + y, sa * 2, 1}, mc); continue; }
            float sb = sqrtf(rb * rb - dy * dy), b0 = ox - sb, b1 = ox + sb;
            if (b0 > -sa) DrawRectangleRec((Rectangle){m.x - sa, m.y + y, (b0 < sa ? b0 : sa) + sa, 1}, mc);
            if (b1 < sa)  DrawRectangleRec((Rectangle){m.x + (b1 > -sa ? b1 : -sa), m.y + y, sa - (b1 > -sa ? b1 : -sa), 1}, mc);
        }
    }
    /* a shooting star every 7 seconds */
    int cyc = (int)(t / 7);
    float ph = fmodf(t, 7);
    if (ph < 0.9f) {
        Vector2 s0 = {r.x + (0.3f + Rnd(cyc, 1) * 0.6f) * r.width, r.y + Rnd(cyc, 2) * r.height * 0.5f};
        Vector2 dir = {-0.9f, 0.43f};
        Vector2 head = {s0.x + dir.x * ph * 520, s0.y + dir.y * ph * 520};
        Vector2 tail = {head.x - dir.x * 90, head.y - dir.y * 90};
        DrawLineEx(tail, head, 2, Alpha(WHITE, (1 - ph / 0.9f) * k));
    }
}

static void ThemeCircuit(Rectangle r, float t, float k)
{
    Color line = {45, 212, 191, 255};
    DrawRectangleRec(r, Alpha((Color){0, 40, 40, 255}, 0.3f * k));
    int n = ItemCount(r, 11000, 120);
    for (int i = 0; i < n; i++) {
        /* each trace: across, then diagonally, then across again, like on a real board */
        float gx = r.x + floorf(Rnd(i, 1) * r.width / 24) * 24, gy = r.y + floorf(Rnd(i, 2) * r.height / 24) * 24;
        float dx = Rnd(i, 3) < 0.5f ? 1 : -1, dy = Rnd(i, 4) < 0.5f ? 1 : -1;
        float l1 = 24 + floorf(Rnd(i, 5) * 5) * 24, l2 = 12 + floorf(Rnd(i, 6) * 3) * 12, l3 = 24 + floorf(Rnd(i, 7) * 4) * 24;
        Vector2 p0 = {gx, gy}, p1 = {gx + dx * l1, gy}, p2 = {p1.x + dx * l2, p1.y + dy * l2}, p3 = {p2.x + dx * l3, p2.y};
        DrawLineEx(p0, p1, 2, Alpha(line, 0.35f * k));
        DrawLineEx(p1, p2, 2, Alpha(line, 0.35f * k));
        DrawLineEx(p2, p3, 2, Alpha(line, 0.35f * k));
        DrawCircleV(p0, 4, Alpha(line, 0.45f * k));
        DrawCircleV(p0, 2, Alpha((Color){13, 17, 23, 255}, k));
        DrawCircleV(p3, 4, Alpha(line, 0.45f * k));
        /* a pulse of light running along the trace */
        float d12 = l2 * 1.41421f, total = l1 + d12 + l3;
        float s = fmodf(t * (60 + Rnd(i, 8) * 60) + Rnd(i, 9) * 600, total + 260);
        if (s < total) {
            Vector2 q;
            if (s < l1)            q = (Vector2){p0.x + dx * s, p0.y};
            else if (s < l1 + d12) { float u = (s - l1) / 1.41421f; q = (Vector2){p1.x + dx * u, p1.y + dy * u}; }
            else                   q = (Vector2){p2.x + dx * (s - l1 - d12), p2.y};
            DrawCircleV(q, 6, Alpha(line, 0.25f * k));
            DrawCircleV(q, 2.5f, Alpha((Color){200, 255, 245, 255}, 0.9f * k));
        }
    }
    int chips = n / 12 + 1;
    for (int i = 0; i < chips; i++) {
        Rectangle c = {r.x + Rnd(i, 21) * (r.width - 60), r.y + Rnd(i, 22) * (r.height - 40), 44 + Rnd(i, 23) * 30, 26 + Rnd(i, 24) * 14};
        DrawRectangleRec(c, Alpha((Color){10, 30, 32, 255}, 0.9f * k));
        DrawRectangleLinesEx(c, 1.5f, Alpha(line, 0.5f * k));
        for (float px = c.x + 6; px < c.x + c.width - 4; px += 8) {
            DrawLineEx((Vector2){px, c.y}, (Vector2){px, c.y - 5}, 2, Alpha(line, 0.4f * k));
            DrawLineEx((Vector2){px, c.y + c.height}, (Vector2){px, c.y + c.height + 5}, 2, Alpha(line, 0.4f * k));
        }
    }
}

static void ThemeOcean(Rectangle r, float t, float k)
{
    GradientV((int)r.x, (int)r.y, (int)r.width, (int)r.height,
                           Alpha((Color){10, 40, 80, 255}, 0.45f * k), Alpha((Color){5, 25, 60, 255}, 0.6f * k));
    static const Color waves[4] = {{90, 180, 235, 255}, {60, 140, 215, 255}, {35, 100, 185, 255}, {20, 65, 150, 255}};
    float bottom = r.y + r.height;
    for (int l = 0; l < 4; l++) {
        float base = r.y + r.height * (0.5f + l * 0.12f), amp = 7 + l * 3, wl = 190 - l * 25, sp = 0.6f + l * 0.25f;
        for (float x = r.x; x < r.x + r.width; x += 4) {
            float y = base + sinf((x / wl) * 6.28f + t * sp + l * 1.7f) * amp + sinf((x / (wl * 0.37f)) + t * sp * 1.6f) * amp * 0.3f;
            DrawRectangle((int)x, (int)y, 4, (int)(bottom - y) + 1, Alpha(waves[l], 0.32f * k));
        }
    }
    for (int i = 0; i < 40; i++) {
        float x = r.x + Rnd(i, 1) * r.width + sinf(t * 1.3f + i) * 6;
        float y = bottom - fmodf(t * (18 + Rnd(i, 2) * 30) + Rnd(i, 3) * r.height, r.height * 0.6f);
        DrawCircleLines((int)x, (int)y, 2 + Rnd(i, 4) * 4, Alpha((Color){190, 230, 255, 255}, 0.45f * k));
    }
}

static void ThemeAutumn(Rectangle r, float t, float k)
{
    static const Color pal[4] = {{230, 126, 34, 255}, {192, 57, 43, 255}, {241, 196, 15, 255}, {160, 82, 45, 255}};
    GradientV((int)r.x, (int)r.y, (int)r.width, (int)r.height,
                           Alpha((Color){60, 30, 10, 255}, 0.25f * k), Alpha((Color){70, 35, 15, 255}, 0.45f * k));
    int n = ItemCount(r, 13000, 150);
    for (int i = 0; i < n; i++) {
        float fall = 22 + Rnd(i, 1) * 35;
        float y = r.y + fmodf(Rnd(i, 2) * (r.height + 40) + t * fall, r.height + 40) - 20;
        float x = r.x + Rnd(i, 3) * r.width + sinf(t * (0.5f + Rnd(i, 4)) + Rnd(i, 5) * 6.28f) * 30;
        float s = 7 + Rnd(i, 6) * 8, a = t * (Rnd(i, 7) * 2 - 1) * 1.4f + Rnd(i, 8) * 6.28f;
        Vector2 d = {cosf(a), sinf(a)}, pp = {-d.y, d.x}, c = {x, y};
        Vector2 tip  = {c.x + d.x * s,  c.y + d.y * s},  back = {c.x - d.x * s,  c.y - d.y * s};
        Vector2 left = {c.x + pp.x * s * 0.5f, c.y + pp.y * s * 0.5f}, right = {c.x - pp.x * s * 0.5f, c.y - pp.y * s * 0.5f};
        Color col = Alpha(pal[(int)(Rnd(i, 9) * 4) % 4], 0.7f * k);
        Tri(tip, left, back, col);
        Tri(tip, back, right, col);
        DrawLineEx((Vector2){back.x - d.x * 4, back.y - d.y * 4}, tip, 1, Alpha((Color){90, 40, 10, 255}, 0.6f * k));
    }
}

/* Draws a theme filling rectangle r, but only the part inside clip.
   strength 0..1: low for behind the app, 1 for banners and previews. */
static void DrawThemeClip(int theme, Rectangle r, Rectangle clip, float strength)
{
    float t = (float)GetTime();
    if (clip.width <= 0 || clip.height <= 0) return;
    BeginScissorMode((int)clip.x, (int)clip.y, (int)clip.width, (int)clip.height);
    switch (theme) {
    case THEME_CODE:    ThemeCode(r, t, strength);    break;
    case THEME_GARDEN:  ThemeGarden(r, t, strength);  break;
    case THEME_NIGHT:   ThemeNight(r, t, strength);   break;
    case THEME_CIRCUIT: ThemeCircuit(r, t, strength); break;
    case THEME_OCEAN:   ThemeOcean(r, t, strength);   break;
    case THEME_AUTUMN:  ThemeAutumn(r, t, strength);  break;
    default:            /* Classic: a quiet banner, nothing behind the app */
        if (strength >= 0.99f) {
            GradientH((int)r.x, (int)r.y, (int)r.width, (int)r.height, (Color){22, 27, 34, 255}, (Color){33, 38, 45, 255});
            for (float x = r.x + 12; x < r.x + r.width; x += 24)
                for (float y = r.y + 12; y < r.y + r.height; y += 24) DrawCircleV((Vector2){x, y}, 1.2f, (Color){48, 54, 61, 255});
        }
        break;
    }
    EndScissorMode();
}

static void DrawTheme(int theme, Rectangle r, float strength) { DrawThemeClip(theme, r, r, strength); }

/* ------------------------------------------------------------------ */
/* Saving and loading                                                 */
/* Format: numbers on their own line; text as  <length>:<bytes>\n      */
/* ------------------------------------------------------------------ */
static void WSN(FILE *f, const char *s, int n) { fprintf(f, "%d:", n); fwrite(s, 1, (size_t)n, f); fputc('\n', f); }
static void WS(FILE *f, const char *s) { WSN(f, s, (int)strlen(s)); }
static void WI(FILE *f, long long v)   { fprintf(f, "%lld\n", v); }

static int  loadErr;
static bool loadFailed;                /* the save file couldn't be read: nothing is saved until you decide */
static int  loadLine;                  /* which check failed (for the message) */
static char loadWhy[TEXT_LEN];         /* what went wrong, in words            */
static char loadWhere[TEXT_LEN];       /* e.g. "the repository jo/hello-world"  */
#define LOADFAIL (loadErr = 1, loadLine = loadLine ? loadLine : __LINE__)
/* Reads one piece of text (or binary data); *outLen gets its length. */
static char *RSN(FILE *f, int *outLen)
{
    int n;
    /* anything up to 1 GB: as big as anything Mini GitHub can store */
    if (fscanf(f, " %d:", &n) != 1 || n < 0 || n > 1073741824) {
        if (!loadErr) snprintf(loadWhy, sizeof loadWhy, "a piece of text or a file in it couldn't be read");
        LOADFAIL; return NULL;
    }
    char *s = malloc((size_t)n + 1);
    if (!s) { if (!loadErr) snprintf(loadWhy, sizeof loadWhy, "not enough memory to load a %d MB file", n >> 20); LOADFAIL; return NULL; }
    if (fread(s, 1, (size_t)n, f) != (size_t)n) { free(s); if (!loadErr) snprintf(loadWhy, sizeof loadWhy, "the file ends too early (cut short?)"); LOADFAIL; return NULL; }
    s[n] = 0;
    if (outLen) *outLen = n;
    fgetc(f);
    return s;
}
static char *RS(FILE *f) { return RSN(f, NULL); }
static void RSInto(FILE *f, char *dst, int cap)
{
    char *s = RS(f);
    snprintf(dst, (size_t)cap, "%s", s ? s : "");
    free(s);
}
static long long RI(FILE *f)
{
    long long v = 0;
    if (fscanf(f, " %lld", &v) != 1) { if (!loadErr) snprintf(loadWhy, sizeof loadWhy, "a number in it couldn't be read"); LOADFAIL; }
    return v;
}

/* Which number each path has in the save file (paths are written once per repository). */
typedef struct { const char **key; int *val; int cap, n; } PtrMap;
static uint32_t PtrHash(const void *p) { uintptr_t v = (uintptr_t)p; v ^= v >> 17; v *= 0x9E3779B1u; return (uint32_t)(v ^ (v >> 15)); }
static int PtrGet(PtrMap *m, const char *k)
{
    if (!m->cap) return -1;
    for (uint32_t i = PtrHash(k) & (uint32_t)(m->cap - 1);; i = (i + 1) & (uint32_t)(m->cap - 1)) {
        if (!m->key[i]) return -1;
        if (m->key[i] == k) return m->val[i];
    }
}
static void PtrPut(PtrMap *m, const char *k, int v)
{
    if ((m->n + 1) * 2 > m->cap) {
        PtrMap n = {0};
        n.cap = m->cap ? m->cap * 2 : 256;
        n.key = calloc((size_t)n.cap, sizeof *n.key);
        n.val = calloc((size_t)n.cap, sizeof *n.val);
        if (!n.key || !n.val) { free(n.key); free(n.val); return; }
        for (int i = 0; i < m->cap; i++) if (m->key[i]) PtrPut(&n, m->key[i], m->val[i]);
        free(m->key); free(m->val);
        *m = n;
    }
    uint32_t i = PtrHash(k) & (uint32_t)(m->cap - 1);
    while (m->key[i] && m->key[i] != k) i = (i + 1) & (uint32_t)(m->cap - 1);
    if (!m->key[i]) m->n++;
    m->key[i] = k; m->val[i] = v;
}
static void PtrFree(PtrMap *m) { free(m->key); free(m->val); memset(m, 0, sizeof *m); }

/* Earlier versions could store a pushed commit before a commit it was built on
   (with merges that criss-cross), recording the link wrongly. Every commit's
   real parents are in its git data, so they're read again from there, and the
   list is put back in order: each commit after the ones it's built on.
   Returns false only if the history can't be put in order at all. */
static bool RepairHistory(Repo *r)
{
    int n = r->commitCount;
    if (!n) return true;
    for (int k = 0; k < n; k++) {
        Commit *c = &r->commits[k];
        CommitInfo ci;
        if (!ParseCommit(c->raw, c->rawLen, &ci)) return false;
        c->parentCount = 0;
        for (int q = 0; q < ci.np && c->parentCount < 2; q++) {
            int p = CommitBySha(r, ci.parents[q]);
            if (p >= 0 && p != k) c->parents[c->parentCount++] = p;   /* a missing one is left out */
        }
        c->statsDone = false;
    }
    /* put in order: repeatedly take a commit whose parents are all placed */
    int *left = calloc((size_t)n, sizeof(int)), *kidStart = calloc((size_t)n + 1, sizeof(int)), *kids = malloc((size_t)n * 2 * sizeof(int) + 1);
    int *order = malloc((size_t)n * sizeof(int)), *to = malloc((size_t)n * sizeof(int)), *queue = malloc((size_t)n * sizeof(int));
    Commit *moved = malloc((size_t)n * sizeof(Commit));
    bool ok = left && kidStart && kids && order && to && queue && moved;
    if (ok) {
        for (int k = 0; k < n; k++) for (int q = 0; q < r->commits[k].parentCount; q++) kidStart[r->commits[k].parents[q] + 1]++;
        for (int k = 0; k < n; k++) kidStart[k + 1] += kidStart[k];
        int *fill = calloc((size_t)n, sizeof(int));
        if (!fill) ok = false;
        for (int k = 0; ok && k < n; k++)
            for (int q = 0; q < r->commits[k].parentCount; q++) { int p = r->commits[k].parents[q]; kids[kidStart[p] + fill[p]++] = k; }
        free(fill);
        int qh = 0, qt = 0, on = 0;
        for (int k = 0; ok && k < n; k++) { left[k] = r->commits[k].parentCount; if (!left[k]) queue[qt++] = k; }
        while (ok && qh < qt) {
            int k = queue[qh++];
            order[on++] = k;
            for (int j = kidStart[k]; j < kidStart[k + 1]; j++) if (--left[kids[j]] == 0) queue[qt++] = kids[j];
        }
        if (on != n) ok = false;                             /* a loop: can't happen with real commits */
        if (ok) {
            for (int i = 0; i < n; i++) to[order[i]] = i;
            for (int i = 0; i < n; i++) moved[i] = r->commits[order[i]];
            for (int i = 0; i < n; i++) {
                for (int q = 0; q < moved[i].parentCount; q++) moved[i].parents[q] = to[moved[i].parents[q]];
                r->commits[i] = moved[i];
            }
            for (int b = 0; b < r->branchCount; b++) r->branches[b].tip = to[r->branches[b].tip];
            MapFree(&r->commitMap);
            for (int i = 0; i < n; i++) MapPut(&r->commitMap, r->commits[i].sha, i);
            r->gen++;
        }
    }
    free(left); free(kidStart); free(kids); free(order); free(to); free(queue); free(moved);
    return ok;
}

static void Save(void)
{
    if (loadFailed) return;                /* never write over a save file that couldn't be read */
    FILE *f = fopen(SAVE_FILE ".tmp", "wb");
    if (!f) { Toast("Could not save - is the folder read-only?"); return; }
    fprintf(f, "MINIGITHUB8\n");
    WI(f, nextUserId);
    WS(f, lastUser);
    WI(f, userCount);
    for (int i = 0; i < userCount; i++) {
        User *u = &users[i];
        WI(f, u->id); WS(f, u->name); WS(f, u->salt); WS(f, u->hash); WI(f, u->created);
        WS(f, u->bio); WI(f, u->theme); WI(f, u->avatarColor); WS(f, u->avatar ? u->avatar : "");
        WS(f, u->rsalt); WS(f, u->rhash);
        WS(f, u->email);
    }
    WI(f, repoCount);
    for (int i = 0; i < repoCount; i++) {
        Repo *r = &repos[i];
        WI(f, r->owner); WS(f, r->name); WS(f, r->desc); WI(f, r->created); WS(f, r->branch);
        WI(f, r->starCount);
        for (int k = 0; k < r->starCount; k++) WI(f, r->starredBy[k]);
        WI(f, r->collabCount);
        for (int k = 0; k < r->collabCount; k++) WI(f, r->collabs[k]);
        WI(f, r->inviteCount);
        for (int k = 0; k < r->inviteCount; k++) { WI(f, r->invites[k]); WI(f, r->inviteTime[k]); }
        WI(f, r->blobCount);
        for (int k = 0; k < r->blobCount; k++) WSN(f, r->blobs[k].data, r->blobs[k].len);

        /* every path used, once, then each commit refers to them by number */
        PtrMap pm = {0};
        int pc = 0;
        for (int k = 0; k < r->commitCount; k++)
            for (int j = 0; j < r->commits[k].fileCount; j++)
                if (PtrGet(&pm, r->commits[k].names[j]) < 0) PtrPut(&pm, r->commits[k].names[j], pc++);
        WI(f, pc);
        const char **order = calloc((size_t)(pc ? pc : 1), sizeof *order);
        for (int s = 0; order && s < pm.cap; s++) if (pm.key[s]) order[pm.val[s]] = pm.key[s];
        for (int k = 0; k < pc; k++) WS(f, order ? order[k] : "");
        free(order);

        WI(f, r->commitCount);
        for (int k = 0; k < r->commitCount; k++) {
            Commit *c = &r->commits[k];
            WSN(f, c->raw, c->rawLen);
            /* the line counts are kept too, so they needn't be worked out again (-1 = not yet) */
            fprintf(f, "%lld %d %d %d %d %d %d %d %d %d\n", c->time, c->author, (int)c->copied, c->parentCount,
                    c->parents[0], c->parents[1], c->statsDone ? c->sFiles : -1, c->sAdds, c->sDels, c->fileCount);
            for (int j = 0; j < c->fileCount; j++) fprintf(f, "%d %d %d\n", PtrGet(&pm, c->names[j]), c->blobs[j], c->modes[j]);
        }
        PtrFree(&pm);
        WI(f, r->branchCount); WI(f, r->defBranch);
        for (int b = 0; b < r->branchCount; b++) { WS(f, r->branches[b].name); WI(f, r->branches[b].tip); }

        WI(f, r->issueCount);
        for (int k = 0; k < r->issueCount; k++) {
            Issue *is = &r->issues[k];
            WS(f, is->title); WI(f, is->open); WI(f, is->time); WI(f, is->author);
        }
        int drafts = 0;                                   /* skip empty drafts */
        for (int k = 0; k < r->draftCount; k++) if (r->drafts[k].count) drafts++;
        WI(f, drafts);
        for (int k = 0; k < r->draftCount; k++) {
            Draft *d = &r->drafts[k];
            if (!d->count) continue;
            WI(f, d->user); WI(f, d->count);
            char mh[41];
            ShaHex(d->mergeSha, mh);
            WS(f, d->branch); WI(f, d->merging); WS(f, mh); WS(f, d->mergeName);
            for (int j = 0; j < d->count; j++) {
                WorkFile *e = &d->files[j];
                WS(f, e->name); WSN(f, e->text, e->len); WI(f, e->deleted); WI(f, e->base);
            }
        }
        WI(f, r->isFork); WI(f, r->forkOwner); WS(f, r->forkName); WS(f, r->forkBase);
    }
    bool ok = !ferror(f);
    ok = fclose(f) == 0 && ok;
    if (!ok) { remove(SAVE_FILE ".tmp"); Toast("Could not save - is the disk full?"); return; }
    remove(SAVE_FILE);
    rename(SAVE_FILE ".tmp", SAVE_FILE);
}

/* "MINIGITHUB3" was used by two different versions of this program: one added
   profiles, the other added recovery codes. Both put their new fields right after
   each account's creation time: profiles start with the bio (text, then a number),
   recovery codes with two pieces of text. So peek at the second item to tell. */
static bool V3HasProfiles(FILE *f)
{
    long at = ftell(f);
    int saved = loadErr;
    char *first = RS(f);
    free(first);
    int n = 0, after = 0;
    bool isText = fscanf(f, " %d", &n) == 1 && (after = fgetc(f)) == ':';
    fseek(f, at, SEEK_SET);
    loadErr = saved;
    return !isText;
}

/* A blob read from the save file goes exactly where it was (commits refer to it by number). */
static bool LoadBlob(Repo *r, char *d, int len)
{
    r->blobs = GrowArr(r->blobs, &r->blobCap, r->blobCount + 1, sizeof(Blob));
    if (r->blobCount >= r->blobCap) { free(d); return false; }
    Blob *b = &r->blobs[r->blobCount];
    b->data = d; b->len = len; b->binary = LooksBinary(d, len); b->lang = NULL;
    ObjectSha("blob", d, (size_t)len, b->sha);
    if (MapGet(&r->blobMap, b->sha) < 0) MapPut(&r->blobMap, b->sha, r->blobCount);
    r->blobCount++;
    return true;
}

typedef struct { const char *name; int blob; } OldEntry;
static int CmpOldEntry(const void *a, const void *b) { return strcmp(((const OldEntry *)a)->name, ((const OldEntry *)b)->name); }

/* Commits from before version 7 weren't git commits yet. Each is turned into
   one now - the same files, author, time and message, on top of the commit
   before it - so it gets a real git fingerprint. */
static bool UpgradeCommits(Repo *r)
{
    for (int k = 0; k < r->commitCount; k++) {
        Commit *c = &r->commits[k];
        uint8_t tree[20];
        const uint8_t *par[1];
        c->parentCount = k > 0;
        c->parents[0] = k - 1;
        if (k > 0) par[0] = r->commits[k - 1].sha;
        if (!CommitTree(r, c, tree, NULL, NULL)) return false;
        c->raw = MakeRawCommit(tree, par, k > 0, UserName(c->author), UserEmail(c->author), c->time, c->message, &c->rawLen);
        if (!c->raw) return false;
        ObjectSha("commit", c->raw, (size_t)c->rawLen, c->sha);
        char hx[41];
        ShaHex(c->sha, hx);
        snprintf(c->id, sizeof c->id, "%.7s", hx);
        MapPut(&r->commitMap, c->sha, k);
    }
    r->gen++;
    return true;
}

/* Reads every version: 7 (git commits, folders), 6 (forks), 5 (collaborators + drafts),
   4 (profiles + recovery codes), 3 (either one), 2 (accounts), 1 (before accounts). */
static bool Load(void)
{
    /* the program stopped in the middle of saving: the new copy was written, the old one already removed */
    if (!FileExists(SAVE_FILE) && FileExists(SAVE_FILE ".tmp")) rename(SAVE_FILE ".tmp", SAVE_FILE);
    FILE *f = fopen(SAVE_FILE, "rb");
    if (!f) return false;
    loadErr = 0; loadLine = 0; loadWhy[0] = 0; loadWhere[0] = 0;
    char magic[32] = {0};
    int ver = 0;
    if (fscanf(f, "%31s", magic) == 1 && strncmp(magic, "MINIGITHUB", 10) == 0 && magic[10] >= '1' && magic[10] <= '8' && !magic[11])
        ver = magic[10] - '0';
    if (!ver) {
        if (strncmp(magic, "MINIGITHUB", 10) == 0) snprintf(loadWhy, sizeof loadWhy, "it was made by a newer version of Mini GitHub");
        else snprintf(loadWhy, sizeof loadWhy, "it isn't a Mini GitHub save file");
        LOADFAIL;
    }

    if (ver >= 2) {
        nextUserId = (int)RI(f);
        RSInto(f, lastUser, NAME_LEN);
        int uc = (int)RI(f);
        if (uc < 0 || uc > MAX_USERS) LOADFAIL;
        for (int i = 0; i < uc && !loadErr; i++) {
            User *u = &users[userCount++];
            memset(u, 0, sizeof *u);
            u->id = (int)RI(f);
            RSInto(f, u->name, NAME_LEN);
            RSInto(f, u->salt, (int)sizeof u->salt);
            RSInto(f, u->hash, (int)sizeof u->hash);
            u->created = RI(f);
            bool profiles = ver >= 4 || (ver == 3 && V3HasProfiles(f));
            bool recovery = ver >= 4 || (ver == 3 && !profiles);
            if (profiles) {
                RSInto(f, u->bio, TEXT_LEN);
                u->theme       = (int)RI(f);
                u->avatarColor = (int)RI(f);
                u->avatar      = RS(f);
                if (u->avatar && !u->avatar[0]) { free(u->avatar); u->avatar = NULL; }
                if (u->theme < 0 || u->theme >= THEME_COUNT) u->theme = 0;
                if (u->avatarColor < 0 || u->avatarColor > 8) u->avatarColor = 0;
            }
            if (recovery) {
                RSInto(f, u->rsalt, (int)sizeof u->rsalt);
                RSInto(f, u->rhash, (int)sizeof u->rhash);
                bool none = !u->rsalt[0] && !u->rhash[0];
                if (!none && (strlen(u->rsalt) != 32 || strlen(u->rhash) != 64)) LOADFAIL;
            }
            if (ver >= 7) RSInto(f, u->email, TEXT_LEN);
            if (u->id <= 0 || strlen(u->salt) != 32 || strlen(u->hash) != 64) LOADFAIL;
            if (u->id >= nextUserId) nextUserId = u->id + 1;
        }
    }

    int n = loadErr ? 0 : (int)RI(f);
    if (n < 0 || n > MAX_REPOS) LOADFAIL;

    static OldEntry oldEntries[64];
    int repaired = 0;                                        /* histories put back in order */
    for (int i = 0; i < n && !loadErr; i++) {
        bool needRepair = false;
        Repo *r = &repos[repoCount++];
        memset(r, 0, sizeof *r);
        snprintf(r->branch, NAME_LEN, "main");
        r->owner = ver >= 2 ? (int)RI(f) : -1;
        RSInto(f, r->name, NAME_LEN);
        snprintf(loadWhere, sizeof loadWhere, "the repository %s", r->name);
        RSInto(f, r->desc, TEXT_LEN);
        if (ver == 1) RI(f);                    /* old single-user "starred" flag: dropped */
        r->created = RI(f);
        if (ver >= 7) { RSInto(f, r->branch, NAME_LEN); if (!r->branch[0]) snprintf(r->branch, NAME_LEN, "main"); }
        if (ver >= 2) {
            int sc = (int)RI(f);
            if (sc < 0 || sc > MAX_USERS) { LOADFAIL; break; }
            for (int k = 0; k < sc; k++) r->starredBy[r->starCount++] = (int)RI(f);
        }

        /* before version 5 there was one working copy per repository (the owner's);
           it's read here and turned into the owner's draft once the commits are in */
        static WorkFile legacy[64];
        int legacyCount = 0;
        if (ver >= 5) {
            int cc2 = (int)RI(f);
            if (cc2 < 0 || cc2 > MAX_USERS) { LOADFAIL; break; }
            for (int k = 0; k < cc2; k++) r->collabs[r->collabCount++] = (int)RI(f);
            int ic2 = (int)RI(f);
            if (ic2 < 0 || ic2 > MAX_USERS) { LOADFAIL; break; }
            for (int k = 0; k < ic2; k++) { r->invites[r->inviteCount] = (int)RI(f); r->inviteTime[r->inviteCount++] = RI(f); }
        } else {
            int fc = (int)RI(f);
            if (fc < 0 || fc > 64) { LOADFAIL; break; }
            for (int k = 0; k < fc && !loadErr; k++) {
                WorkFile *w = &legacy[legacyCount];
                memset(w, 0, sizeof *w);
                RSInto(f, w->name, PATH_LEN);
                int txLen = 0;
                char *tx = RSN(f, &txLen);
                if (tx) { WF_SetN(w, tx, txLen); free(tx); legacyCount++; }
            }
        }

        int bc = (int)RI(f);
        if (bc < 0 || bc > MAX_BLOBS) { LOADFAIL; break; }
        for (int k = 0; k < bc && !loadErr; k++) {
            int dLen = 0;
            char *d = RSN(f, &dLen);
            if (!d || !LoadBlob(r, d, dLen)) { LOADFAIL; break; }
        }
        if (loadErr) break;

        int cc = (int)RI(f);
        if (ver >= 7) {
            int pc = cc;                                   /* first the table of paths */
            if (pc < 0 || pc > 10000000) { LOADFAIL; break; }
            const char **paths = malloc((size_t)(pc ? pc : 1) * sizeof *paths);
            if (!paths) { LOADFAIL; break; }
            for (int k = 0; k < pc && !loadErr; k++) {
                char *s = RS(f);
                paths[k] = s ? Intern(s) : NULL;
                if (!paths[k]) LOADFAIL;
                free(s);
            }
            cc = loadErr ? 0 : (int)RI(f);
            if (cc < 0 || cc > MAX_COMMITS) LOADFAIL;
            for (int k = 0; k < cc && !loadErr; k++) {
                Commit c;
                memset(&c, 0, sizeof c);
                c.raw = RSN(f, &c.rawLen);
                c.time = RI(f); c.author = (int)RI(f); c.copied = RI(f) != 0;
                c.parentCount = (int)RI(f); c.parents[0] = (int)RI(f); c.parents[1] = (int)RI(f);
                c.sFiles = (int)RI(f); c.sAdds = (int)RI(f); c.sDels = (int)RI(f);
                bool stats = c.sFiles >= 0 && c.sAdds >= 0 && c.sDels >= 0;
                int ff = (int)RI(f);
                if (!c.raw || ff < 0 || ff > MAX_FILES || c.parentCount < 0 || c.parentCount > 2) { free(c.raw); LOADFAIL; break; }
                for (int q = 0; q < c.parentCount; q++)
                    if (c.parents[q] < 0 || c.parents[q] >= k) { needRepair = true; c.parents[q] = 0; }   /* fixed below */
                c.fileCount = ff;
                c.names = malloc((size_t)(ff ? ff : 1) * sizeof *c.names);
                c.blobs = malloc((size_t)(ff ? ff : 1) * sizeof *c.blobs);
                c.modes = malloc((size_t)(ff ? ff : 1));
                if (!c.names || !c.blobs || !c.modes) { FreeCommit(&c); LOADFAIL; break; }
                for (int j = 0; j < ff && !loadErr; j++) {
                    int pi = (int)RI(f), b = (int)RI(f), m = (int)RI(f);
                    if (pi < 0 || pi >= pc || b < 0 || b >= r->blobCount || m < 0 || m > 2) { LOADFAIL; break; }
                    c.names[j] = paths[pi]; c.blobs[j] = b; c.modes[j] = (uint8_t)m;
                }
                CommitInfo info;
                if (!loadErr && !ParseCommit(c.raw, c.rawLen, &info)) LOADFAIL;
                if (loadErr) { FreeCommit(&c); break; }
                snprintf(c.message, TEXT_LEN, "%s", info.summary);
                if (!AppendCommit(r, &c)) { FreeCommit(&c); LOADFAIL; break; }
                r->commits[r->commitCount - 1].statsDone = stats;
            }
            free(paths);
            if (loadErr) break;
        } else {
            if (cc < 0 || cc > MAX_COMMITS) { LOADFAIL; break; }
            r->commits = GrowArr(r->commits, &r->commitCap, cc + 1, sizeof(Commit));
            if (r->commitCap < cc) { LOADFAIL; break; }
            for (int k = 0; k < cc && !loadErr; k++) {
                Commit *c = &r->commits[k];
                memset(c, 0, sizeof *c);
                RSInto(f, c->id, (int)sizeof c->id);       /* the old short id: only used to match up forks below */
                RSInto(f, c->message, TEXT_LEN);
                c->time   = RI(f);
                c->author = ver >= 2 ? (int)RI(f) : -1;
                int ff = (int)RI(f);
                if (ff < 0 || ff > 64) { LOADFAIL; break; }
                c->fileCount = ff;
                for (int j = 0; j < ff; j++) {
                    char nm[PATH_LEN];
                    RSInto(f, nm, PATH_LEN);
                    int b = (int)RI(f);
                    if (b < 0 || b >= r->blobCount || !nm[0]) { LOADFAIL; break; }
                    oldEntries[j].name = Intern(nm);
                    oldEntries[j].blob = b;
                    if (!oldEntries[j].name) LOADFAIL;
                }
                if (ver >= 6) c->copied = RI(f) != 0;
                if (loadErr) break;
                qsort(oldEntries, (size_t)ff, sizeof oldEntries[0], CmpOldEntry);
                c->names = malloc((size_t)(ff ? ff : 1) * sizeof *c->names);
                c->blobs = malloc((size_t)(ff ? ff : 1) * sizeof *c->blobs);
                c->modes = calloc((size_t)(ff ? ff : 1), 1);
                r->commitCount++;
                if (!c->names || !c->blobs || !c->modes) { LOADFAIL; break; }
                for (int j = 0; j < ff; j++) { c->names[j] = oldEntries[j].name; c->blobs[j] = oldEntries[j].blob; }
            }
            if (loadErr) break;
        }

        /* branches (before version 8 there was just one, at the newest commit) */
        if (ver >= 8) {
            int bc2 = (int)RI(f), db = (int)RI(f);
            if (bc2 < 0 || bc2 > MAX_BRANCHES || (bc2 && (db < 0 || db >= bc2))) { LOADFAIL; break; }
            for (int b = 0; b < bc2 && !loadErr; b++) {
                char bn[NAME_LEN];
                RSInto(f, bn, NAME_LEN);
                int tip = (int)RI(f);
                if (tip < 0 || tip >= r->commitCount || !bn[0]) LOADFAIL;
                else AddBranch(r, bn, tip);
            }
            r->defBranch = r->curBranch = bc2 ? db : 0;
            if (loadErr) break;
        } else if (r->commitCount) { AddBranch(r, r->branch, r->commitCount - 1); r->defBranch = r->curBranch = 0; }
        if (needRepair) {                                    /* a history stored out of order: put it right */
            if (!RepairHistory(r)) { snprintf(loadWhy, sizeof loadWhy, "its history couldn't be put back in order"); LOADFAIL; break; }
            repaired++;
            needRepair = false;
        }

        int ic = (int)RI(f);
        if (ic < 0 || ic > MAX_ISSUES) { LOADFAIL; break; }
        for (int k = 0; k < ic && !loadErr; k++) {
            Issue *is = &r->issues[r->issueCount++];
            RSInto(f, is->title, TEXT_LEN);
            is->open   = RI(f) != 0;
            is->time   = RI(f);
            is->author = ver >= 2 ? (int)RI(f) : -1;
        }

        if (ver >= 5) {
            int dc = (int)RI(f);
            if (dc < 0 || dc > MAX_DRAFTS) { LOADFAIL; break; }
            for (int k = 0; k < dc && !loadErr; k++) {
                int uid = (int)RI(f), n2 = (int)RI(f);
                if (n2 < 0 || n2 > MAX_DRAFT) { LOADFAIL; break; }
                char dbn[NAME_LEN] = "", mh[48] = "", mn[NAME_LEN] = "";
                bool merging = false;
                if (ver >= 8) { RSInto(f, dbn, NAME_LEN); merging = RI(f) != 0; RSInto(f, mh, sizeof mh); RSInto(f, mn, NAME_LEN); }
                int keep = r->curBranch, bi = dbn[0] ? FindBranch(r, dbn) : -1;
                if (bi >= 0) r->curBranch = bi;            /* GetDraft makes it for the branch you're on */
                Draft *d = (bi >= 0 || !dbn[0]) ? GetDraft(r, uid, true) : NULL;   /* a draft for a branch that's gone is dropped */
                r->curBranch = keep;
                if (d && merging && ParseHex40(mh, d->mergeSha)) { d->merging = true; snprintf(d->mergeName, NAME_LEN, "%s", mn); }
                for (int j = 0; j < n2 && !loadErr; j++) {
                    char nm[PATH_LEN];
                    RSInto(f, nm, PATH_LEN);
                    int txLen = 0;
                    char *tx = RSN(f, &txLen);
                    bool del = RI(f) != 0;
                    int base = (int)RI(f);
                    if (!tx) break;
                    if (base < -1 || base >= r->blobCount) LOADFAIL;
                    if (d && !loadErr && d->count < MAX_DRAFT) {
                        WorkFile *e = &d->files[d->count++];
                        memset(e, 0, sizeof *e);
                        snprintf(e->name, PATH_LEN, "%s", nm);
                        WF_SetN(e, tx, txLen);
                        e->deleted = del;
                        e->base = base;
                    }
                    free(tx);
                }
            }
        } else {
            /* the old working copy becomes the owner's draft: only the real changes */
            for (int k = 0; k < legacyCount; k++) {
                WorkFile *w = &legacy[k];
                if (!SameAsBlob(r, w, HeadBlobOf(r, w->name))) {
                    WorkFile *e = DraftEdit(r, r->owner, w->name);
                    if (e) WF_SetN(e, w->text, w->len);
                }
            }
            Commit *h = Head(r);
            for (int k = 0; h && k < h->fileCount; k++) {
                bool kept = false;
                for (int j = 0; j < legacyCount; j++) if (strcmp(legacy[j].name, h->names[k]) == 0) kept = true;
                if (!kept) { WorkFile *e = DraftEdit(r, r->owner, h->names[k]); if (e) e->deleted = true; }
            }
            for (int k = 0; k < legacyCount; k++) WF_Free(&legacy[k]);
        }

        if (ver >= 6 && !loadErr) {
            r->isFork    = RI(f) != 0;
            r->forkOwner = (int)RI(f);
            RSInto(f, r->forkName, NAME_LEN);
            RSInto(f, r->forkBase, (int)sizeof r->forkBase);
        }
    }
    fclose(f);

    /* older saves: turn every commit into a git commit. Forks remember where they
       started by the original's old short id, so that's matched up first. */
    if (!loadErr && ver < 7) {
        static int baseAt[MAX_REPOS];
        for (int i = 0; i < repoCount; i++) {
            Repo *r = &repos[i];
            baseAt[i] = -1;
            if (!r->isFork || !r->forkBase[0]) continue;
            for (int j = 0; j < repoCount && baseAt[i] < 0; j++)
                if (repos[j].owner == r->forkOwner && EqualsCI(repos[j].name, r->forkName))
                    for (int k = 0; k < repos[j].commitCount; k++)
                        if (strcmp(repos[j].commits[k].id, r->forkBase) == 0) { baseAt[i] = j * 1000000 + k; break; }
        }
        for (int i = 0; i < repoCount && !loadErr; i++) if (!UpgradeCommits(&repos[i])) LOADFAIL;
        for (int i = 0; i < repoCount && !loadErr; i++) {
            Repo *r = &repos[i];
            if (!r->isFork || !r->forkBase[0]) continue;
            if (baseAt[i] < 0) { snprintf(r->forkBase, sizeof r->forkBase, "%040d", 0); continue; }   /* no longer matches */
            ShaHex(repos[baseAt[i] / 1000000].commits[baseAt[i] % 1000000].sha, r->forkBase);
        }
        if (!loadErr) {
            /* keep the old file, just in case */
            char old[64];
            snprintf(old, sizeof old, "%s.v%d-backup", SAVE_FILE, ver);
            if (!FileExists(old)) {
                int sz = 0;
                unsigned char *d = LoadFileData(SAVE_FILE, &sz);
                if (d) { SaveFileData(old, d, sz); UnloadFileData(d); }
            }
            MarkDirty();
        }
    }

    if (loadErr) {
        /* Don't touch the file: it stays exactly as it is, and nothing is saved
           over it until you choose what to do (see ScreenLoadFail). */
        for (int i = 0; i < repoCount; i++) FreeRepo(&repos[i]);
        for (int i = 0; i < userCount; i++) free(users[i].avatar);
        memset(repos, 0, sizeof repos);
        memset(users, 0, sizeof users);
        repoCount = userCount = 0;
        nextUserId = 1;
        lastUser[0] = 0;
        if (!loadWhy[0]) snprintf(loadWhy, sizeof loadWhy, "part of it didn't make sense (check %d)", loadLine);
        loadFailed = true;
        return false;
    }
    if (repaired) {                                          /* save the mended version (the old file is kept, below) */
        char old[64];
        snprintf(old, sizeof old, "%s.before-repair", SAVE_FILE);
        if (!FileExists(old)) { int sz = 0; unsigned char *d = LoadFileData(SAVE_FILE, &sz); if (d) { SaveFileData(old, d, sz); UnloadFileData(d); } }
        MarkDirty();
        Toast(TextFormat("Mended the history of %d repositor%s stored out of order by an older version", repaired, repaired == 1 ? "y" : "ies"));
        return true;
    }
    /* it loaded: keep a copy of this good file, in case anything goes wrong later */
    if (ver >= 7) {
        int sz = 0;
        unsigned char *d = LoadFileData(SAVE_FILE, &sz);
        if (d) { SaveFileData(SAVE_FILE ".last-good", d, sz); UnloadFileData(d); }
    }
    return true;
}

/* An example repository for new accounts that want one. */
static void SeedExample(int owner)
{
    Repo *r = NewRepo(owner, "hello-world", "An example repository to try things out");
    if (!r) return;
    DraftSetText(r, owner, "README.md",
        "# hello-world\n"
        "\n"
        "Welcome to Mini GitHub!\n"
        "\n"
        "This is an example repository so you can see how things work.\n");
    DraftSetText(r, owner, "main.c",
        "#include <stdio.h>\n"
        "\n"
        "int main(void)\n"
        "{\n"
        "    printf(\"Hello, world!\\n\");\n"
        "    return 0;\n"
        "}\n");
    DoCommit(r, "Initial commit");

    DraftSetText(r, owner, "README.md",
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

/* An eye; with a line through it when "slashed". */
static void DrawEye(float cx, float cy, bool slashed, Color c, Color bg)
{
    DrawEllipse((int)cx, (int)cy, 11, 7, c);
    DrawEllipse((int)cx, (int)cy, 9, 5, bg);
    DrawCircle((int)cx, (int)cy, 3.5f, c);
    if (slashed) {
        DrawLineEx((Vector2){cx - 11, cy + 9}, (Vector2){cx + 11, cy - 9}, 5, bg);
        DrawLineEx((Vector2){cx - 11, cy + 9}, (Vector2){cx + 11, cy - 9}, 2, c);
    }
}

/* One-line text box. Returns true when Enter is pressed in it.
   Password boxes get an eye button on the right to show / hide the text. */
static bool TextFieldUI(TextField *t, Rectangle r, const char *placeholder)
{
    bool enter = false;
    float eyeW = t->secret ? 42.0f : 0.0f;
    Rectangle eye = {r.x + r.width - eyeW, r.y, eyeW, r.height};
    bool eyeHot = t->secret && Hover(eye);
    if (t->secret && Clicked(eye)) t->reveal = !t->reveal;

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) t->active = Hover(r);
    if (eyeHot)       wantCursor = MOUSE_CURSOR_POINTING_HAND;
    else if (Hover(r)) wantCursor = MOUSE_CURSOR_IBEAM;
    if (t->active) {
        int ch;
        while ((ch = GetCharPressed()) > 0)
            if (ch >= 32 && ch < 127 && t->len < TEXT_LEN - 1) { t->buf[t->len++] = (char)ch; t->buf[t->len] = 0; }
        if (KeyRep(KEY_BACKSPACE) && t->len > 0) t->buf[--t->len] = 0;
        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) enter = true;
        bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL) ||
                    IsKeyDown(KEY_LEFT_SUPER) || IsKeyDown(KEY_RIGHT_SUPER);
        if (ctrl && IsKeyPressed(KEY_V)) {                   /* Ctrl+V pastes */
            const char *clip = GetClipboardText();
            for (; clip && *clip && t->len < TEXT_LEN - 1; clip++)
                if (*clip >= 32 && *clip < 127) { t->buf[t->len++] = *clip; t->buf[t->len] = 0; }
        }
    }
    DrawRectangleRec(r, UI_BG);
    DrawRectangleLinesEx(r, t->active ? 2.0f : 1.0f, t->active ? UI_LINK : UI_BORDER);

    /* password boxes show one * per character, unless the eye is open */
    const char *shown = t->buf;
    char masked[TEXT_LEN];
    if (t->secret && !t->reveal) { memset(masked, '*', (size_t)t->len); masked[t->len] = 0; shown = masked; }

    int tx = (int)r.x + 10, ty = (int)(r.y + (r.height - FS) / 2);
    int textW = MeasureText(shown, FS);
    int room  = (int)(r.width - eyeW) - 30;
    int off = textW > room ? textW - room : 0;
    BeginScissorMode((int)r.x + 2, (int)r.y, (int)(r.width - eyeW) - 4, (int)r.height);
    if (t->len == 0) DrawText(placeholder, tx, ty, FS, UI_MUTED);
    else             DrawText(shown, tx - off, ty, FS, UI_TEXT);
    if (t->active && ((int)(GetTime() * 2) % 2) == 0)
        DrawRectangle(tx - off + textW + (t->len ? 2 : 0), ty - 2, 2, FS + 4, UI_TEXT);
    EndScissorMode();
    if (t->secret) {
        Rectangle hit = {eye.x + 4, eye.y + 4, eye.width - 8, eye.height - 8};
        if (eyeHot) DrawRectangleRounded(hit, 0.3f, 6, UI_HOVER);
        /* eye = "show password"; eye with a line = "hide it again" */
        DrawEye(eye.x + eye.width / 2, eye.y + eye.height / 2, t->reveal,
                eyeHot ? UI_TEXT : UI_MUTED, eyeHot ? UI_HOVER : UI_BG);
    }
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

/* The Code tab remembers the open file by name, so it stays open while files
   are added, deleted or committed around it. "" means "open the first one". */
static void SelectFileName(const char *name)
{
    snprintf(selName, PATH_LEN, "%.*s", PATH_LEN - 1, name ? name : "");
    ed.cursor = 0; ed.scrollX = ed.scrollY = 0;
    ed.focused = false;
}

/* Draws and runs the editor. Returns true if the text was changed.
   In read-only mode you can still click, scroll and move the cursor. */
/* Letters like "e" with an accent take 2-4 bytes in UTF-8 text. The extra
   bytes all look like 10xxxxxx, so the cursor skips over those to always
   land at the start of a whole letter. */
static bool IsCont(char c) { return ((unsigned char)c & 0xC0) == 0x80; }
static int  PrevChar(const char *t, int p)          { if (p > 0) p--; while (p > 0 && IsCont(t[p])) p--; return p; }
static int  NextChar(const char *t, int len, int p) { if (p < len) p++; while (p < len && IsCont(t[p])) p++; return p; }
static int  SnapChar(const char *t, int len, int p) { while (p > 0 && p < len && IsCont(t[p])) p--; return p; }

/* git's conflict marker lines: "<<<<<<< yours", "=======", ">>>>>>> theirs" */
static bool LineStarts(const char *t, int p, int le, const char *what)
{
    int n = (int)strlen(what);
    return le - p >= n && memcmp(t + p, what, (size_t)n) == 0;
}
static bool IsMidMarker(const char *t, int p, int le)
{
    int n = le - p;
    if (n > 0 && t[le - 1] == '\r') n--;
    return n == 7 && memcmp(t + p, "=======", 7) == 0;
}
/* Settles one clash (the one whose "<<<<<<<" line starts at start):
   which = 0 keeps your lines, 1 theirs, 2 both (yours first). */
static bool ResolveConflict(WorkFile *f, int start, int which)
{
    const char *t = f->text;
    int len = f->len, firstEnd = LineEnd(t, len, start), mid = -1, end = -1;
    for (int pos = firstEnd + 1; pos < len; ) {
        int le = LineEnd(t, len, pos);
        if (mid < 0 && IsMidMarker(t, pos, le)) mid = pos;
        else if (mid >= 0 && LineStarts(t, pos, le, ">>>>>>>")) { end = pos; break; }
        else if (LineStarts(t, pos, le, "<<<<<<<")) return false;            /* a broken block */
        if (le >= len) break;
        pos = le + 1;
    }
    if (mid < 0 || end < 0 || firstEnd >= len) return false;
    int midEnd = LineEnd(t, len, mid), endEnd = LineEnd(t, len, end);
    int cut = endEnd < len ? endEnd + 1 : endEnd;
    int o0 = firstEnd + 1, o1 = mid, t0 = midEnd + 1, t1 = end;
    Buf b = {0};
    BufAdd(&b, t, (size_t)start);
    if (which != 1) BufAdd(&b, t + o0, (size_t)(o1 - o0));
    if (which != 0) BufAdd(&b, t + t0, (size_t)(t1 - t0));
    BufAdd(&b, t + cut, (size_t)(len - cut));
    if (b.oom) { BufFree(&b); return false; }
    WF_SetN(f, b.d ? (const char *)b.d : "", (int)b.n);
    BufFree(&b);
    return true;
}

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
    ed.cursor = SnapChar(f->text, f->len, ed.cursor);

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
        if (!readOnly && KeyRep(KEY_BACKSPACE) && ed.cursor > 0) {
            int p = PrevChar(f->text, ed.cursor);
            WF_Delete(f, p, ed.cursor - p); ed.cursor = p; changed = true;
        }
        if (!readOnly && KeyRep(KEY_DELETE) && ed.cursor < f->len) {
            WF_Delete(f, ed.cursor, NextChar(f->text, f->len, ed.cursor) - ed.cursor); changed = true;
        }
        if (KeyRep(KEY_LEFT) && ed.cursor > 0)        { ed.cursor = PrevChar(f->text, ed.cursor); moved = true; }
        if (KeyRep(KEY_RIGHT) && ed.cursor < f->len)  { ed.cursor = NextChar(f->text, f->len, ed.cursor); moved = true; }
        if (IsKeyPressed(KEY_HOME))                   { ed.cursor = LineStart(f->text, ed.cursor); moved = true; }
        if (IsKeyPressed(KEY_END))                    { ed.cursor = LineEnd(f->text, f->len, ed.cursor); moved = true; }
        if (KeyRep(KEY_UP)) {
            int ls = LineStart(f->text, ed.cursor), col = ed.cursor - ls;
            if (ls > 0) {
                int pls = LineStart(f->text, ls - 1), plen = (ls - 1) - pls;
                ed.cursor = SnapChar(f->text, f->len, pls + (col < plen ? col : plen));
            }
            moved = true;
        }
        if (KeyRep(KEY_DOWN)) {
            int ls = LineStart(f->text, ed.cursor), col = ed.cursor - ls;
            int le = LineEnd(f->text, f->len, ed.cursor);
            if (le < f->len) {
                int nls = le + 1, nlen = LineEnd(f->text, f->len, nls) - nls;
                ed.cursor = SnapChar(f->text, f->len, nls + (col < nlen ? col : nlen));
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
        ed.cursor = SnapChar(f->text, f->len, ls + XToCol(f->text + ls, le - ls, m.x - textX + ed.scrollX));
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
    DrawRectangleRec(area, UI_BASE);
    BeginScissorMode((int)area.x, (int)area.y, (int)area.width, (int)area.height);
    DrawRectangle((int)area.x, (int)area.y, (int)gutter, (int)area.height, (Color){22, 27, 34, 255});
    const char *t = f->text;
    int p = 0, line = 0, curLine = LineOf(t, ed.cursor);
    int clash = 0;                      /* 0 normal, 1 inside "your" side of a clash, 2 inside "their" side */
    int fixAt = -1, fixHow = 0;         /* a "Keep..." link clicked: done after drawing */
    for (;;) {
        int le = LineEnd(t, f->len, p);
        float y = top + line * LH - ed.scrollY;
        if (y > area.y + area.height) break;
        /* conflict markers colour the lines between them (worked out for every line, even off screen) */
        int marker = 0;                 /* 1 start, 2 middle, 3 end */
        if (LineStarts(t, p, le, "<<<<<<<")) { marker = 1; }
        else if (clash == 1 && IsMidMarker(t, p, le)) marker = 2;
        else if (clash == 2 && LineStarts(t, p, le, ">>>>>>>")) marker = 3;
        int side = marker ? 0 : clash;
        if (marker == 1) clash = 1; else if (marker == 2) clash = 2; else if (marker == 3) clash = 0;
        if (y + LH >= area.y) {
            if (marker)    DrawRectangle((int)(area.x + gutter), (int)y - 2, (int)(area.width - gutter), LH, (Color){247, 129, 102, 45});
            else if (side) DrawRectangle((int)(area.x + gutter), (int)y - 2, (int)(area.width - gutter), LH,
                                         side == 1 ? (Color){56, 139, 253, 32} : (Color){46, 160, 67, 32});
            if (line == curLine && ed.focused)
                DrawRectangle((int)(area.x + gutter), (int)y - 2, (int)(area.width - gutter), LH, (Color){255, 255, 255, 10});

            /* light colouring: headings / #include lines, and // comments */
            int s = p;
            while (s < le && t[s] == ' ') s++;
            Color c = UI_TEXT;
            if (marker) c = UI_ORANGE;
            else if (s < le && t[s] == '#') c = UI_ORANGE;
            else if (s + 1 < le && t[s] == '/' && t[s + 1] == '/') c = UI_MUTED;
            DrawText(Slice(t + p, le - p), (int)(textX - ed.scrollX), (int)y, FS, c);
            if (marker == 1 && !readOnly) {                  /* one-click ways to settle this clash */
                const char *opts[3] = {"Keep yours", "Keep theirs", "Keep both"};
                int lx = (int)(area.x + area.width) - 16;
                for (int o = 2; o >= 0; o--) {
                    int ow = MeasureText(opts[o], FS);
                    lx -= ow;
                    Rectangle hit = {(float)lx - 6, y - 2, (float)ow + 12, LH};
                    bool hh = Hover(hit) && Hover(area);
                    DrawRectangleRec(hit, hh ? (Color){56, 139, 253, 90} : (Color){13, 17, 23, 230});
                    DrawText(opts[o], lx, (int)y, FS, o == 0 ? (Color){120, 180, 255, 255} : o == 1 ? UI_GREENT : UI_TEXT);
                    if (hh) wantCursor = MOUSE_CURSOR_POINTING_HAND;
                    if (hh && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) { fixAt = p; fixHow = o; clickUsed = true; }
                    lx -= 20;
                }
            }

            if (line == curLine && ed.focused && ((int)(GetTime() * 2) % 2) == 0) {
                int cx = (int)(textX - ed.scrollX) + MeasureN(t + p, ed.cursor - p, FS);
                DrawRectangle(cx, (int)y - 2, 2, FS + 4, UI_TEXT);
            }
            /* line numbers, drawn over any text that scrolled left */
            DrawRectangle((int)area.x, (int)y - 2, (int)gutter, LH, (Color){22, 27, 34, 255});
            const char *num = TextFormat("%d", line + 1);
            DrawText(num, (int)(area.x + gutter - 10) - MeasureText(num, FS), (int)y, FS,
                     line == curLine ? UI_TEXT : UI_MUTED);
        }
        if (le >= f->len) break;
        p = le + 1;
        line++;
    }
    EndScissorMode();
    if (fixAt >= 0 && ResolveConflict(f, fixAt, fixHow)) {
        changed = true;
        ed.cursor = fixAt <= f->len ? fixAt : f->len;
    }
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
typedef struct { Repo *r; int files, adds, dels; } StatCtx;
static void StatEach(void *ctx, const char *name, int ob, int nb)
{
    (void)name;
    StatCtx *s = ctx;
    s->files++;
    if (ob == nb || BlobBinary(s->r, ob) || BlobBinary(s->r, nb)) return;   /* binary: no lines to count */
    int a, d;
    ComputeDiff(BlobText(s->r, ob), BlobText(s->r, nb), &a, &d);
    s->adds += a; s->dels += d;
}
static void CommitStats(Repo *r, int ci)
{
    Commit *c = &r->commits[ci];
    if (c->statsDone) return;
    StatCtx s = {r, 0, 0, 0};
    WalkChanges(ParentOf(r, ci), c, StatEach, &s);
    c->sFiles = s.files; c->sAdds = s.adds; c->sDels = s.dels;
    c->statsDone = true;
}

/* The files a commit changed: kind is 'A' added, 'M' modified, 'D' deleted. Cheap - no diffing. */
typedef struct { char *kinds; const char **names; int n, cap; } ListCtx;
static void ListEach(void *ctx, const char *name, int ob, int nb)
{
    ListCtx *l = ctx;
    if (l->n >= l->cap) return;
    l->kinds[l->n] = ob < 0 ? 'A' : nb < 0 ? 'D' : 'M';
    l->names[l->n++] = name;
}
static int CommitFileList(Repo *r, int ci, char *kinds, const char **names, int cap)
{
    ListCtx l = {kinds, names, 0, cap};
    WalkChanges(ParentOf(r, ci), &r->commits[ci], ListEach, &l);
    return l.n;
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
            if (c->author != uid || c->copied || c->time < start || c->time >= end) continue;
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
static void DrawDayTooltip(int uid, long long day)
{
    GatherDay(uid, day);

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
/* JPG reader                                                         */
/*                                                                    */
/* raylib's standard build can open PNG but not JPG, and most photos  */
/* are JPG, so here is a small reader for "baseline" JPG files (the   */
/* kind cameras, phones and most programs save).                      */
/*                                                                    */
/* How a JPG works, in short: the picture is cut into 8x8 squares.    */
/* Each square is stored as 64 "frequency" numbers (mostly zeros),    */
/* squeezed with Huffman coding. To read it we undo the squeezing,    */
/* turn the frequencies back into pixels (the IDCT step), and convert */
/* from brightness + colour-difference (YCbCr) to red/green/blue.     */
/* ------------------------------------------------------------------ */
typedef struct {
    uint8_t counts[17];          /* how many codes of each length 1..16 */
    uint8_t vals[256];
    int     mincode[17], maxcode[18], valptr[17];
    bool    ok;
} JHuff;

typedef struct {
    int id, h, v, tq;            /* sampling factors and which quantisation table */
    int td, ta;                  /* Huffman tables for this scan                  */
    int pred;                    /* last DC value (each block stores a difference) */
    int bw, bh;                  /* size of its pixel plane, in 8x8 blocks        */
    uint8_t *pix;
} JComp;

typedef struct {
    const uint8_t *p;
    size_t n, pos;
    unsigned cur; int nbits; bool marker;
    uint16_t q[4][64];
    JHuff dc[4], ac[4];
    JComp comp[3];
    int ncomp, w, h, hmax, vmax, restart, orientation;
    bool progressive;
    const char *err;
} Jpg;

static const uint8_t ZIGZAG[64] = {
     0, 1, 8,16, 9, 2, 3,10, 17,24,32,25,18,11, 4, 5,
    12,19,26,33,40,48,41,34, 27,20,13, 6, 7,14,21,28,
    35,42,49,56,57,50,43,36, 29,22,15,23,30,37,44,51,
    58,59,52,45,38,31,39,46, 53,60,61,54,47,55,62,63
};

static int JU16(Jpg *j)
{
    if (j->pos + 2 > j->n) { j->err = "the file is cut short"; return 0; }
    int v = j->p[j->pos] << 8 | j->p[j->pos + 1];
    j->pos += 2;
    return v;
}

static unsigned JBit(Jpg *j)
{
    if (j->nbits == 0) {
        unsigned b = 0;
        if (!j->marker && j->pos < j->n) {
            b = j->p[j->pos++];
            if (b == 0xFF) {
                unsigned nx = j->pos < j->n ? j->p[j->pos] : 0;
                if (nx == 0x00) j->pos++;                      /* a real 0xFF byte */
                else { j->marker = true; j->pos--; b = 0; }    /* a marker: stop here */
            }
        }
        j->cur = b;
        j->nbits = 8;
    }
    j->nbits--;
    return (j->cur >> j->nbits) & 1;
}
static int JBits(Jpg *j, int n) { int v = 0; while (n--) v = (v << 1) | (int)JBit(j); return v; }
static int JExtend(int v, int s) { return s == 0 ? 0 : (v < (1 << (s - 1)) ? v - (1 << s) + 1 : v); }

static void JBuildHuff(JHuff *t)
{
    int code = 0, k = 0;
    for (int l = 1; l <= 16; l++) {
        t->valptr[l]  = k;
        t->mincode[l] = code;
        code += t->counts[l];
        k    += t->counts[l];
        t->maxcode[l] = t->counts[l] ? code - 1 : -1;
        code <<= 1;
    }
    t->ok = true;
}
static int JDecode(Jpg *j, JHuff *t)
{
    int code = 0;
    for (int l = 1; l <= 16; l++) {
        code = (code << 1) | (int)JBit(j);
        if (t->maxcode[l] >= 0 && code <= t->maxcode[l]) return t->vals[t->valptr[l] + code - t->mincode[l]];
    }
    j->err = "the picture data is damaged";
    return 0;
}

static float idctTab[8][8];   /* idctTab[x][u] = C(u) * cos((2x+1) u pi / 16) / 2 */
static void JInitIdct(void)
{
    for (int x = 0; x < 8; x++)
        for (int u = 0; u < 8; u++)
            idctTab[x][u] = (u == 0 ? 0.70710678f : 1.0f) * cosf((2 * x + 1) * u * 3.14159265f / 16.0f) / 2.0f;
}

/* Read one 8x8 block and write its pixels into the component's plane. */
static void JBlock(Jpg *j, JComp *c, int bx, int by)
{
    float coef[64] = {0}, tmp[64];
    uint16_t *q = j->q[c->tq];
    int t = JDecode(j, &j->dc[c->td]);
    c->pred += t ? JExtend(JBits(j, t), t) : 0;
    coef[0] = (float)(c->pred * q[0]);
    for (int k = 1; k < 64; ) {
        int rs = JDecode(j, &j->ac[c->ta]), r = rs >> 4, s = rs & 15;
        if (j->err) return;
        if (s == 0) { if (r == 15) { k += 16; continue; } break; }
        k += r;
        if (k > 63) break;
        coef[ZIGZAG[k]] = (float)(JExtend(JBits(j, s), s) * q[k]);
        k++;
    }
    /* turn frequencies back into pixels: rows first, then columns */
    for (int v = 0; v < 8; v++)
        for (int x = 0; x < 8; x++) {
            float s = 0;
            for (int u = 0; u < 8; u++) s += coef[v * 8 + u] * idctTab[x][u];
            tmp[v * 8 + x] = s;
        }
    int stride = c->bw * 8;
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++) {
            float s = 128.0f;
            for (int v = 0; v < 8; v++) s += idctTab[y][v] * tmp[v * 8 + x];
            int px = bx * 8 + x, py = by * 8 + y;
            if (px < stride && py < c->bh * 8)
                c->pix[py * stride + px] = (uint8_t)(s < 0 ? 0 : s > 255 ? 255 : (int)(s + 0.5f));
        }
}

/* After every "restart interval", the file has a marker and the DC differences start over. */
static void JRestart(Jpg *j)
{
    j->nbits = 0;
    j->marker = false;
    while (j->pos + 1 < j->n) {
        if (j->p[j->pos] == 0xFF && j->p[j->pos + 1] >= 0xD0 && j->p[j->pos + 1] <= 0xD7) { j->pos += 2; break; }
        j->pos++;
    }
    for (int i = 0; i < j->ncomp; i++) j->comp[i].pred = 0;
}

static void JScan(Jpg *j)
{
    int len = JU16(j), ns = j->pos < j->n ? j->p[j->pos++] : 0;
    JComp *sc[3];
    if (ns < 1 || ns > 3) { j->err = "the picture data is damaged"; return; }
    for (int i = 0; i < ns; i++) {
        int id = j->p[j->pos++], tables = j->p[j->pos++];
        sc[i] = NULL;
        for (int k = 0; k < j->ncomp; k++) if (j->comp[k].id == id) sc[i] = &j->comp[k];
        if (!sc[i]) { j->err = "the picture data is damaged"; return; }
        sc[i]->td = (tables >> 4) & 3;
        sc[i]->ta = tables & 3;
        if (!j->dc[sc[i]->td].ok || !j->ac[sc[i]->ta].ok) { j->err = "the picture data is damaged"; return; }
    }
    j->pos += 3;                                   /* spectral selection: always 0..63 in baseline */
    (void)len;
    for (int i = 0; i < j->ncomp; i++) j->comp[i].pred = 0;
    j->nbits = 0;
    j->marker = false;

    int count = 0;
    if (ns == 1) {                                 /* one component on its own: blocks in plain order */
        JComp *c = sc[0];
        int cw = (j->w * c->h + j->hmax - 1) / j->hmax, ch = (j->h * c->v + j->vmax - 1) / j->vmax;
        int nx = (cw + 7) / 8, ny = (ch + 7) / 8;
        for (int by = 0; by < ny && !j->err; by++)
            for (int bx = 0; bx < nx && !j->err; bx++) {
                if (j->restart && count && count % j->restart == 0) JRestart(j);
                JBlock(j, c, bx, by);
                count++;
            }
    } else {                                       /* interleaved: each "MCU" holds h*v blocks of every component */
        int mx = (j->w + 8 * j->hmax - 1) / (8 * j->hmax), my = (j->h + 8 * j->vmax - 1) / (8 * j->vmax);
        for (int y = 0; y < my && !j->err; y++)
            for (int x = 0; x < mx && !j->err; x++) {
                if (j->restart && count && count % j->restart == 0) JRestart(j);
                for (int i = 0; i < ns; i++)
                    for (int v = 0; v < sc[i]->v; v++)
                        for (int h = 0; h < sc[i]->h; h++)
                            JBlock(j, sc[i], x * sc[i]->h + h, y * sc[i]->v + v);
                count++;
            }
    }
    /* move on to the next marker */
    j->marker = false;
    while (j->pos + 1 < j->n && !(j->p[j->pos] == 0xFF && j->p[j->pos + 1] != 0x00 &&
                                  !(j->p[j->pos + 1] >= 0xD0 && j->p[j->pos + 1] <= 0xD7))) j->pos++;
}

/* Phones store photos sideways plus a note (EXIF "orientation") saying how to turn them. */
static void JReadExif(Jpg *j, size_t start, size_t len)
{
    const uint8_t *d = j->p + start;
    if (len < 14 || memcmp(d, "Exif\0\0", 6) != 0) return;
    const uint8_t *t = d + 6;
    size_t tl = len - 6;
    bool le = t[0] == 'I';
#define RD16(o) ((o) + 2 <= tl ? (le ? (t[o] | t[(o)+1] << 8) : (t[o] << 8 | t[(o)+1])) : 0)
#define RD32(o) ((o) + 4 <= tl ? (le ? (uint32_t)(t[o] | t[(o)+1] << 8 | t[(o)+2] << 16 | (uint32_t)t[(o)+3] << 24) \
                                     : (uint32_t)((uint32_t)t[o] << 24 | t[(o)+1] << 16 | t[(o)+2] << 8 | t[(o)+3])) : 0)
    uint32_t ifd = RD32(4);
    int entries = RD16(ifd);
    for (int e = 0; e < entries && e < 200; e++) {
        size_t o = ifd + 2 + (size_t)e * 12;
        if (RD16(o) == 0x0112) { j->orientation = RD16(o + 8); break; }
    }
#undef RD16
#undef RD32
}

static Image DecodeJpg(const uint8_t *data, int size, const char **err)
{
    Image img = {0};
    Jpg *j = calloc(1, sizeof(Jpg));
    if (!j) { *err = "out of memory"; return img; }
    j->p = data; j->n = (size_t)size; j->orientation = 1;
    if (size < 4 || data[0] != 0xFF || data[1] != 0xD8) { *err = "it isn't a JPG file"; free(j); return img; }
    j->pos = 2;
    JInitIdct();
    bool sawFrame = false, sawScan = false;

    while (!j->err && j->pos + 4 <= j->n) {
        if (j->p[j->pos] != 0xFF) { j->pos++; continue; }
        int m = j->p[j->pos + 1];
        j->pos += 2;
        if (m == 0xFF || m == 0x01 || (m >= 0xD0 && m <= 0xD7)) { if (m == 0xFF) j->pos--; continue; }
        if (m == 0xD9) break;                                   /* end of picture */
        if (m == 0xDA) {                                        /* start of scan */
            if (!sawFrame) { j->err = "the picture data is damaged"; break; }
            JScan(j);
            sawScan = true;
            continue;
        }
        size_t segStart = j->pos;
        int len = JU16(j);
        if (len < 2 || segStart + (size_t)len > j->n) { j->err = "the file is cut short"; break; }
        size_t segEnd = segStart + (size_t)len;

        if (m == 0xDB) {                                        /* quantisation tables */
            while (j->pos < segEnd) {
                int pq = j->p[j->pos] >> 4, tq = j->p[j->pos] & 3;
                j->pos++;
                for (int k = 0; k < 64; k++) {
                    if (pq) { j->q[tq][k] = (uint16_t)(j->p[j->pos] << 8 | j->p[j->pos + 1]); j->pos += 2; }
                    else      j->q[tq][k] = j->p[j->pos++];
                }
            }
        } else if (m == 0xC4) {                                 /* Huffman tables */
            while (j->pos < segEnd) {
                int tc = j->p[j->pos] >> 4, th = j->p[j->pos] & 3, total = 0;
                j->pos++;
                JHuff *t = tc ? &j->ac[th] : &j->dc[th];
                for (int l = 1; l <= 16; l++) { t->counts[l] = j->p[j->pos++]; total += t->counts[l]; }
                if (total > 256 || j->pos + (size_t)total > segEnd) { j->err = "the picture data is damaged"; break; }
                memcpy(t->vals, j->p + j->pos, (size_t)total);
                j->pos += (size_t)total;
                JBuildHuff(t);
            }
        } else if (m == 0xC0 || m == 0xC1) {                    /* frame header: size and colour layout */
            j->pos++;                                           /* bits per sample: always 8 here */
            j->h = JU16(j);
            j->w = JU16(j);
            j->ncomp = j->p[j->pos++];
            if (j->ncomp != 1 && j->ncomp != 3) { j->err = "CMYK JPG files aren't supported"; break; }
            if (j->w <= 0 || j->h <= 0 || j->w > 16384 || j->h > 16384) { j->err = "the picture is too big"; break; }
            j->hmax = j->vmax = 1;
            for (int i = 0; i < j->ncomp; i++) {
                JComp *c = &j->comp[i];
                c->id = j->p[j->pos++];
                c->h = j->p[j->pos] >> 4; c->v = j->p[j->pos] & 15; j->pos++;
                c->tq = j->p[j->pos++] & 3;
                if (c->h < 1 || c->h > 4 || c->v < 1 || c->v > 4) { j->err = "the picture data is damaged"; break; }
                if (c->h > j->hmax) j->hmax = c->h;
                if (c->v > j->vmax) j->vmax = c->v;
            }
            int mx = (j->w + 8 * j->hmax - 1) / (8 * j->hmax), my = (j->h + 8 * j->vmax - 1) / (8 * j->vmax);
            for (int i = 0; i < j->ncomp && !j->err; i++) {
                JComp *c = &j->comp[i];
                c->bw = mx * c->h;
                c->bh = my * c->v;
                c->pix = calloc((size_t)c->bw * 8 * (size_t)c->bh * 8, 1);
                if (!c->pix) j->err = "out of memory";
            }
            sawFrame = true;
        } else if (m == 0xC2 || m == 0xC6 || m == 0xCA) {
            j->err = "\"progressive\" JPG files aren't supported - try saving it again as PNG";
        } else if (m >= 0xC3 && m <= 0xCF && m != 0xC4 && m != 0xC8 && m != 0xCC) {
            j->err = "this kind of JPG isn't supported - try saving it again as PNG";
        } else if (m == 0xDD) {                                 /* restart interval */
            j->restart = JU16(j);
        } else if (m == 0xE1) {
            JReadExif(j, j->pos, segEnd - j->pos);
        }
        j->pos = segEnd;
    }
    if (!j->err && !sawScan) j->err = "the picture data is damaged";

    if (!j->err) {
        uint8_t *out = malloc((size_t)j->w * (size_t)j->h * 3);
        if (!out) j->err = "out of memory";
        else {
            for (int y = 0; y < j->h; y++)
                for (int x = 0; x < j->w; x++) {
                    float s[3];
                    for (int i = 0; i < j->ncomp; i++) {
                        JComp *c = &j->comp[i];
                        int sx = x * c->h / j->hmax, sy = y * c->v / j->vmax;
                        s[i] = c->pix[sy * c->bw * 8 + sx];
                    }
                    uint8_t *o = out + ((size_t)y * j->w + x) * 3;
                    if (j->ncomp == 1) { o[0] = o[1] = o[2] = (uint8_t)s[0]; continue; }
                    float Y = s[0], cb = s[1] - 128, cr = s[2] - 128;
                    float rgb[3] = {Y + 1.402f * cr, Y - 0.344136f * cb - 0.714136f * cr, Y + 1.772f * cb};
                    for (int k = 0; k < 3; k++) o[k] = (uint8_t)(rgb[k] < 0 ? 0 : rgb[k] > 255 ? 255 : (int)(rgb[k] + 0.5f));
                }
            img.data = out;
            img.width = j->w;
            img.height = j->h;
            img.mipmaps = 1;
            img.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8;
            /* turn it the right way up */
            switch (j->orientation) {
            case 2: ImageFlipHorizontal(&img); break;
            case 3: ImageRotate(&img, 180); break;
            case 4: ImageFlipVertical(&img); break;
            case 5: ImageRotateCW(&img); ImageFlipHorizontal(&img); break;
            case 6: ImageRotateCW(&img); break;
            case 7: ImageRotateCCW(&img); ImageFlipHorizontal(&img); break;
            case 8: ImageRotateCCW(&img); break;
            default: break;
            }
        }
    }
    for (int i = 0; i < 3; i++) free(j->comp[i].pix);
    *err = j->err;
    free(j);
    return img;
}

/* ------------------------------------------------------------------ */
/* Profile pictures                                                   */
/*                                                                    */
/* A picture is cropped to a square, shrunk to 192x192, given a round */
/* see-through edge, saved as PNG, and kept inside minigithub.dat as  */
/* base64 text (a way of writing any bytes using only letters, digits */
/* and + /). So there are no extra files to lose.                     */
/* ------------------------------------------------------------------ */
#define AVATAR_PX 192

static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static char *ToBase64(const uint8_t *d, int n)
{
    char *o = malloc((size_t)(n + 2) / 3 * 4 + 1);
    if (!o) return NULL;
    int j = 0;
    for (int i = 0; i < n; i += 3) {
        uint32_t v = (uint32_t)d[i] << 16 | (i + 1 < n ? d[i + 1] << 8 : 0) | (i + 2 < n ? d[i + 2] : 0);
        o[j++] = B64[v >> 18 & 63];
        o[j++] = B64[v >> 12 & 63];
        o[j++] = i + 1 < n ? B64[v >> 6 & 63] : '=';
        o[j++] = i + 2 < n ? B64[v & 63] : '=';
    }
    o[j] = 0;
    return o;
}

static uint8_t *FromBase64(const char *s, int *outLen)
{
    int n = (int)strlen(s), j = 0, bits = 0;
    uint32_t acc = 0;
    uint8_t *o = malloc((size_t)n * 3 / 4 + 3);
    if (!o) return NULL;
    for (int i = 0; i < n; i++) {
        const char *p = strchr(B64, s[i]);
        if (!p || !s[i]) continue;                       /* skips '=' and anything odd */
        acc = acc << 6 | (uint32_t)(p - B64);
        bits += 6;
        if (bits >= 8) { bits -= 8; o[j++] = (uint8_t)(acc >> bits); }
    }
    *outLen = j;
    return o;
}

static void FreeAvatarTex(User *u)
{
    if (u->texState == 1) UnloadTexture(u->tex);
    u->texState = 0;
}

/* The picture as something raylib can draw, made the first time it's needed. */
static bool AvatarTex(User *u)
{
    if (!u->avatar || u->texState == -1) return false;
    if (u->texState == 1) return true;
    int n = 0;
    uint8_t *png = FromBase64(u->avatar, &n);
    Image img = png ? LoadImageFromMemory(".png", png, n) : (Image){0};
    free(png);
    if (!img.data || img.width <= 0) { u->texState = -1; return false; }
    u->tex = LoadTextureFromImage(img);
    UnloadImage(img);
    GenTextureMipmaps(&u->tex);                          /* keeps it smooth when drawn small */
    SetTextureFilter(u->tex, TEXTURE_FILTER_TRILINEAR);
    u->texState = 1;
    return true;
}

/* Returns NULL on success, or a message saying what went wrong. */
/* Opens a PNG, JPG, GIF or QOI picture held in memory. If it can't,
   returns an empty Image and points *err at the reason. */
static Image LoadAnyImage(const unsigned char *d, int size, const char **err)
{
    Image img = {0};
    *err = NULL;
    if (size > 8 && memcmp(d, "\x89PNG", 4) == 0)                    img = LoadImageFromMemory(".png", d, size);
    else if (size > 3 && d[0] == 0xFF && d[1] == 0xD8)               img = DecodeJpg(d, size, err);
    else if (size > 6 && memcmp(d, "GIF8", 4) == 0)                  img = LoadImageFromMemory(".gif", d, size);
    else if (size > 4 && memcmp(d, "qoif", 4) == 0)                  img = LoadImageFromMemory(".qoi", d, size);
    else if (size > 12 && memcmp(d, "RIFF", 4) == 0 && memcmp(d + 8, "WEBP", 4) == 0)
        *err = "WEBP pictures aren't supported - save it as PNG or JPG first";
    else if (size > 12 && memcmp(d + 4, "ftyp", 4) == 0)
        *err = "HEIC pictures (from iPhones) aren't supported - save it as PNG or JPG first";
    else *err = "that isn't a picture this program can read - use PNG or JPG";
    if (*err && img.data) { UnloadImage(img); img = (Image){0}; }
    if (!*err && (!img.data || img.width <= 0 || img.height <= 0)) {
        if (img.data) UnloadImage(img);
        img = (Image){0};
        *err = "the picture data is damaged";
    }
    return img;
}

static const char *ImportAvatar(User *u, const char *raw)
{
    /* tidy the path: "Copy as path" on Windows adds quotes */
    char path[1024];
    while (*raw == ' ' || *raw == '"' || *raw == '\'') raw++;
    snprintf(path, sizeof path, "%s", raw);
    int n = (int)strlen(path);
    while (n > 0 && (path[n - 1] == ' ' || path[n - 1] == '"' || path[n - 1] == '\'')) path[--n] = 0;
    if (!n)                return "Type or paste the picture's file path first";
    if (!FileExists(path)) return "Couldn't find a file at that path";

    int size = 0;
    unsigned char *d = LoadFileData(path, &size);
    if (!d || size <= 0) return "Couldn't open that file";
    if (size > 40 * 1024 * 1024) { UnloadFileData(d); return "That picture is too big (over 40 MB)"; }

    const char *err = NULL;
    Image img = LoadAnyImage(d, size, &err);
    UnloadFileData(d);
    if (err) return TextFormat("Couldn't use it: %s", err);

    /* square crop from the middle, shrink, round edge */
    ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    int side = img.width < img.height ? img.width : img.height;
    ImageCrop(&img, (Rectangle){(float)((img.width - side) / 2), (float)((img.height - side) / 2), (float)side, (float)side});
    ImageResize(&img, AVATAR_PX, AVATAR_PX);
    Color *px = (Color *)img.data;
    float c = (AVATAR_PX - 1) / 2.0f, rad = AVATAR_PX / 2.0f;
    for (int y = 0; y < AVATAR_PX; y++)
        for (int x = 0; x < AVATAR_PX; x++) {
            float dist = sqrtf((x - c) * (x - c) + (y - c) * (y - c));
            float a = rad - dist;                       /* 1 inside, 0 outside, smooth in between */
            if (a < 0) a = 0;
            if (a > 1) a = 1;
            px[y * AVATAR_PX + x].a = (unsigned char)(px[y * AVATAR_PX + x].a * a);
        }

    int pngSize = 0;
    unsigned char *png = ExportImageToMemory(img, ".png", &pngSize);
    UnloadImage(img);
    if (!png || pngSize <= 0) return "Couldn't convert the picture";
    char *b64 = ToBase64(png, pngSize);
    MemFree(png);
    if (!b64) return "Out of memory";

    FreeAvatarTex(u);
    free(u->avatar);
    u->avatar = b64;
    MarkDirty();
    return NULL;
}

static void RemoveAvatar(User *u)
{
    FreeAvatarTex(u);
    free(u->avatar);
    u->avatar = NULL;
    MarkDirty();
}

/* Colours for people without a picture. 0 in avatarColor means "pick one for me". */
static const Color AVATAR_COLORS[8] = {
    {137, 87, 229, 255}, { 31, 111, 235, 255}, { 35, 134,  54, 255}, {191, 135,  0, 255},
    {207,  34,  46, 255}, {219,  97, 162, 255}, { 20, 150, 160, 255}, {110, 118, 129, 255}
};
static Color AvatarColor(int id)
{
    User *u = FindUserById(id);
    if (u && u->avatarColor >= 1 && u->avatarColor <= 8) return AVATAR_COLORS[u->avatarColor - 1];
    return AVATAR_COLORS[(id < 0 ? 0 : id) % 6];
}

/* The user's picture, or a coloured circle with the first letter of their name. */
static void DrawAvatar(int id, int cx, int cy, int radius)
{
    User *u = FindUserById(id);
    if (u && AvatarTex(u)) {
        DrawTexturePro(u->tex, (Rectangle){0, 0, (float)u->tex.width, (float)u->tex.height},
                       (Rectangle){(float)(cx - radius), (float)(cy - radius), (float)radius * 2, (float)radius * 2},
                       (Vector2){0, 0}, 0, WHITE);
        return;
    }
    const char *n = UserName(id);
    char letter[2] = {(char)toupper((unsigned char)n[0]), 0};
    int fs = radius >= 40 ? 60 : radius >= 24 ? 30 : 20;
    DrawCircle(cx, cy, (float)radius, AvatarColor(id));
    DrawText(letter, cx - MeasureText(letter, fs) / 2, cy - fs / 2 + 1, fs, UI_TEXT);
}

static int UserTheme(int id)
{
    User *u = FindUserById(id);
    return u && u->theme >= 0 && u->theme < THEME_COUNT ? u->theme : THEME_CLASSIC;
}

/* Word-wrapped text. Returns how many lines it used. */
static int DrawWrapped(const char *s, int x, int y, int maxW, int maxLines, Color c)
{
    char line[256];
    int lines = 0;
    const char *p = s;
    while (*p && lines < maxLines) {
        while (*p == ' ') p++;
        int best = 0, i = 0;
        for (;;) {                                       /* take as many whole words as fit */
            while (p[i] && p[i] != ' ') i++;
            if (i > 250) i = 250;
            if (MeasureText(Slice(p, i), FS) > maxW) break;
            best = i;
            if (!p[i] || i >= 250) break;
            i++;
        }
        if (best == 0) {                                 /* one very long word: cut it */
            best = 1;
            while (p[best] && MeasureText(Slice(p, best + 1), FS) <= maxW) best++;
        }
        bool last = lines == maxLines - 1 && p[best];
        snprintf(line, sizeof line, "%.*s%s", best, p, last ? "..." : "");
        if (last) DrawFit(line, x, y + lines * 24, FS, maxW, c);
        else      DrawText(line, x, y + lines * 24, FS, c);
        lines++;
        p += best;
    }
    return lines;
}

/* ------------------------------------------------------------------ */
/* Navigation                                                         */
/* ------------------------------------------------------------------ */
static void Unfocus(void)
{
    tfSearch.active = tfName.active = tfDesc.active = false;
    tfNewFile.active = tfCommit.active = tfIssue.active = false;
    tfUser.active = tfPass.active = tfPass2.active = tfCode.active = false;
    tfUpPath.active = tfUpMsg.active = tfInvite.active = tfEmail.active = tfBranch.active = false;
    tfOldPass.active = tfNewPass.active = tfNewPass2.active = tfDelPass.active = false;
    ed.focused = false;
    armedId = 0;
}

/* Wipe every password box so passwords don't sit around in memory or on
   screen, and close every "eye" so they start hidden again. */
static void ClearSecrets(void)
{
    TextField *s[] = {&tfPass, &tfPass2, &tfOldPass, &tfNewPass, &tfNewPass2, &tfDelPass, &tfCode};
    int n = (int)(sizeof s / sizeof s[0]);
    for (int i = 0; i < n; i++) { memset(s[i]->buf, 0, sizeof s[i]->buf); s[i]->len = 0; s[i]->reveal = false; }
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

static void GoForgot(void)
{
    Unfocus();
    ClearSecrets();
    PrefillUser();
    screen = SCR_FORGOT;
    if (tfUser.len) tfCode.active = true; else tfUser.active = true;
}

/* Shows a freshly made recovery code (it's only ever shown once). */
static void ShowRecoveryCode(User *u, bool afterReset)
{
    Unfocus();
    NewRecoveryCode(u, shownCode);
    codeAfterReset = afterReset;
    MarkDirty();
    screen = SCR_RECOVERY;
}

static void OpenProfile(int id);
static int  ForkCount(Repo *r);

static void GoHome(void)
{
    if (curUser < 0) { GoLogin(); return; }
    OpenProfile(curUser);                          /* your profile is your home page */
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
    viewUser = -1;
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
    User *u = FindUserById(curUser);
    Unfocus();
    ClearSecrets();
    settingsTab = 0;
    scSettings = 0;
    TF_Clear(&tfAvatar);
    snprintf(tfBio.buf, TEXT_LEN, "%s", u ? u->bio : "");
    tfBio.len = (int)strlen(tfBio.buf);
    snprintf(tfEmail.buf, TEXT_LEN, "%s", u ? u->email : "");
    tfEmail.len = (int)strlen(tfEmail.buf);
    screen = SCR_ACCOUNT;
}

static void OpenProfile(int id)
{
    if (!FindUserById(id)) { if (curUser >= 0 && id != curUser) GoHome(); return; }
    Unfocus();
    if (viewUser != id) {                        /* a different person: start at the top, no search */
        scProfile = 0;
        TF_Clear(&tfSearch);
    }
    viewUser = id;
    screen = SCR_PROFILE;
}

static void OpenPeople(void)
{
    Unfocus();
    scPeople = 0;
    screen = SCR_PEOPLE;
}

static void OpenDay(int uid, long long day)
{
    Unfocus();
    dayUser = uid;
    curDay = day;
    scDay = scDayRepos = 0;
    screen = SCR_DAY;
}

static void ForgetScratch(void);

static void OpenRepo(int i)
{
    Unfocus();
    fromDay = false;
    curRepo = i;
    screen  = SCR_REPO;
    tab     = TAB_CODE;
    SelectFileName("");
    scFiles = scCommits = scIssues = scPeopleTab = scGraph = 0;
    ForgetScratch();
    TF_Clear(&tfInvite);
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
        if (changes[i].binary) dOpCount = 0;
        else ComputeDiff(BlobText(r, changes[i].oldB), BlobText(r, changes[i].newB), NULL, NULL);
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
    bool noCode = !HasRecoveryCode(u);
    int inv = InvitationCount(u->id);
    SignIn(u->id);
    if (inv)         Toast(TextFormat("You've been invited to %d repositor%s - see your profile", inv, inv == 1 ? "y" : "ies"));
    else if (noCode) Toast("Tip: make a recovery code in Account settings in case you forget your password");
    else             Toast(TextFormat("Welcome back, %s", nm));
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
    snprintf(u->name, NAME_LEN, "%.*s", NAME_LEN - 1, tfUser.buf);   /* already checked to be short */
    u->created = (long long)time(NULL);
    SetPassword(u, tfPass.buf);

    int  uid = u->id;
    char nm[NAME_LEN];
    snprintf(nm, NAME_LEN, "%s", u->name);
    SignIn(uid);
    int claimed = ClaimOrphans(uid);
    if (optExample && !claimed) SeedExample(uid);
    MarkDirty();
    ShowRecoveryCode(FindUserById(uid), false);
    Toast(claimed ? TextFormat("Welcome, %s - your existing repositories are now yours", nm)
                  : TextFormat("Welcome to Mini GitHub, %s!", nm));
}

/* "Forgot password": username + recovery code -> choose a new password. */
static void DoResetPassword(void)
{
    if (lockT > 0) { Toast(TextFormat("Too many wrong attempts - wait %d seconds", (int)lockT + 1)); return; }
    char norm[CODE_CHARS + 1];
    const char *err = NULL;
    if (!tfUser.buf[0])                                       err = "Enter your username";
    else if (NormalizeCode(tfCode.buf, norm) != CODE_CHARS)   err = "Recovery codes have 16 letters and numbers, like K7QM-2XWD-9PRT-H4NC";
    else if ((int)strlen(tfNewPass.buf) < MIN_PASS)           err = TextFormat("New passwords need at least %d characters", MIN_PASS);
    else if (strcmp(tfNewPass.buf, tfNewPass2.buf) != 0)      err = "The two new passwords don't match";
    if (err) { Toast(err); return; }

    User *u = FindUserByName(tfUser.buf);
    bool ok;
    if (u) ok = CheckRecoveryCode(u, tfCode.buf);
    else {
        char dummy[65];   /* same slow work, so a wrong username looks the same as a wrong code */
        HashPassword(norm, "00000000000000000000000000000000", dummy);
        ok = false;
    }
    if (!ok) {
        memset(tfCode.buf, 0, sizeof tfCode.buf);
        tfCode.len = 0;
        if (++failedLogins >= 5) {
            failedLogins = 0;
            lockT = 30;
            Toast("Too many wrong attempts - paused for 30 seconds");
        } else Toast("That username and recovery code don't match");
        Unfocus();
        tfCode.active = true;
        return;
    }

    SetPassword(u, tfNewPass.buf);
    int uid = u->id;
    SignIn(uid);                                  /* also wipes the password boxes */
    ShowRecoveryCode(FindUserById(uid), true);    /* the old code is used up: here's a new one */
    Toast("Password changed - you're signed in");
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

    /* name + avatar: click to open your profile */
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
    if (Clicked(acct)) OpenProfile(curUser);
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
            if (author != uid || (isCommit && r->commits[k].copied)) continue;
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
        if (Clicked((Rectangle){cell.x - 1, cell.y - 1, 15, 15})) OpenDay(uid, day);
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

/* Draws text broken into lines no wider than maxW (breaking at spaces), any length.
   With draw = false it only measures. Returns the height used. */
static int WrapText(const char *s, int x, int y, int maxW, int fs, Color c, bool draw)
{
    char line[300] = "", trial[300];
    int  lines = 0, step = fs + 8;
    const char *p = s;
    while (*p) {
        const char *w = p;
        while (*p && *p != ' ') p++;
        int wl = (int)(p - w);
        if (wl > 120) wl = 120;                                  /* keeps every line inside the buffers */
        if (line[0]) snprintf(trial, sizeof trial, "%.170s %.*s", line, wl, w);
        else         snprintf(trial, sizeof trial, "%.*s", wl, w);
        if (line[0] && MeasureText(trial, fs) > maxW) {          /* doesn't fit: start a new line */
            if (draw) DrawText(line, x, y + lines * step, fs, c);
            lines++;
            snprintf(line, sizeof line, "%.*s", wl, w);
        } else snprintf(line, sizeof line, "%s", trial);
        while (*p == ' ') p++;
    }
    if (line[0]) { if (draw) DrawText(line, x, y + lines * step, fs, c); lines++; }
    return lines * step;
}

/* Blue clickable text. rightAlign: x is where the text ends. */
static bool LinkText(const char *t, float x, float y, bool rightAlign)
{
    int w = MeasureText(t, FS);
    if (rightAlign) x -= (float)w;
    Rectangle hit = {x, y - 4, (float)w, FS + 8};
    bool hot = Hover(hit);
    if (hot) { wantCursor = MOUSE_CURSOR_POINTING_HAND; DrawLine((int)x, (int)y + FS + 1, (int)x + w, (int)y + FS + 1, UI_LINK); }
    DrawText(t, (int)x, (int)y, FS, UI_LINK);
    return Clicked(hit);
}

static void ScreenForgot(void)
{
    TextField *order[] = {&tfUser, &tfCode, &tfNewPass, &tfNewPass2};
    TabCycle(order, 4);

    float pw = 480, px = ((float)W - pw) / 2;
    DrawCentered("Reset your password", 76, 30, UI_TEXT);

    Rectangle p = {px, 124, pw, 452};
    DrawRectangleRec(p, UI_PANEL);
    DrawRectangleLinesEx(p, 1, UI_BORDER);
    float x = px + 24, y = p.y, fw = pw - 48;

    WrapText("Use the recovery code you saved when you created your account.", (int)x, (int)y + 18, (int)fw, FS, UI_MUTED, true);

    DrawText("Username", (int)x, (int)y + 80, FS, UI_TEXT);
    bool e1 = TextFieldUI(&tfUser, (Rectangle){x, y + 104, fw, 40}, "your-username");

    DrawText("Recovery code", (int)x, (int)y + 156, FS, UI_TEXT);
    bool e2 = TextFieldUI(&tfCode, (Rectangle){x, y + 180, fw, 40}, "XXXX-XXXX-XXXX-XXXX");

    DrawText("New password", (int)x, (int)y + 232, FS, UI_TEXT);
    const char *rule = TextFormat("at least %d characters", MIN_PASS);
    DrawText(rule, (int)(x + fw) - MeasureText(rule, FS), (int)y + 232, FS, tfNewPass.len >= MIN_PASS ? UI_GREENT : UI_MUTED);
    bool e3 = TextFieldUI(&tfNewPass, (Rectangle){x, y + 256, fw, 40}, "");

    DrawText("Confirm new password", (int)x, (int)y + 308, FS, UI_TEXT);
    if (tfNewPass2.len) {
        bool same = strcmp(tfNewPass.buf, tfNewPass2.buf) == 0;
        const char *m = same ? "matches" : "doesn't match";
        DrawText(m, (int)(x + fw) - MeasureText(m, FS), (int)y + 308, FS, same ? UI_GREENT : UI_REDT);
    }
    bool e4 = TextFieldUI(&tfNewPass2, (Rectangle){x, y + 332, fw, 40}, "");

    if (lockT > 0) DrawText(TextFormat("Paused - try again in %d s", (int)lockT + 1), (int)x, (int)y + 378, FS, UI_REDT);
    bool go = Button((Rectangle){x, y + 392, fw, 44}, "Reset password", UI_GREEN);

    /* Enter moves to the next empty box, and resets once everything is filled in */
    TextField *empty = !tfUser.len ? &tfUser : !tfCode.len ? &tfCode : !tfNewPass.len ? &tfNewPass : !tfNewPass2.len ? &tfNewPass2 : NULL;
    if ((e1 || e2 || e3 || e4) && empty) { Unfocus(); empty->active = true; }
    else if (go || e1 || e2 || e3 || e4) DoResetPassword();

    if (SwitchBox(px, 592, pw, "Remembered it? ", "Back to sign in")) GoLogin();
}

/* Shown once after sign-up, after a reset, or when you make a new code. */
static void ScreenRecovery(void)
{
    float pw = 600, px = ((float)W - pw) / 2;
    float x = px + 28, iw = pw - 56;                      /* inner width: text never goes past it */
    const char *intro = codeAfterReset
        ? "Your old code has been used up, so here is a new one. Keep it somewhere safe, like the old one."
        : "If you ever forget your password, this code lets you choose a new one on the \"Forgot password?\" page.";
    const char *warn  = "This is the only time it will be shown.";
    const char *tip   = "Write it down or copy it into a safe place.";
    const char *later = "Lost it? You can make a new one any time in Account settings.";

    /* measure first, so the panel is exactly as tall as what's inside it */
    int hIntro = WrapText(intro, 0, 0, (int)iw, FS, UI_TEXT, false);
    int hWarn  = WrapText(warn,  0, 0, (int)iw, FS, UI_TEXT, false);
    int hTip   = WrapText(tip,   0, 0, (int)iw, FS, UI_TEXT, false);
    int hLater = WrapText(later, 0, 0, (int)iw, FS, UI_TEXT, false);
    float ph = 24 + hIntro + 16 + 80 + 20 + hWarn + hTip + 20 + 44 + 20 + hLater + 16;

    DrawCentered("Save your recovery code", 90, 30, UI_TEXT);
    Rectangle p = {px, 144, pw, ph};
    DrawRectangleRec(p, UI_PANEL);
    DrawRectangleLinesEx(p, 1, UI_YELLOW);

    float y = p.y + 24;
    y += WrapText(intro, (int)x, (int)y, (int)iw, FS, UI_TEXT, true) + 16;

    Rectangle box = {x, y, iw, 80};
    DrawRectangleRec(box, UI_BG);
    DrawRectangleLinesEx(box, 1, UI_BORDER);
    int cfs = 40;                                         /* as big as fits in the box */
    while (cfs > FS && MeasureText(shownCode, cfs) > (int)box.width - 40) cfs -= 2;
    int cw = MeasureText(shownCode, cfs), cy = (int)(box.y + (box.height - (float)cfs) / 2);
    DrawText(shownCode, (int)(box.x + (box.width - (float)cw) / 2), cy, cfs, UI_TEXT);
    DrawText(shownCode, (int)(box.x + (box.width - (float)cw) / 2) + 1, cy, cfs, UI_TEXT);
    y += 80 + 20;

    y += WrapText(warn, (int)x, (int)y, (int)iw, FS, UI_YELLOW, true);
    y += WrapText(tip,  (int)x, (int)y, (int)iw, FS, UI_MUTED,  true) + 20;

    if (Button((Rectangle){x, y, 200, 44}, "Copy code", UI_BTN)) {
        SetClipboardText(shownCode);
        Toast("Recovery code copied to the clipboard");
    }
    bool done = Button((Rectangle){x + iw - 280, y, 280, 44}, "I've saved it - continue", UI_GREEN);
    y += 44 + 20;
    WrapText(later, (int)x, (int)y, (int)iw, FS, UI_MUTED, true);

    if (done || IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) GoHome();
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
    if (LinkText("Forgot password?", x + fw, y + 108, true)) { GoForgot(); return; }
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

/* ---------------- settings: profile, password and account ---------------- */

/* Overlap of two rectangles (used so theme previews stay inside a scrolling list). */
static Rectangle Intersect(Rectangle a, Rectangle b)
{
    float x0 = a.x > b.x ? a.x : b.x, y0 = a.y > b.y ? a.y : b.y;
    float x1 = a.x + a.width < b.x + b.width ? a.x + a.width : b.x + b.width;
    float y1 = a.y + a.height < b.y + b.height ? a.y + a.height : b.y + b.height;
    return (Rectangle){x0, y0, x1 > x0 ? x1 - x0 : 0, y1 > y0 ? y1 - y0 : 0};
}

static void UseAvatarPath(User *u, const char *path)
{
    const char *err = ImportAvatar(u, path);
    if (err) Toast(err);
    else { TF_Clear(&tfAvatar); tfAvatar.active = false; Toast("Profile picture updated"); }
}

static void SettingsProfileTab(User *u, float top)
{
    /* ---- left: picture, colour, bio ---- */
    Rectangle a = {20, top, 460, (float)H - 20 - top};
    DrawRectangleRec(a, UI_PANEL);
    DrawRectangleLinesEx(a, 1, UI_BORDER);
    float x = a.x + 24, y = top, fw = a.width - 48;

    DrawBold("Profile picture", (int)x, (int)y + 18, FS, UI_TEXT);
    DrawCircle((int)x + 56, (int)y + 112, 60, THEMES[UserTheme(u->id)].accent);
    DrawAvatar(u->id, (int)x + 56, (int)y + 112, 56);
    int tx = (int)x + 136;
    DrawText("Drag a PNG or JPG picture",  tx, (int)y + 62, FS, UI_MUTED);
    DrawText("onto this window, or paste", tx, (int)y + 86, FS, UI_MUTED);
    DrawText("its file path below.",       tx, (int)y + 110, FS, UI_MUTED);
    if (u->avatar && Button((Rectangle){(float)tx, y + 142, 180, 34}, "Remove picture", UI_BTN)) {
        RemoveAvatar(u);
        Toast("Profile picture removed");
    }
    bool enter = TextFieldUI(&tfAvatar, (Rectangle){x, y + 190, fw - 100, 40}, "e.g. C:/Users/jo/Pictures/me.jpg");
    if (Button((Rectangle){x + fw - 90, y + 190, 90, 40}, "Use", UI_GREEN) || enter) UseAvatarPath(u, tfAvatar.buf);

    DrawText("Circle colour (when there's no picture)", (int)x, (int)y + 248, FS, UI_TEXT);
    for (int i = 0; i <= 8; i++) {
        Vector2 c = {x + 16 + i * 44.0f, y + 292};
        Rectangle hit = {c.x - 18, c.y - 18, 36, 36};
        bool sel = u->avatarColor == i;
        if (sel) DrawCircleV(c, 19, UI_TEXT);
        if (i == 0) {                                   /* "Auto": the colour picked for you */
            DrawCircleV(c, 16, AVATAR_COLORS[u->id % 6]);
            DrawText("A", (int)c.x - MeasureText("A", FS) / 2, (int)c.y - 10, FS, UI_TEXT);
        } else DrawCircleV(c, 16, AVATAR_COLORS[i - 1]);
        if (Hover(hit)) wantCursor = MOUSE_CURSOR_POINTING_HAND;
        if (Clicked(hit)) { u->avatarColor = i; MarkDirty(); }
    }

    DrawText("Bio", (int)x, (int)y + 336, FS, UI_TEXT);
    const char *cnt = TextFormat("%d / %d", tfBio.len, BIO_LEN);
    DrawText(cnt, (int)(x + fw) - MeasureText(cnt, FS), (int)y + 336, FS, tfBio.len > BIO_LEN ? UI_REDT : UI_MUTED);
    bool bioEnter = TextFieldUI(&tfBio, (Rectangle){x, y + 362, fw, 40}, "A few words about you");
    bool changed = strcmp(tfBio.buf, u->bio) != 0;
    if (ButtonEx((Rectangle){x, y + 414, 140, 38}, "Save bio", changed ? UI_GREEN : UI_BTN, changed) || (bioEnter && changed)) {
        if (tfBio.len > BIO_LEN) Toast(TextFormat("Bios can be at most %d characters", BIO_LEN));
        else { snprintf(u->bio, sizeof u->bio, "%s", tfBio.buf); MarkDirty(); Toast("Bio saved"); }
    }
    if (changed) DrawText("not saved yet", (int)x + 156, (int)y + 423, FS, UI_YELLOW);

    /* ---- right: theme picker ---- */
    Rectangle b = {500, top, (float)W - 520, (float)H - 20 - top};
    DrawRectangleRec(b, UI_PANEL);
    DrawRectangleLinesEx(b, 1, UI_BORDER);
    DrawBold("Theme", (int)b.x + 24, (int)top + 18, FS, UI_TEXT);
    DrawFit("Shown behind the app while you're signed in, and on your profile page.",
            (int)b.x + 24, (int)top + 46, FS, (int)b.width - 48, UI_MUTED);

    Rectangle la = {b.x + 1, top + 84, b.width - 2, b.height - 86};
    float inner = la.width - 46;
    int cols = (int)((inner + 14) / (170 + 14));
    if (cols < 2) cols = 2;
    float cw = (inner - (cols - 1) * 14) / cols, chh = cw * 0.5f;
    if (chh > 100) chh = 100;
    float rowH = chh + 66;
    int rows = (THEME_COUNT + cols - 1) / cols;
    ScrollArea(la, &scSettings, rows * rowH);

    for (int i = 0; i < THEME_COUNT; i++) {
        float cx = la.x + 23 + (i % cols) * (cw + 14), cy = la.y + (i / cols) * rowH - scSettings;
        Rectangle card = {cx, cy, cw, chh};
        Rectangle vis = Intersect(card, la);
        if (vis.height <= 0) continue;
        DrawRectangleRec(vis, UI_BASE);
        DrawThemeClip(i, card, vis, 1.0f);
        BeginScissorMode((int)vis.x, (int)vis.y, (int)vis.width, (int)vis.height);
        bool sel = u->theme == i, hot = Hover(la) && Hover(card);
        DrawRectangleLinesEx(card, sel ? 3.0f : 1.0f, sel ? THEMES[i].accent : hot ? UI_TEXT : UI_BORDER);
        EndScissorMode();
        if (cy + chh + 50 <= la.y + la.height && cy + chh >= la.y) {
            DrawBold(THEMES[i].name, (int)cx, (int)(cy + chh + 8), FS, sel ? THEMES[i].accent : UI_TEXT);
            DrawFit(THEMES[i].blurb, (int)cx, (int)(cy + chh + 32), FS, (int)cw, UI_MUTED);
        }
        if (hot) wantCursor = MOUSE_CURSOR_POINTING_HAND;
        if (Hover(la) && Clicked(card) && !sel) {
            u->theme = i;
            MarkDirty();
            Toast(TextFormat("Theme changed to %s", THEMES[i].name));
        }
    }
    DrawScrollbar(la, scSettings, rows * rowH);
}

static void SettingsAccountTab(User *u, float top)
{
    TextField *order[] = {&tfOldPass, &tfNewPass, &tfNewPass2, &tfDelPass};
    TabCycle(order, 4);
    int owned = ReposOwnedBy(u->id);

    float pw = ((float)W - 60) / 2;
    Rectangle a = {20, top, pw, 336}, b = {40 + pw, top, pw, 336};
    float fw = pw - 48;

    /* change password */
    DrawRectangleRec(a, UI_PANEL);
    DrawRectangleLinesEx(a, 1, UI_BORDER);
    float x = a.x + 24, y = a.y;
    DrawBold("Change password", (int)x, (int)y + 18, FS, UI_TEXT);
    DrawText("Current password", (int)x, (int)y + 54, FS, UI_MUTED);
    bool c1 = TextFieldUI(&tfOldPass, (Rectangle){x, y + 78, fw, 40}, "");
    DrawText("New password", (int)x, (int)y + 126, FS, UI_MUTED);
    bool c2 = TextFieldUI(&tfNewPass, (Rectangle){x, y + 150, fw, 40}, "");
    DrawText("Confirm new password", (int)x, (int)y + 198, FS, UI_MUTED);
    bool c3 = TextFieldUI(&tfNewPass2, (Rectangle){x, y + 222, fw, 40}, "");
    if (Button((Rectangle){x, y + 276, 220, 44}, "Update password", UI_GREEN) || c1 || c2 || c3) DoChangePassword();

    /* delete account */
    DrawRectangleRec(b, UI_PANEL);
    DrawRectangleLinesEx(b, 1, UI_DANGER);
    x = b.x + 24; y = b.y;
    DrawBold("Delete account", (int)x, (int)y + 18, FS, UI_REDT);
    DrawText("This permanently deletes your account", (int)x, (int)y + 54, FS, UI_MUTED);
    DrawText(TextFormat("and all %d of your repositories.", owned), (int)x, (int)y + 80, FS, UI_MUTED);
    DrawText("This can't be undone.", (int)x, (int)y + 106, FS, UI_MUTED);
    DrawText("Type your password to confirm", (int)x, (int)y + 198, FS, UI_TEXT);
    TextFieldUI(&tfDelPass, (Rectangle){x, y + 222, fw, 40}, "");
    if (ConfirmButton((Rectangle){x, y + 276, 260, 44}, "Delete my account", "Click again to delete", 4)) { DoDeleteAccount(); return; }

    /* recovery code */
    bool has = HasRecoveryCode(u);
    Rectangle c = {20, top + 352, (float)W - 40, 88};
    Rectangle bt = {c.x + c.width - 24 - 340, c.y + 22, 340, 44};
    int textW = (int)(bt.x - (c.x + 24) - 24);             /* stop before the button */
    DrawRectangleRec(c, UI_PANEL);
    DrawRectangleLinesEx(c, 1, has ? UI_BORDER : UI_YELLOW);
    DrawBold("Recovery code", (int)c.x + 24, (int)c.y + 14, FS, UI_TEXT);
    WrapText(has ? "Use it if you ever forget your password."
                 : "None yet - you need one to reset a lost password.",
             (int)c.x + 24, (int)c.y + 44, textW, FS, has ? UI_MUTED : UI_YELLOW, true);
    bool make = has ? ConfirmButton(bt, "Make a new code", "Click again - old code stops working", 5)
                    : Button(bt, "Create recovery code", UI_GREEN);
    if (make) ShowRecoveryCode(u, false);
}

/* ---------- Settings > Git ---------- */
/* An email git can use: one @, no spaces or < > (they'd break git's author line). "" is fine (= none). */
static bool UsableEmail(const char *e)
{
    if (!e[0]) return true;
    const char *at = strchr(e, '@');
    if (!at || at == e || !at[1] || strchr(at + 1, '@')) return false;
    for (const char *p = e; *p; p++) if (*p == ' ' || *p == '<' || *p == '>' || (unsigned char)*p < 32) return false;
    return true;
}

static void SettingsGitTab(User *u, float top)
{
    /* the server */
    Rectangle a = {20, top, (float)W - 40, 222};
    DrawRectangleRec(a, UI_PANEL);
    DrawRectangleLinesEx(a, 1, UI_BORDER);
    float x = a.x + 24, y = a.y, fw = a.width - 48;
    DrawBold("Using real git", (int)x, (int)y + 18, FS, UI_TEXT);
    if (gitPort) {
        DrawCircle((int)x + 6, (int)y + 60, 6, UI_GREENT);
        DrawFit(TextFormat("Mini GitHub's git server is running at http://localhost:%d  (only this computer can reach it)", gitPort),
                (int)x + 22, (int)y + 50, FS, (int)fw - 22, UI_TEXT);
    } else {
        DrawCircle((int)x + 6, (int)y + 60, 6, UI_REDT);
        DrawFit(TextFormat("The git server isn't running: %s", gitProblem), (int)x + 22, (int)y + 50, FS, (int)fw - 22, UI_ORANGE);
    }
    DrawFit("1.  Open a repository, press Code, and copy its address.", (int)x, (int)y + 88, FS, (int)fw, UI_MUTED);
    DrawFit("2.  In a terminal, type  git clone  and paste the address.", (int)x, (int)y + 114, FS, (int)fw, UI_MUTED);
    DrawFit("3.  Change files, git commit, then git push. When git asks, use your Mini GitHub username and password.",
            (int)x, (int)y + 140, FS, (int)fw, UI_MUTED);
    DrawFit("To bring in a project you already have: make a new repository without a README, then git push to its address.",
            (int)x, (int)y + 176, FS, (int)fw, UI_MUTED);

    /* the email */
    Rectangle b = {20, top + 238, (float)W - 40, 148};
    DrawRectangleRec(b, UI_PANEL);
    DrawRectangleLinesEx(b, 1, UI_BORDER);
    x = b.x + 24; y = b.y;
    DrawBold("Your email on commits", (int)x, (int)y + 18, FS, UI_TEXT);
    DrawFit("Commits you make here show it in git. Pushed commits with this email count as yours.",
            (int)x, (int)y + 46, FS, (int)fw, UI_MUTED);
    char def[TEXT_LEN];
    snprintf(def, sizeof def, "%s@minigithub.local (if you leave it empty)", u->name);
    bool enter = TextFieldUI(&tfEmail, (Rectangle){x, y + 84, fw - 150, 40}, def);
    bool changed = strcmp(tfEmail.buf, u->email) != 0;
    if (ButtonEx((Rectangle){x + fw - 140, y + 84, 140, 40}, "Save email", changed ? UI_GREEN : UI_BTN, changed) || (enter && changed)) {
        if (!UsableEmail(tfEmail.buf)) Toast("That doesn't look like an email address");
        else { snprintf(u->email, sizeof u->email, "%s", tfEmail.buf); MarkDirty(); Toast(u->email[0] ? "Email saved" : "Email removed"); }
    }
}

static void ScreenAccount(void)
{
    User *u = FindUserById(curUser);
    if (!u) { SignOut(); return; }

    DrawAvatar(u->id, 60, 112, 40);
    DrawBold(u->name, 120, 84, 30, UI_TEXT);
    DrawText("Your settings", 120, 120, FS, UI_MUTED);
    if (Button((Rectangle){(float)W - 160, 88, 140, 40}, "Back", UI_BTN)) { GoHome(); return; }

    int tx = 20;
    const char *labels[3] = {"Profile", "Password & account", "Git"};
    for (int t = 0; t < 3; t++) {
        int w = MeasureText(labels[t], FS) + 28;
        Rectangle tr = {(float)tx, 156, (float)w, 38};
        bool on = settingsTab == t;
        if (Hover(tr)) {
            wantCursor = MOUSE_CURSOR_POINTING_HAND;
            if (!on) DrawRectangleRounded((Rectangle){(float)tx, 158, (float)w, 30}, 0.3f, 6, UI_HOVER);
        }
        DrawText(labels[t], tx + 14, 164, FS, on ? UI_TEXT : UI_MUTED);
        if (on) DrawRectangle(tx, 191, w, 3, uiAccent);
        if (Clicked(tr) && !on) { Unfocus(); ClearSecrets(); settingsTab = t; }
        tx += w + 6;
    }
    DrawLine(0, 194, W, 194, UI_BORDER);

    if (settingsTab == 0)      SettingsProfileTab(u, 210);
    else if (settingsTab == 1) SettingsAccountTab(u, 210);
    else                       SettingsGitTab(u, 210);
}

/* ------------------------------------------------------------------ */
/* Language detection                                                 */
/*                                                                    */
/* Reads a file and guesses what it's written in. Each language has a */
/* list of clues: words it uses (like "def" or "fn"), bits of text it */
/* uses ("std::", "console.log("), and how its lines start            */
/* ("#include", "import "). Every clue found adds points to its       */
/* language; the language with the most points wins.                  */
/*                                                                    */
/* A clue only scores up to 4 times, so one common word can't win on  */
/* its own - a file has to look like the language in several ways.    */
/* The file's ending (.c, .py...) is used as one extra clue, worth a  */
/* few points, so a tiny file can still be recognised.                */
/* ------------------------------------------------------------------ */
enum {
    LANG_C, LANG_CPP, LANG_CSHARP, LANG_JAVA, LANG_PYTHON, LANG_JS, LANG_TS, LANG_RUST, LANG_GO,
    LANG_HTML, LANG_CSS, LANG_SQL, LANG_SHELL, LANG_PHP, LANG_RUBY, LANG_KOTLIN, LANG_SWIFT,
    LANG_ASM, LANG_LUA, LANG_MAKE,
    LANG_FIRST_NONCODE,                       /* everything below isn't counted as code */
    LANG_MARKDOWN = LANG_FIRST_NONCODE, LANG_JSON, LANG_YAML, LANG_TEXT,
    LANG_COUNT
};
static const char *LANG_NAMES[LANG_COUNT] = {
    "C", "C++", "C#", "Java", "Python", "JavaScript", "TypeScript", "Rust", "Go",
    "HTML", "CSS", "SQL", "Shell", "PHP", "Ruby", "Kotlin", "Swift",
    "Assembly", "Lua", "Makefile",
    "Markdown", "JSON", "YAML", "Plain text"
};
#define LANG_BINARY (-2)                      /* not text at all (pictures and so on) */

/* how a clue is matched */
enum { K_WORD,      /* a whole word, e.g. "def" but not "define"             */
       K_WORDI,     /* a whole word, any capitals (SQL: select = SELECT)     */
       K_SUB,       /* this text anywhere                                    */
       K_START,     /* a line starts with this (after any spaces)            */
       K_STARTI,    /* the same, any capitals                                */
       K_END,       /* a line ends with this                                 */
       K_SHEBANG }; /* the first line is "#!..." and contains this           */

typedef struct { unsigned char lang, kind, weight; const char *text; } Clue;

static const Clue CLUES[] = {
    /* ---- C ---- */
    {LANG_C, K_START, 3, "#include"}, {LANG_C, K_SUB, 3, "#include <stdio.h>"}, {LANG_C, K_SUB, 3, "#include <stdlib.h>"},
    {LANG_C, K_SUB, 2, ".h\""}, {LANG_C, K_SUB, 2, "printf("}, {LANG_C, K_SUB, 3, "malloc("}, {LANG_C, K_SUB, 2, "free("},
    {LANG_C, K_SUB, 2, "typedef struct"}, {LANG_C, K_START, 2, "#define"}, {LANG_C, K_START, 2, "#ifdef"}, {LANG_C, K_START, 2, "#ifndef"},
    {LANG_C, K_START, 2, "#endif"}, {LANG_C, K_WORD, 1, "unsigned"}, {LANG_C, K_WORD, 1, "sizeof"}, {LANG_C, K_SUB, 1, "->"},
    {LANG_C, K_WORD, 1, "static"}, {LANG_C, K_WORD, 1, "void"}, {LANG_C, K_WORD, 1, "int"}, {LANG_C, K_SUB, 2, "int main("},
    {LANG_C, K_WORD, 1, "struct"}, {LANG_C, K_SUB, 2, "NULL"}, {LANG_C, K_WORD, 1, "const"}, {LANG_C, K_SUB, 1, "/*"},
    /* ---- C++ (on top of C: these are things C doesn't have) ---- */
    {LANG_CPP, K_START, 3, "#include"}, {LANG_CPP, K_SUB, 4, "std::"}, {LANG_CPP, K_WORD, 4, "namespace"}, {LANG_CPP, K_SUB, 4, "template <"},
    {LANG_CPP, K_SUB, 4, "template<"}, {LANG_CPP, K_SUB, 3, "#include <iostream>"}, {LANG_CPP, K_SUB, 3, "#include <vector>"},
    {LANG_CPP, K_SUB, 3, "#include <string>"}, {LANG_CPP, K_WORD, 3, "cout"}, {LANG_CPP, K_WORD, 3, "nullptr"},
    {LANG_CPP, K_START, 3, "public:"}, {LANG_CPP, K_START, 3, "private:"}, {LANG_CPP, K_START, 3, "protected:"},
    {LANG_CPP, K_WORD, 2, "class"}, {LANG_CPP, K_SUB, 2, "::"}, {LANG_CPP, K_WORD, 2, "virtual"}, {LANG_CPP, K_WORD, 2, "override"},
    {LANG_CPP, K_SUB, 3, "using namespace"}, {LANG_CPP, K_WORD, 2, "constexpr"}, {LANG_CPP, K_WORD, 2, "typename"},
    {LANG_CPP, K_START, 2, "#define"}, {LANG_CPP, K_START, 2, "#ifndef"}, {LANG_CPP, K_START, 2, "#endif"}, {LANG_CPP, K_WORD, 1, "const"},
    {LANG_CPP, K_WORD, 1, "void"}, {LANG_CPP, K_WORD, 1, "int"}, {LANG_CPP, K_SUB, 2, "auto "},
    /* ---- C# ---- */
    {LANG_CSHARP, K_START, 5, "using System"}, {LANG_CSHARP, K_SUB, 4, "Console.Write"}, {LANG_CSHARP, K_WORD, 3, "namespace"},
    {LANG_CSHARP, K_SUB, 4, "{ get; set; }"}, {LANG_CSHARP, K_SUB, 3, "{ get;"}, {LANG_CSHARP, K_WORD, 2, "public"}, {LANG_CSHARP, K_WORD, 2, "class"},
    {LANG_CSHARP, K_WORD, 2, "string"}, {LANG_CSHARP, K_WORD, 2, "var"}, {LANG_CSHARP, K_SUB, 3, "static void Main"},
    {LANG_CSHARP, K_WORD, 3, "readonly"}, {LANG_CSHARP, K_START, 3, "[Test"}, {LANG_CSHARP, K_WORD, 2, "async"}, {LANG_CSHARP, K_SUB, 2, "await "},
    {LANG_CSHARP, K_WORD, 2, "foreach"}, {LANG_CSHARP, K_START, 2, "///"}, {LANG_CSHARP, K_WORD, 2, "internal"},
    /* ---- Java ---- */
    {LANG_JAVA, K_START, 5, "import java."}, {LANG_JAVA, K_START, 4, "package "}, {LANG_JAVA, K_SUB, 5, "System.out.print"},
    {LANG_JAVA, K_SUB, 5, "public static void main(String"}, {LANG_JAVA, K_WORD, 2, "public"}, {LANG_JAVA, K_WORD, 2, "class"},
    {LANG_JAVA, K_WORD, 2, "private"}, {LANG_JAVA, K_WORD, 2, "extends"}, {LANG_JAVA, K_WORD, 3, "implements"}, {LANG_JAVA, K_START, 3, "@Override"},
    {LANG_JAVA, K_WORD, 2, "String"}, {LANG_JAVA, K_WORD, 3, "throws"}, {LANG_JAVA, K_WORD, 2, "new"}, {LANG_JAVA, K_WORD, 2, "final"},
    {LANG_JAVA, K_START, 2, "import "}, {LANG_JAVA, K_START, 2, "/**"},
    /* ---- Python ---- */
    {LANG_PYTHON, K_START, 4, "def "}, {LANG_PYTHON, K_START, 3, "import "}, {LANG_PYTHON, K_START, 4, "from "}, {LANG_PYTHON, K_SUB, 3, " import "},
    {LANG_PYTHON, K_WORD, 4, "self"}, {LANG_PYTHON, K_WORD, 4, "elif"}, {LANG_PYTHON, K_SUB, 4, "__init__"}, {LANG_PYTHON, K_SUB, 3, "__name__"},
    {LANG_PYTHON, K_END, 1, ":"}, {LANG_PYTHON, K_START, 3, "if __name__"}, {LANG_PYTHON, K_START, 2, "return "},
    {LANG_PYTHON, K_START, 3, "raise "}, {LANG_PYTHON, K_START, 3, "try:"}, {LANG_PYTHON, K_START, 3, "with "}, {LANG_PYTHON, K_SUB, 2, "f\""}, {LANG_PYTHON, K_WORD, 2, "None"}, {LANG_PYTHON, K_WORD, 2, "True"}, {LANG_PYTHON, K_WORD, 2, "False"},
    {LANG_PYTHON, K_START, 3, "class "}, {LANG_PYTHON, K_SUB, 2, "print("}, {LANG_PYTHON, K_START, 2, "#"}, {LANG_PYTHON, K_SUB, 3, "\"\"\""},
    {LANG_PYTHON, K_WORD, 2, "lambda"}, {LANG_PYTHON, K_WORD, 2, "except"}, {LANG_PYTHON, K_START, 3, "@"}, {LANG_PYTHON, K_SHEBANG, 9, "python"},
    {LANG_PYTHON, K_WORD, 1, "not"}, {LANG_PYTHON, K_WORD, 1, "in"}, {LANG_PYTHON, K_WORD, 2, "pass"},
    /* ---- JavaScript ---- */
    {LANG_JS, K_WORD, 3, "function"}, {LANG_JS, K_START, 2, "const "}, {LANG_JS, K_START, 2, "let "}, {LANG_JS, K_WORD, 2, "var"},
    {LANG_JS, K_SUB, 3, "=>"}, {LANG_JS, K_SUB, 4, "console.log("}, {LANG_JS, K_SUB, 4, "require("}, {LANG_JS, K_SUB, 4, "module.exports"},
    {LANG_JS, K_SUB, 3, "document."}, {LANG_JS, K_SUB, 3, "===",}, {LANG_JS, K_SUB, 2, "!=="}, {LANG_JS, K_WORD, 2, "undefined"},
    {LANG_JS, K_START, 3, "export "}, {LANG_JS, K_START, 3, "import "}, {LANG_JS, K_WORD, 2, "async"}, {LANG_JS, K_WORD, 2, "await"},
    {LANG_JS, K_WORD, 2, "this"}, {LANG_JS, K_SUB, 2, "});"}, {LANG_JS, K_SHEBANG, 9, "node"}, {LANG_JS, K_START, 2, "'use strict'"},
    {LANG_JS, K_START, 2, "\"use strict\""}, {LANG_JS, K_WORD, 1, "return"}, {LANG_JS, K_WORD, 1, "null"},
    /* ---- TypeScript (on top of JavaScript: types) ---- */
    {LANG_TS, K_WORD, 3, "function"}, {LANG_TS, K_START, 2, "const "}, {LANG_TS, K_START, 2, "let "}, {LANG_TS, K_SUB, 3, "=>"},
    {LANG_TS, K_START, 3, "export "}, {LANG_TS, K_START, 3, "import "}, {LANG_TS, K_SUB, 2, "==="}, {LANG_TS, K_WORD, 2, "async"},
    {LANG_TS, K_WORD, 2, "this"}, {LANG_TS, K_WORD, 1, "return"}, {LANG_TS, K_WORD, 1, "null"}, {LANG_TS, K_WORD, 2, "undefined"},
    {LANG_TS, K_SUB, 5, ": string;"}, {LANG_TS, K_SUB, 5, ": string)"}, {LANG_TS, K_SUB, 5, ": string,"}, {LANG_TS, K_SUB, 4, ": string ="},
    {LANG_TS, K_SUB, 5, ": number;"}, {LANG_TS, K_SUB, 5, ": number)"}, {LANG_TS, K_SUB, 5, ": number,"}, {LANG_TS, K_SUB, 5, ": boolean;"},
    {LANG_TS, K_SUB, 4, ": boolean)"}, {LANG_TS, K_SUB, 4, "): string"}, {LANG_TS, K_SUB, 4, "): number"}, {LANG_TS, K_SUB, 4, "): boolean"},
    {LANG_TS, K_SUB, 3, "?: "}, {LANG_TS, K_WORD, 4, "interface"},
    {LANG_TS, K_START, 5, "export type"}, {LANG_TS, K_START, 5, "export interface"}, {LANG_TS, K_START, 5, "declare "},
    {LANG_TS, K_SUB, 4, "): void"}, {LANG_TS, K_SUB, 3, "readonly "}, {LANG_TS, K_SUB, 3, "<T>"}, {LANG_TS, K_SUB, 3, ": any;"},
    {LANG_TS, K_SUB, 3, ": any)"},
    {LANG_TS, K_SUB, 3, "): Promise<"}, {LANG_TS, K_WORD, 2, "enum"},
    /* ---- Rust ---- */
    {LANG_RUST, K_START, 4, "fn "}, {LANG_RUST, K_START, 4, "pub fn "}, {LANG_RUST, K_SUB, 5, "let mut "}, {LANG_RUST, K_SUB, 5, "println!("},
    {LANG_RUST, K_START, 4, "use std::"}, {LANG_RUST, K_START, 3, "impl "}, {LANG_RUST, K_SUB, 3, "&mut "}, {LANG_RUST, K_WORD, 3, "match"},
    {LANG_RUST, K_SUB, 3, "::new("}, {LANG_RUST, K_SUB, 3, "Vec<"}, {LANG_RUST, K_SUB, 3, "Option<"}, {LANG_RUST, K_SUB, 3, "Result<"},
    {LANG_RUST, K_START, 3, "#[derive("}, {LANG_RUST, K_SUB, 2, "-> "}, {LANG_RUST, K_WORD, 3, "struct"}, {LANG_RUST, K_SUB, 3, ".unwrap()"},
    {LANG_RUST, K_START, 3, "mod "}, {LANG_RUST, K_WORD, 2, "pub"}, {LANG_RUST, K_SUB, 2, "Some("}, {LANG_RUST, K_WORD, 2, "enum"},
    /* ---- Go ---- */
    {LANG_GO, K_START, 5, "package "}, {LANG_GO, K_START, 4, "func "}, {LANG_GO, K_SUB, 4, ":="}, {LANG_GO, K_SUB, 4, "fmt."},
    {LANG_GO, K_START, 4, "import ("}, {LANG_GO, K_SUB, 3, "err != nil"}, {LANG_GO, K_WORD, 3, "chan"}, {LANG_GO, K_WORD, 2, "defer"},
    {LANG_GO, K_SUB, 3, "go func"}, {LANG_GO, K_WORD, 2, "struct"}, {LANG_GO, K_WORD, 1, "nil"}, {LANG_GO, K_SUB, 2, "[]string"},
    {LANG_GO, K_SUB, 2, "interface{}"}, {LANG_GO, K_START, 3, "type "},
    /* ---- HTML ---- */
    {LANG_HTML, K_SUB, 6, "<!DOCTYPE"}, {LANG_HTML, K_SUB, 6, "<!doctype"}, {LANG_HTML, K_SUB, 5, "<html"}, {LANG_HTML, K_SUB, 3, "<head"},
    {LANG_HTML, K_SUB, 3, "<body"}, {LANG_HTML, K_SUB, 3, "<div"}, {LANG_HTML, K_SUB, 3, "</div>"}, {LANG_HTML, K_SUB, 2, "<p>"},
    {LANG_HTML, K_SUB, 2, "</p>"}, {LANG_HTML, K_SUB, 2, "href=\""}, {LANG_HTML, K_SUB, 2, "<a "}, {LANG_HTML, K_SUB, 2, "<span"},
    {LANG_HTML, K_SUB, 2, "<meta"}, {LANG_HTML, K_SUB, 2, "<script"}, {LANG_HTML, K_SUB, 2, "<title>"}, {LANG_HTML, K_SUB, 2, "class=\""},
    {LANG_HTML, K_SUB, 2, "<li>"}, {LANG_HTML, K_SUB, 2, "<br"}, {LANG_HTML, K_SUB, 2, "<table"}, {LANG_HTML, K_SUB, 3, "</"},
    {LANG_HTML, K_SUB, 2, "<!--"}, {LANG_HTML, K_SUB, 2, "<h1"}, {LANG_HTML, K_SUB, 2, "<h2"}, {LANG_HTML, K_SUB, 2, "<h3"},
    {LANG_HTML, K_SUB, 2, "<ul"}, {LANG_HTML, K_SUB, 2, "<img"}, {LANG_HTML, K_SUB, 2, "<link"}, {LANG_HTML, K_SUB, 2, "<style"},
    {LANG_HTML, K_SUB, 2, "<input"}, {LANG_HTML, K_SUB, 2, "<form"}, {LANG_HTML, K_SUB, 2, "<button"}, {LANG_HTML, K_SUB, 2, "<nav"},
    {LANG_HTML, K_SUB, 2, "<section"}, {LANG_HTML, K_SUB, 2, "<footer"}, {LANG_HTML, K_SUB, 2, "<header"}, {LANG_HTML, K_SUB, 2, "<td"},
    {LANG_HTML, K_SUB, 2, "<tr"}, {LANG_HTML, K_SUB, 2, "<code"}, {LANG_HTML, K_SUB, 2, "<pre"}, {LANG_HTML, K_SUB, 2, "<svg"},
    {LANG_HTML, K_SUB, 2, "{% "}, {LANG_HTML, K_SUB, 2, "{{ "},
    /* ---- CSS ---- */
    {LANG_CSS, K_SUB, 3, "px;"}, {LANG_CSS, K_START, 3, "color:"}, {LANG_CSS, K_START, 3, "margin"}, {LANG_CSS, K_START, 3, "padding"},
    {LANG_CSS, K_START, 3, "@media"}, {LANG_CSS, K_START, 3, "display:"}, {LANG_CSS, K_START, 3, "font-"}, {LANG_CSS, K_START, 2, "background"},
    {LANG_CSS, K_SUB, 2, "!important"}, {LANG_CSS, K_START, 2, "border"}, {LANG_CSS, K_START, 2, "width:"}, {LANG_CSS, K_START, 2, "height:"},
    {LANG_CSS, K_SUB, 2, "em;"}, {LANG_CSS, K_SUB, 2, "rgba("}, {LANG_CSS, K_START, 2, "text-align:"}, {LANG_CSS, K_START, 2, "position:"},
    {LANG_CSS, K_START, 2, "cursor:"}, {LANG_CSS, K_START, 2, "transition:"}, {LANG_CSS, K_START, 2, "opacity:"}, {LANG_CSS, K_START, 2, "--"},
    {LANG_CSS, K_SUB, 2, ":hover"}, {LANG_CSS, K_SUB, 2, "var(--"}, {LANG_CSS, K_START, 2, "@import"}, {LANG_CSS, K_START, 2, "@font-face"},
    /* ---- SQL ---- */
    {LANG_SQL, K_WORDI, 3, "select"}, {LANG_SQL, K_WORDI, 3, "from"}, {LANG_SQL, K_WORDI, 3, "where"}, {LANG_SQL, K_SUB, 4, "INSERT INTO"},
    {LANG_SQL, K_SUB, 4, "CREATE TABLE"}, {LANG_SQL, K_SUB, 4, "create table"}, {LANG_SQL, K_WORDI, 2, "join"}, {LANG_SQL, K_SUB, 3, "VALUES"},
    {LANG_SQL, K_SUB, 3, "PRIMARY KEY"}, {LANG_SQL, K_WORDI, 2, "varchar"}, {LANG_SQL, K_SUB, 3, "GROUP BY"}, {LANG_SQL, K_SUB, 3, "ORDER BY"},
    {LANG_SQL, K_START, 2, "--"}, {LANG_SQL, K_WORDI, 2, "update"}, {LANG_SQL, K_WORDI, 2, "delete"}, {LANG_SQL, K_END, 1, ";"},
    {LANG_SQL, K_SUB, 3, "CREATE "}, {LANG_SQL, K_SUB, 3, "DROP "}, {LANG_SQL, K_SUB, 3, "SELECT "}, {LANG_SQL, K_SUB, 2, "BEGIN;"},
    /* ---- Shell ---- */
    {LANG_SHELL, K_SHEBANG, 9, "sh"}, {LANG_SHELL, K_SHEBANG, 9, "bash"}, {LANG_SHELL, K_START, 3, "echo "}, {LANG_SHELL, K_START, 3, "fi"},
    {LANG_SHELL, K_SUB, 3, "; then"}, {LANG_SHELL, K_START, 2, "then"}, {LANG_SHELL, K_START, 3, "done"}, {LANG_SHELL, K_SUB, 3, "; do"},
    {LANG_SHELL, K_SUB, 3, "$("}, {LANG_SHELL, K_SUB, 2, "${"}, {LANG_SHELL, K_WORD, 3, "esac"}, {LANG_SHELL, K_START, 2, "export "},
    {LANG_SHELL, K_SUB, 2, "[ -"}, {LANG_SHELL, K_SUB, 2, "[[ "}, {LANG_SHELL, K_SUB, 2, " 2>&1"}, {LANG_SHELL, K_SUB, 2, "$1"},
    {LANG_SHELL, K_SUB, 2, "\"$@\""}, {LANG_SHELL, K_START, 2, "#"}, {LANG_SHELL, K_START, 2, "set -e"},
    /* ---- PHP ---- */
    {LANG_PHP, K_SUB, 9, "<?php"}, {LANG_PHP, K_SHEBANG, 9, "php"}, {LANG_PHP, K_SUB, 4, "$this->"}, {LANG_PHP, K_SUB, 2, "->"}, {LANG_PHP, K_WORD, 2, "function"},
    {LANG_PHP, K_START, 2, "echo "}, {LANG_PHP, K_SUB, 3, "array("}, {LANG_PHP, K_SUB, 2, "$_"}, {LANG_PHP, K_SUB, 2, "=> "},
    {LANG_PHP, K_START, 3, "namespace "}, {LANG_PHP, K_START, 3, "use "}, {LANG_PHP, K_WORD, 2, "public"},
    /* ---- Ruby ---- */
    {LANG_RUBY, K_START, 3, "def "}, {LANG_RUBY, K_START, 4, "end"}, {LANG_RUBY, K_END, 3, " do"}, {LANG_RUBY, K_SUB, 3, "#{"},
    {LANG_RUBY, K_SUB, 2, ".new"}, {LANG_RUBY, K_SUB, 2, "nil?"}, {LANG_RUBY, K_START, 3, "require "}, {LANG_RUBY, K_START, 3, "require_relative"},
    {LANG_RUBY, K_SUB, 3, " do |"}, {LANG_RUBY, K_SUB, 3, ".each"}, {LANG_RUBY, K_START, 3, "puts "}, {LANG_RUBY, K_SUB, 3, "attr_"},
    {LANG_RUBY, K_START, 3, "module "}, {LANG_RUBY, K_START, 2, "class "}, {LANG_RUBY, K_WORD, 2, "unless"}, {LANG_RUBY, K_WORD, 2, "nil"},
    {LANG_RUBY, K_SUB, 2, "@"}, {LANG_RUBY, K_SHEBANG, 9, "ruby"}, {LANG_RUBY, K_START, 2, "#"}, {LANG_RUBY, K_WORD, 2, "elsif"},
    {LANG_RUBY, K_SUB, 2, " => "}, {LANG_RUBY, K_SUB, 2, ":symbol"},
    /* ---- Kotlin ---- */
    {LANG_KOTLIN, K_START, 4, "fun "}, {LANG_KOTLIN, K_START, 3, "val "}, {LANG_KOTLIN, K_START, 2, "var "}, {LANG_KOTLIN, K_START, 4, "import kotlin"},
    {LANG_KOTLIN, K_SUB, 3, "println("}, {LANG_KOTLIN, K_START, 3, "package "}, {LANG_KOTLIN, K_SUB, 3, "data class"}, {LANG_KOTLIN, K_WORD, 3, "when"},
    {LANG_KOTLIN, K_SUB, 2, "?."}, {LANG_KOTLIN, K_SUB, 2, "!!"}, {LANG_KOTLIN, K_WORD, 2, "override"}, {LANG_KOTLIN, K_SUB, 2, ": String"},
    {LANG_KOTLIN, K_SUB, 2, ": Int"},
    /* ---- Swift ---- */
    {LANG_SWIFT, K_START, 4, "func "}, {LANG_SWIFT, K_START, 4, "import Foundation"}, {LANG_SWIFT, K_START, 4, "import UIKit"},
    {LANG_SWIFT, K_START, 4, "import SwiftUI"}, {LANG_SWIFT, K_WORD, 3, "guard"}, {LANG_SWIFT, K_START, 2, "let "}, {LANG_SWIFT, K_START, 2, "var "},
    {LANG_SWIFT, K_SUB, 2, "print("}, {LANG_SWIFT, K_SUB, 3, "-> "}, {LANG_SWIFT, K_SUB, 2, "?? "}, {LANG_SWIFT, K_WORD, 2, "struct"},
    {LANG_SWIFT, K_SUB, 3, ": View"}, {LANG_SWIFT, K_SUB, 2, "@State"},
    /* ---- Assembly (x86 and 68k) ---- */
    {LANG_ASM, K_STARTI, 3, "mov"}, {LANG_ASM, K_STARTI, 3, "move."}, {LANG_ASM, K_SUB, 4, "section .text"}, {LANG_ASM, K_SUB, 4, "section .data"},
    {LANG_ASM, K_START, 4, "global _start"}, {LANG_ASM, K_STARTI, 3, "syscall"}, {LANG_ASM, K_SUB, 3, "int 0x80"}, {LANG_ASM, K_WORDI, 2, "eax"},
    {LANG_ASM, K_WORDI, 2, "rax"}, {LANG_ASM, K_WORDI, 2, "rsp"}, {LANG_ASM, K_STARTI, 2, "jmp"}, {LANG_ASM, K_STARTI, 2, "push"},
    {LANG_ASM, K_STARTI, 2, "pop"}, {LANG_ASM, K_STARTI, 2, "lea"}, {LANG_ASM, K_STARTI, 2, "ret"}, {LANG_ASM, K_STARTI, 2, "call"},
    {LANG_ASM, K_STARTI, 2, "cmp"}, {LANG_ASM, K_STARTI, 2, "dc.b"}, {LANG_ASM, K_START, 3, ".globl"}, {LANG_ASM, K_START, 3, ".text"},
    {LANG_ASM, K_START, 2, ".section"}, {LANG_ASM, K_START, 2, ".align"}, {LANG_ASM, K_SUB, 2, "%rsp"}, {LANG_ASM, K_SUB, 2, "%eax"},
    {LANG_ASM, K_START, 2, ";"}, {LANG_ASM, K_STARTI, 2, "add"}, {LANG_ASM, K_STARTI, 2, "sub"}, {LANG_ASM, K_STARTI, 2, "bne"},
    {LANG_ASM, K_STARTI, 2, "beq"}, {LANG_ASM, K_START, 2, ".type"}, {LANG_ASM, K_START, 2, ".size"}, {LANG_ASM, K_STARTI, 2, "xor"},
    {LANG_ASM, K_STARTI, 2, "movq"}, {LANG_ASM, K_STARTI, 2, "movl"}, {LANG_ASM, K_STARTI, 2, "movb"}, {LANG_ASM, K_STARTI, 2, "leaq"},
    {LANG_ASM, K_STARTI, 2, "addq"}, {LANG_ASM, K_STARTI, 2, "subq"}, {LANG_ASM, K_STARTI, 2, "cmpq"}, {LANG_ASM, K_STARTI, 2, "testq"},
    {LANG_ASM, K_STARTI, 2, "jne"}, {LANG_ASM, K_STARTI, 2, "je"}, {LANG_ASM, K_STARTI, 2, "jz"}, {LANG_ASM, K_STARTI, 2, "jnz"},
    {LANG_ASM, K_STARTI, 2, "jl"}, {LANG_ASM, K_STARTI, 2, "jg"}, {LANG_ASM, K_STARTI, 2, "pushq"}, {LANG_ASM, K_STARTI, 2, "popq"},
    {LANG_ASM, K_STARTI, 2, "xorl"}, {LANG_ASM, K_STARTI, 2, "andq"}, {LANG_ASM, K_STARTI, 2, "shlq"}, {LANG_ASM, K_STARTI, 2, "shrq"},
    {LANG_ASM, K_STARTI, 2, "ldr"}, {LANG_ASM, K_STARTI, 2, "str"}, {LANG_ASM, K_STARTI, 2, "bl"}, {LANG_ASM, K_STARTI, 2, "stp"},
    {LANG_ASM, K_STARTI, 2, "ldp"}, {LANG_ASM, K_STARTI, 2, "nop"}, {LANG_ASM, K_STARTI, 2, "inc"}, {LANG_ASM, K_STARTI, 2, "dec"},
    {LANG_ASM, K_STARTI, 2, "trap"}, {LANG_ASM, K_STARTI, 2, "rts"}, {LANG_ASM, K_STARTI, 2, "bra"}, {LANG_ASM, K_STARTI, 2, "jsr"},
    {LANG_ASM, K_STARTI, 2, "dc.l"}, {LANG_ASM, K_STARTI, 2, "dc.w"}, {LANG_ASM, K_STARTI, 2, "org"},
    {LANG_ASM, K_START, 2, ".cfi_"}, {LANG_ASM, K_START, 2, ".long"}, {LANG_ASM, K_START, 2, ".quad"}, {LANG_ASM, K_START, 2, ".byte"},
    {LANG_ASM, K_START, 2, ".p2align"}, {LANG_ASM, K_START, 2, ".data"}, {LANG_ASM, K_START, 2, ".file"}, {LANG_ASM, K_START, 2, ".word"},
    {LANG_ASM, K_START, 3, "TEXT "}, {LANG_ASM, K_SUB, 3, "(SB)"}, {LANG_ASM, K_SUB, 3, "(FP)"}, {LANG_ASM, K_SUB, 2, "(%r"},
    {LANG_ASM, K_SUB, 2, ", $"}, {LANG_ASM, K_SUB, 1, ",%"}, {LANG_ASM, K_START, 2, "ENTRY("}, {LANG_ASM, K_START, 2, "END("},
    /* ---- Lua ---- */
    {LANG_LUA, K_START, 4, "local "}, {LANG_LUA, K_SUB, 3, "local function"}, {LANG_LUA, K_WORD, 2, "function"}, {LANG_LUA, K_START, 2, "end"},
    {LANG_LUA, K_WORD, 2, "then"}, {LANG_LUA, K_WORD, 3, "elseif"}, {LANG_LUA, K_WORD, 2, "nil"}, {LANG_LUA, K_START, 2, "--"},
    {LANG_LUA, K_SUB, 3, "~="}, {LANG_LUA, K_SUB, 3, "require(\""}, {LANG_LUA, K_SUB, 3, "require \""}, {LANG_LUA, K_SUB, 2, " .. "},
    {LANG_LUA, K_WORD, 2, "pairs"}, {LANG_LUA, K_WORD, 2, "ipairs"}, {LANG_LUA, K_START, 2, "return "},
    /* ---- Makefile ---- */
    {LANG_MAKE, K_SUB, 4, "$(CC)"}, {LANG_MAKE, K_SUB, 3, "$(CFLAGS)"}, {LANG_MAKE, K_START, 4, ".PHONY"}, {LANG_MAKE, K_SUB, 3, "$@"},
    {LANG_MAKE, K_SUB, 3, "$<"}, {LANG_MAKE, K_SUB, 3, "$^"}, {LANG_MAKE, K_START, 2, "all:"}, {LANG_MAKE, K_START, 2, "clean:"},
    {LANG_MAKE, K_SUB, 3, " := "}, {LANG_MAKE, K_SUB, 2, " ?= "}, {LANG_MAKE, K_SUB, 2, "$("}, {LANG_MAKE, K_START, 2, "include "},
    {LANG_MAKE, K_START, 2, "ifeq"}, {LANG_MAKE, K_START, 2, "endif"}, {LANG_MAKE, K_SUB, 3, "@echo"}, {LANG_MAKE, K_SUB, 3, "$(MAKE)"},
    {LANG_MAKE, K_SUB, 3, " += "}, {LANG_MAKE, K_START, 2, "-include"}, {LANG_MAKE, K_SUB, 3, "%.o"}, {LANG_MAKE, K_SUB, 2, "$(RM)"},
    {LANG_MAKE, K_START, 2, "ifdef"}, {LANG_MAKE, K_START, 2, "ifneq"}, {LANG_MAKE, K_START, 2, "install:"},
    /* ---- the C family's comment styles (Python, Ruby, Shell... never use these) ---- */
    {LANG_C, K_START, 2, "//"}, {LANG_CPP, K_START, 2, "//"}, {LANG_CSHARP, K_START, 2, "//"}, {LANG_JAVA, K_START, 2, "//"},
    {LANG_JS, K_START, 2, "//"}, {LANG_TS, K_START, 2, "//"}, {LANG_RUST, K_START, 2, "//"}, {LANG_GO, K_START, 2, "//"},
    {LANG_KOTLIN, K_START, 2, "//"}, {LANG_SWIFT, K_START, 2, "//"}, {LANG_PHP, K_START, 1, "//"},
    {LANG_C, K_START, 2, "/*"}, {LANG_CPP, K_START, 2, "/*"}, {LANG_JAVA, K_START, 2, "/*"}, {LANG_JS, K_START, 1, "/*"},
    {LANG_CSS, K_START, 2, "/*"}, {LANG_CSHARP, K_START, 1, "/*"}, {LANG_GO, K_START, 1, "/*"},
    /* ---- Markdown ---- */
    {LANG_MARKDOWN, K_START, 3, "# "}, {LANG_MARKDOWN, K_START, 3, "## "}, {LANG_MARKDOWN, K_START, 3, "### "}, {LANG_MARKDOWN, K_START, 2, "- "},
    {LANG_MARKDOWN, K_START, 1, "* "}, {LANG_MARKDOWN, K_START, 3, "```"}, {LANG_MARKDOWN, K_SUB, 3, "]("}, {LANG_MARKDOWN, K_SUB, 2, "**"},
    {LANG_MARKDOWN, K_START, 2, "> "}, {LANG_MARKDOWN, K_START, 2, "1. "}, {LANG_MARKDOWN, K_SUB, 2, "`"}, {LANG_MARKDOWN, K_START, 2, "| "},
    /* ---- JSON ---- */
    {LANG_JSON, K_SUB, 3, "\": "}, {LANG_JSON, K_SUB, 2, "\":"}, {LANG_JSON, K_END, 2, "\","}, {LANG_JSON, K_END, 2, "},"},
    {LANG_JSON, K_END, 1, "["}, {LANG_JSON, K_END, 1, "{"}, {LANG_JSON, K_WORD, 1, "true"}, {LANG_JSON, K_WORD, 1, "false"},
    /* ---- YAML ---- */
    {LANG_YAML, K_START, 3, "---"}, {LANG_YAML, K_START, 2, "- name:"}, {LANG_YAML, K_END, 2, ":"}, {LANG_YAML, K_SUB, 2, ": "},
    {LANG_YAML, K_START, 1, "- "}, {LANG_YAML, K_START, 1, "#"},
};
#define CLUE_COUNT ((int)(sizeof CLUES / sizeof CLUES[0]))

/* File endings, each worth a few points to its language. */
static const struct { const char *ext; int lang; } EXTS[] = {
    {"c", LANG_C}, {"h", LANG_C}, {"cpp", LANG_CPP}, {"cc", LANG_CPP}, {"cxx", LANG_CPP}, {"hpp", LANG_CPP}, {"hh", LANG_CPP},
    {"cs", LANG_CSHARP}, {"java", LANG_JAVA}, {"py", LANG_PYTHON}, {"pyw", LANG_PYTHON}, {"js", LANG_JS}, {"mjs", LANG_JS},
    {"cjs", LANG_JS}, {"jsx", LANG_JS}, {"ts", LANG_TS}, {"tsx", LANG_TS}, {"mts", LANG_TS}, {"rs", LANG_RUST}, {"go", LANG_GO},
    {"html", LANG_HTML}, {"htm", LANG_HTML}, {"css", LANG_CSS}, {"sql", LANG_SQL}, {"sh", LANG_SHELL}, {"bash", LANG_SHELL},
    {"php", LANG_PHP}, {"rb", LANG_RUBY}, {"kt", LANG_KOTLIN}, {"kts", LANG_KOTLIN}, {"swift", LANG_SWIFT}, {"s", LANG_ASM},
    {"asm", LANG_ASM}, {"x68", LANG_ASM}, {"lua", LANG_LUA}, {"mk", LANG_MAKE}, {"md", LANG_MARKDOWN}, {"markdown", LANG_MARKDOWN},
    {"json", LANG_JSON}, {"yml", LANG_YAML}, {"yaml", LANG_YAML}, {"txt", LANG_TEXT},
};

#define LANG_SCAN (48 * 1024)   /* only the start of a big file is read: plenty to tell */

static bool WordChar(char c) { return isalnum((unsigned char)c) || c == '_'; }

/* Is this text anywhere in the first n bytes? */
static bool Contains(const char *d, int n, const char *text)
{
    int tl = (int)strlen(text);
    for (int i = 0; i + tl <= n; i++) if (d[i] == text[0] && memcmp(d + i, text, (size_t)tl) == 0) return true;
    return false;
}

/* Same letters, ignoring capitals (strncasecmp isn't on every compiler). */
static bool SameCI(const char *a, const char *b, int n)
{
    for (int i = 0; i < n; i++) if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) return false;
    return true;
}

/* Points for each language, from the content alone. */
static void ContentScores(const char *d, int len, unsigned short out[LANG_COUNT])
{
    int n = len < LANG_SCAN ? len : LANG_SCAN;

    /* Markdown code blocks (between ``` lines) are examples quoted in the
       document, not what the file itself is written in: blank them out,
       keeping the ``` lines themselves as a clue that this is Markdown. */
    char *clean = NULL;
    bool hasFence = false;
    for (int i = 0; i + 2 < n && !hasFence; i++) if (d[i] == '`' && d[i + 1] == '`' && d[i + 2] == '`') hasFence = true;
    if (hasFence && (clean = malloc((size_t)n + 1)) != NULL) {
        bool inFence = false;
        int o = 0;
        for (int p0 = 0; p0 < n; ) {
            int e = p0;
            while (e < n && d[e] != '\n') e++;
            int s = p0;
            while (s < e && (d[s] == ' ' || d[s] == '\t')) s++;
            bool fence = e - s >= 3 && ((d[s] == '`' && d[s + 1] == '`' && d[s + 2] == '`') || (d[s] == '~' && d[s + 1] == '~' && d[s + 2] == '~'));
            if (fence) inFence = !inFence;
            if (fence || !inFence) { memcpy(clean + o, d + p0, (size_t)(e - p0)); o += e - p0; }
            if (e < n) clean[o++] = '\n';
            p0 = e + 1;
        }
        clean[o] = 0;
        d = clean;
        n = o;
    }
    int hits[sizeof CLUES / sizeof CLUES[0]];
    memset(hits, 0, sizeof hits);
    #define HIT(i) do { if (hits[i] < 4) hits[i]++; } while (0)

    /* line by line. Lines that are C-style comments (starting with // or
       slash-star) only count as "this language uses these comments": their
       words are English, not code, so they're skipped for everything else. */
    int p = 0, lineNo = 0, yamlKeys = 0, makeVars = 0, tabLines = 0;
    bool inBlock = false;
    while (p < n) {
        int e = p;
        while (e < n && d[e] != '\n') e++;
        int s = p;
        while (s < e && (d[s] == ' ' || d[s] == '\t')) s++;
        int t = e;
        while (t > s && (d[t - 1] == ' ' || d[t - 1] == '\t' || d[t - 1] == '\r')) t--;
        bool comment = false, opened = false;
        if (inBlock) {
            comment = true;
            for (int k = s; k + 1 < t; k++) if (d[k] == '*' && d[k + 1] == '/') { inBlock = false; break; }
        } else if (t - s >= 2 && d[s] == '/' && (d[s + 1] == '/' || d[s + 1] == '*')) {
            comment = true;
            if (d[s + 1] == '*') {
                opened = true;
                for (int k = s + 2; k + 1 < t; k++) if (d[k] == '*' && d[k + 1] == '/') { opened = false; break; }
            }
        }
        if (p < n && d[p] == '\t' && t > s) tabLines++;

        for (int i = 0; i < CLUE_COUNT; i++) {
            const Clue *c = &CLUES[i];
            if (comment && !(c->kind == K_START && c->text[0] == '/')) continue;
            int cl;
            switch (c->kind) {
            case K_START: case K_STARTI:
                cl = (int)strlen(c->text);
                if (t - s < cl) break;
                if (c->kind == K_START ? memcmp(d + s, c->text, (size_t)cl) == 0 : SameCI(d + s, c->text, cl)) {
                    /* for word-like clues ("fi", "end", "mov") the next character mustn't continue the word */
                    char last = c->text[cl - 1], next = s + cl < t ? d[s + cl] : ' ';
                    if (!(WordChar(last) && WordChar(next))) HIT(i);
                }
                break;
            case K_END:
                cl = (int)strlen(c->text);
                if (t - s >= cl && memcmp(d + t - cl, c->text, (size_t)cl) == 0) HIT(i);
                break;
            case K_SHEBANG:
                if (lineNo == 0 && e - p > 2 && d[p] == '#' && d[p + 1] == '!') {
                    char line[160];
                    snprintf(line, sizeof line, "%.*s", e - p, d + p);
                    if (strstr(line, c->text)) HIT(i);
                }
                break;
            }
        }
        if (opened) inBlock = true;

        if (!comment) {
            /* whole words on this line */
            for (int i = s; i < t; ) {
                if (!WordChar(d[i]) || (i > s && WordChar(d[i - 1]))) { i++; continue; }
                int j = i;
                while (j < t && WordChar(d[j])) j++;
                int wl = j - i;
                for (int k = 0; k < CLUE_COUNT; k++) {
                    const Clue *c = &CLUES[k];
                    if ((c->kind != K_WORD && c->kind != K_WORDI) || hits[k] >= 4) continue;
                    if ((int)strlen(c->text) != wl) continue;
                    if (c->kind == K_WORD ? memcmp(d + i, c->text, (size_t)wl) == 0 : SameCI(d + i, c->text, wl)) HIT(k);
                }
                i = j;
            }
            /* YAML: "key: value" or "key:" lines (also "- key: value") */
            int k = s;
            if (k + 1 < t && d[k] == '-' && d[k + 1] == ' ') k += 2;
            int ks = k;
            while (k < t && (WordChar(d[k]) || d[k] == '-' || d[k] == '.')) k++;
            if (k > ks && k < t && d[k] == ':' && (k + 1 == t || d[k + 1] == ' ') && isalpha((unsigned char)d[ks])) {
                char last = d[t - 1];
                bool pyWord = (k - ks == 4 && memcmp(d + ks, "else", 4) == 0) || (k - ks == 3 && memcmp(d + ks, "try", 3) == 0);
                if (!pyWord && last != '{' && last != ';' && last != ',' && last != ')') yamlKeys++;
            }
            /* Make: $(NAME) with a capital-letter name */
            for (int q = s; q + 3 < t; q++)
                if (d[q] == '$' && d[q + 1] == '(' && isupper((unsigned char)d[q + 2])) {
                    int z = q + 2;
                    while (z < t && (isupper((unsigned char)d[z]) || d[z] == '_' || isdigit((unsigned char)d[z]))) z++;
                    if (z < t && d[z] == ')') makeVars++;
                }
        }
        p = e + 1;
        lineNo++;
    }

    /* text anywhere (memmem isn't everywhere, so a simple search) */
    for (int k = 0; k < CLUE_COUNT; k++) {
        const Clue *c = &CLUES[k];
        if (c->kind != K_SUB) continue;
        int cl = (int)strlen(c->text);
        for (int i = 0; i + cl <= n && hits[k] < 4; i++)
            if (d[i] == c->text[0] && memcmp(d + i, c->text, (size_t)cl) == 0) { HIT(k); i += cl - 1; }
    }
    #undef HIT

    int total[LANG_COUNT] = {0};
    for (int k = 0; k < CLUE_COUNT; k++) total[CLUES[k].lang] += hits[k] * CLUES[k].weight;

    /* the special counters */
    total[LANG_YAML] += 2 * (yamlKeys < 6 ? yamlKeys : 6);
    total[LANG_MAKE] += 3 * (makeVars < 5 ? makeVars : 5) + (tabLines < 4 ? tabLines : 4);
    /* a few markers that settle it */
    if (Contains(d, n, "<!DOCTYPE html") || Contains(d, n, "<!doctype html") || Contains(d, n, "<html")) total[LANG_HTML] += 20;
    for (int i = 0; i + 5 <= n; i++)                          /* <?php starting a line (not just mentioned) */
        if ((i == 0 || d[i - 1] == '\n') && memcmp(d + i, "<?php", 5) == 0) { total[LANG_PHP] += 40; break; }

    /* JSON has to start like JSON: { then a "name" (or }), or [ then a value */
    int q = 0;
    while (q < n && isspace((unsigned char)d[q])) q++;
    bool jsonStart = false;
    if (q < n && (d[q] == '{' || d[q] == '[')) {
        int r2 = q + 1;
        while (r2 < n && isspace((unsigned char)d[r2])) r2++;
        char c = r2 < n ? d[r2] : 0;
        jsonStart = d[q] == '{' ? (c == '"' || c == '}') : (c == '{' || c == '[' || c == '"' || c == ']' || c == '-' || isdigit((unsigned char)c));
    }
    if (!jsonStart) total[LANG_JSON] = 0;
    else total[LANG_JSON] += 10;

    for (int l = 0; l < LANG_COUNT; l++) out[l] = (unsigned short)(total[l] > 65000 ? 65000 : total[l]);
    free(clean);
}

static int ExtLang(const char *name)
{
    const char *slash = strrchr(name, '/');                  /* just the file's own name, not its folders */
    if (slash) name = slash + 1;
    if (EqualsCI(name, "Makefile") || EqualsCI(name, "GNUmakefile")) return LANG_MAKE;
    const char *dot = strrchr(name, '.');
    if (!dot) return -1;
    for (int i = 0; i < (int)(sizeof EXTS / sizeof EXTS[0]); i++) if (EqualsCI(dot + 1, EXTS[i].ext)) return EXTS[i].lang;
    return -1;
}

/* Picks the language from the content's points plus the file ending. */
static int PickLang(const unsigned short sc[LANG_COUNT], const char *name)
{
    int total[LANG_COUNT];
    for (int l = 0; l < LANG_COUNT; l++) total[l] = sc[l];
    int ext = ExtLang(name);
    if (ext >= 0) total[ext] += 8;                            /* the ending counts, but content can outvote it */
    const char *base = strrchr(name, '/');
    base = base ? base + 1 : name;
    if (ext == LANG_MAKE && !strchr(base, '.')) total[ext] += 8;   /* a file called "Makefile" really is one */
    if (ext == LANG_C) total[LANG_CPP] += 4;                  /* a .h file could be either */
    if (ext == LANG_TEXT) return LANG_TEXT;                   /* .txt is always plain text */
    int best = -1;
    for (int l = 0; l < LANG_COUNT; l++) if (best < 0 || total[l] > total[best]) best = l;
    /* C++ and TypeScript are "C plus extra" and "JavaScript plus types". C-style code in a
       .cpp file is C++ (that's how it gets compiled), and plain JavaScript in a .ts file is TypeScript */
    if (best == LANG_C && ext == LANG_CPP) best = LANG_CPP;
    if (best == LANG_JS && ext == LANG_TS) best = LANG_TS;
    /* otherwise only call it C++ if it clearly has C++-only things */
    if (best == LANG_CPP && total[LANG_C] >= total[LANG_CPP] - 3 && ext != LANG_CPP) best = LANG_C;
    /* TypeScript is "JavaScript plus types": same idea */
    if (best == LANG_TS && total[LANG_JS] >= total[LANG_TS] - 3 && ext != LANG_TS) best = LANG_JS;
    if (total[best] < 8) return LANG_TEXT;                    /* not enough clues for anything */
    return best;
}

/* Remembers a file's points so they're only worked out once. */
struct LangCache { unsigned short sc[LANG_COUNT]; int lines; bool done; bool binary; };

static void FillLangCache(LangCache *c, const char *data, int len, bool binary)
{
    c->binary = binary;
    c->lines = 0;
    if (!binary) {
        ContentScores(data, len, c->sc);
        c->lines = len ? 1 : 0;
        for (int i = 0; i < len; i++) if (data[i] == '\n' && i + 1 < len) c->lines++;
    }
    c->done = true;
}
static int LangOf(LangCache *c, const char *name) { return c->binary ? LANG_BINARY : PickLang(c->sc, name); }


static void SetTip(const char *a, const char *b, const char *c);

/* ---------- remembering each file's language ----------
   Working it out means reading the file, so it's done once and kept:
   for committed versions (blobs never change) until the program closes,
   and for files you're editing until you next change them. */
static LangCache *BlobLangCache(Repo *r, int b)
{
    Blob *bl = &r->blobs[b];
    if (!bl->lang) {
        bl->lang = calloc(1, sizeof(LangCache));
        if (!bl->lang) return NULL;
        FillLangCache(bl->lang, bl->data, bl->len, bl->binary);
    }
    return bl->lang;
}
static LangCache *WorkLangCache(WorkFile *f)
{
    if (!f->lang) { f->lang = calloc(1, sizeof(LangCache)); if (!f->lang) return NULL; f->langVer = f->ver - 1; }
    if (f->langVer != f->ver) { FillLangCache(f->lang, f->text, f->len, f->binary); f->langVer = f->ver; }
    return f->lang;
}

/* The language of a file as you see it, and how many lines it has. */
static int ViewLang(Repo *r, ViewFile *v, int *lines)
{
    LangCache *c = v->mine ? WorkLangCache(v->mine) : v->blob >= 0 ? BlobLangCache(r, v->blob) : NULL;
    if (lines) *lines = c ? c->lines : 0;
    return c ? LangOf(c, v->name) : LANG_TEXT;
}

static const char *LangLabel(int lang) { return lang == LANG_BINARY ? "Binary" : LANG_NAMES[lang]; }

/* ---------- colours ----------
   Each language has its own fixed colour from the same colour-blind-checked
   set as the contributions pie, so Python looks the same in every repository.
   There are only 8 colours for 20 languages, so if two languages in one
   repository share a colour, the second takes the next free one. */
static const Color LANG_PALETTE[8] = {
    {0x39, 0x87, 0xe5, 255}, {0xd9, 0x59, 0x26, 255}, {0x19, 0x9e, 0x70, 255}, {0xc9, 0x85, 0x00, 255},
    {0xd5, 0x51, 0x81, 255}, {0x00, 0x83, 0x00, 255}, {0x90, 0x85, 0xe9, 255}, {0xe6, 0x67, 0x67, 255}
};
static const Color LANG_OTHER = {0x6e, 0x76, 0x81, 255};
static const unsigned char LANG_SLOT[LANG_FIRST_NONCODE] = {
    /* C */ 2, /* C++ */ 4, /* C# */ 5, /* Java */ 1, /* Python */ 0, /* JavaScript */ 3, /* TypeScript */ 0,
    /* Rust */ 1, /* Go */ 2, /* HTML */ 1, /* CSS */ 6, /* SQL */ 7, /* Shell */ 5, /* PHP */ 6, /* Ruby */ 7,
    /* Kotlin */ 6, /* Swift */ 1, /* Assembly */ 7, /* Lua */ 0, /* Makefile */ 3
};

/* ---------- the breakdown ---------- */
typedef struct { int lang; int lines; int files; Color color; } LangPart;   /* lang -1 = "Other" */
static LangPart langParts[6];
static int      langPartCount, langTotal;
static char     langSkipped[TEXT_LEN];       /* the kinds of file that aren't counted, e.g. "Markdown, JSON" */

/* Counts the lines of code in each language: the latest commit, plus your
   own changes if uid can edit. Text that isn't code (Markdown, JSON, YAML,
   plain text) and binary files aren't counted - the same as GitHub. */
static void BuildLanguages(Repo *r, int uid)
{
    int lines[LANG_COUNT] = {0}, files[LANG_COUNT] = {0};
    bool skipped[LANG_COUNT] = {0}, binary = false;
    BuildView(r, uid);
    for (int i = 0; i < viewCount; i++) {
        if (ViewDeleted(&view[i])) continue;
        int n = 0, l = ViewLang(r, &view[i], &n);
        if (l == LANG_BINARY) { binary = true; continue; }
        if (l >= LANG_FIRST_NONCODE) { skipped[l] = true; continue; }
        lines[l] += n;
        files[l]++;
    }
    /* biggest first */
    int order[LANG_FIRST_NONCODE], n = 0;
    for (int l = 0; l < LANG_FIRST_NONCODE; l++) if (files[l]) order[n++] = l;
    for (int a = 0; a < n; a++)
        for (int b = a + 1; b < n; b++)
            if (lines[order[b]] > lines[order[a]]) { int t = order[a]; order[a] = order[b]; order[b] = t; }

    langPartCount = langTotal = 0;
    bool used[8] = {0};
    for (int i = 0; i < n; i++) {
        int l = order[i];
        langTotal += lines[l];
        if (n > 6 && i >= 5) {                    /* more than 6 languages: the smallest share "Other" */
            if (langPartCount == 5) langParts[langPartCount++] = (LangPart){-1, 0, 0, LANG_OTHER};
            langParts[5].lines += lines[l];
            langParts[5].files += files[l];
            continue;
        }
        int s = LANG_SLOT[l];
        for (int k = 0; k < 8 && used[s]; k++) s = (s + 1) % 8;
        used[s] = true;
        langParts[langPartCount++] = (LangPart){l, lines[l], files[l], LANG_PALETTE[s]};
    }

    langSkipped[0] = 0;
    for (int l = LANG_FIRST_NONCODE; l < LANG_COUNT; l++)
        if (skipped[l]) snprintf(langSkipped + strlen(langSkipped), sizeof langSkipped - strlen(langSkipped), "%s%s", langSkipped[0] ? ", " : "", LANG_NAMES[l]);
    if (binary) snprintf(langSkipped + strlen(langSkipped), sizeof langSkipped - strlen(langSkipped), "%s%s", langSkipped[0] ? ", " : "", "binary files");
}

/* The colour a language has in the breakdown just built (grey if it's in "Other" or not code). */
static Color LangColor(int lang)
{
    for (int i = 0; i < langPartCount; i++) if (langParts[i].lang == lang) return langParts[i].color;
    return LANG_OTHER;
}
static const char *PartName(LangPart *p) { return p->lang >= 0 ? LANG_NAMES[p->lang] : "Other"; }

/* A thin bar split into each language's share, with a key underneath.
   Hovering part of the bar shows its numbers. */
static void DrawLanguageBar(Rectangle area)
{
    if (!langTotal) {
        DrawFit(langSkipped[0] ? TextFormat("Languages: no code yet (%s isn't counted)", langSkipped) : "Languages: no code yet",
                (int)area.x, (int)area.y + 6, FS, (int)area.width, UI_MUTED);
        return;
    }
    Rectangle bar = {area.x, area.y + 2, area.width, 10};
    float x = bar.x;
    int hov = -1;
    for (int i = 0; i < langPartCount; i++) {
        LangPart *p = &langParts[i];
        float w = bar.width * p->lines / langTotal;
        if (i == langPartCount - 1) w = bar.x + bar.width - x;            /* no rounding gap at the end */
        float dw = w - (i < langPartCount - 1 ? 2 : 0);                    /* a 2-pixel gap between parts */
        if (dw < 2) dw = 2;
        Rectangle seg = {x, bar.y, dw, bar.height};
        DrawRectangleRounded(seg, 0.6f, 4, p->color);
        if (CheckCollisionPointRec(GetMousePosition(), (Rectangle){x, bar.y - 6, w, bar.height + 12})) hov = i;
        x += w;
    }
    /* the key: name and share for each, as many as fit */
    float kx = area.x, ky = area.y + 18;
    int shown = 0;
    for (int i = 0; i < langPartCount; i++) {
        LangPart *p = &langParts[i];
        const char *pct = TextFormat(" %.1f%%", 100.0 * p->lines / langTotal);
        int w = 14 + MeasureText(PartName(p), FS) + MeasureText(pct, FS) + 22;
        int moreW = i < langPartCount - 1 ? MeasureText("+9 more", FS) : 0;     /* leave room to say what's left out */
        if (kx + w + moreW > area.x + area.width) break;
        shown++;
        DrawCircle((int)kx + 5, (int)ky + 10, 5, p->color);
        DrawText(PartName(p), (int)kx + 16, (int)ky, FS, UI_TEXT);
        DrawText(pct, (int)kx + 16 + MeasureText(PartName(p), FS), (int)ky, FS, UI_MUTED);
        kx += w;
    }
    if (shown < langPartCount) {
        DrawText(TextFormat("+%d more", langPartCount - shown), (int)kx, (int)ky, FS, UI_MUTED);
        kx += MeasureText("+9 more", FS) + 22;
    } else if (langSkipped[0]) {
        const char *note = TextFormat("|   not counted: %s", langSkipped);
        if (kx + MeasureText(note, FS) <= area.x + area.width) DrawText(note, (int)kx, (int)ky, FS, UI_MUTED);
    }
    if (hov >= 0) {
        LangPart *p = &langParts[hov];
        SetTip(PartName(p), TextFormat("%d line%s  (%.1f%%)", p->lines, p->lines == 1 ? "" : "s", 100.0 * p->lines / langTotal),
               TextFormat("in %d file%s", p->files, p->files == 1 ? "" : "s"));
    }
}

/* ---------------- profile pages ----------------
   Everyone has one. Your own is also your home page: the first thing you
   see after signing in, with your repositories, everyone else's, a search
   box and the New button. Other people's profiles list their repositories. */
static void ScreenProfile(void)
{
    User *u = FindUserById(viewUser);
    if (!u) { GoHome(); return; }
    bool mine = u->id == curUser;
    int theme = UserTheme(u->id);
    Color acc = THEMES[theme].accent;

    Rectangle banner = {0, 57, (float)W, 120};
    DrawRectangleRec(banner, UI_BASE);
    DrawTheme(theme, banner, 1.0f);
    DrawLine(0, 177, W, 177, UI_BORDER);

    /* ---- left column: picture, name, bio, numbers ---- */
    int cx = 96, cy = 177;
    DrawCircle(cx, cy, 80, UI_BASE);
    DrawCircle(cx, cy, 77, acc);
    DrawAvatar(u->id, cx, cy, 72);
    DrawFit(u->name, 20, 264, 30, 290, UI_TEXT);
    DrawText(TextFormat("Joined %s", TimeAgo(u->created)), 20, 298, FS, UI_MUTED);
    int y = 330;
    if (u->bio[0]) y += DrawWrapped(u->bio, 20, y, 290, H >= 760 ? 4 : 3, UI_TEXT) * 24 + 8;   /* shorter on small windows */
    if (mine) {
        if (Button((Rectangle){20, (float)y, 280, 36}, "Edit profile", UI_BTN)) { OpenAccount(); return; }
        y += 48;
    }

    int nRepos = 0, nCommits = 0, nStars = 0, nIssues = 0, nStarred = 0, nOpen = 0;
    for (int i = 0; i < repoCount; i++) {
        Repo *r = &repos[i];
        if (r->owner == u->id) { nRepos++; nStars += r->starCount; nOpen += OpenIssues(r); }
        if (HasStarred(r, u->id)) nStarred++;
        for (int k = 0; k < r->commitCount; k++) if (r->commits[k].author == u->id && !r->commits[k].copied) nCommits++;
        for (int k = 0; k < r->issueCount;  k++) if (r->issues[k].author  == u->id) nIssues++;
    }
    DrawLine(20, y, 300, y, UI_BORDER);
    y += 14;
    DrawText(TextFormat("%d repositor%s",     nRepos,   nRepos == 1 ? "y" : "ies"), 20, y, FS, UI_TEXT); y += 28;
    DrawText(TextFormat("%d commit%s",        nCommits, nCommits == 1 ? "" : "s"),  20, y, FS, UI_TEXT); y += 28;
    DrawText(TextFormat("%d star%s received", nStars,   nStars == 1 ? "" : "s"),    20, y, FS, UI_TEXT); y += 28;
    if (mine) {
        DrawText(TextFormat("%d repositor%s starred", nStarred, nStarred == 1 ? "y" : "ies"), 20, y, FS, UI_TEXT); y += 28;
        DrawFit(TextFormat("%d open issue%s in your repos", nOpen, nOpen == 1 ? "" : "s"), 20, y, FS, 300, UI_TEXT); y += 28;
    } else {
        DrawText(TextFormat("%d issue%s opened", nIssues, nIssues == 1 ? "" : "s"), 20, y, FS, UI_TEXT); y += 28;
    }
    y += 12;
    const char *ppl = TextFormat("%d user%s on this computer", userCount, userCount == 1 ? "" : "s");
    Rectangle pplHit = {20, (float)y - 4, (float)MeasureText(ppl, FS), 28};
    bool pplHot = Hover(pplHit);
    DrawText(ppl, 20, y, FS, pplHot ? UI_LINK : UI_MUTED);
    if (pplHot) { wantCursor = MOUSE_CURSOR_POINTING_HAND; DrawLine(20, y + 22, 20 + (int)pplHit.width, y + 22, UI_LINK); }
    if (Clicked(pplHit)) { OpenPeople(); return; }
    if (y + 54 <= H) DrawText(TextFormat("Theme: %s", THEMES[theme].name), 20, y + 30, FS, acc);   /* only if there's room */

    /* ---- right column: invitations waiting for you ---- */
    float mx = 340, mw = (float)W - 360, oy = 0;
    if (mine) {
        int shownInv = 0, totalInv = InvitationCount(curUser);
        for (int i = 0; i < repoCount && shownInv < 2; i++) {
            Repo *ir = &repos[i];
            if (!IsInvited(ir, curUser)) continue;
            Rectangle b = {mx, 191 + oy, mw, 52};
            DrawRectangleRec(b, (Color){20, 50, 90, 255});
            DrawRectangleLinesEx(b, 1, UI_LINK);
            DrawAvatar(ir->owner, (int)b.x + 28, (int)b.y + 26, 16);
            const char *msg = TextFormat("%s invited you to collaborate on %s", UserName(ir->owner), ir->name);
            DrawFit(msg, (int)b.x + 54, (int)b.y + 15, FS, (int)b.width - 54 - 370, UI_TEXT);
            if (Button((Rectangle){b.x + b.width - 360, b.y + 9, 100, 34}, "View", UI_BTN)) { OpenRepo(i); tab = TAB_PEOPLE; return; }
            if (Button((Rectangle){b.x + b.width - 252, b.y + 9, 120, 34}, "Accept", UI_GREEN)) {
                AcceptInvite(ir, curUser); MarkDirty();
                Toast(TextFormat("You can now edit %s - it's in your list below", ir->name));
                return;
            }
            if (Button((Rectangle){b.x + b.width - 124, b.y + 9, 116, 34}, "Decline", UI_BTN)) {
                RemoveInvite(ir, curUser); MarkDirty();
                Toast("Invitation declined");
                return;
            }
            oy += 60;
            shownInv++;
        }
        if (totalInv > shownInv) {
            DrawText(TextFormat("...and %d more invitation%s", totalInv - shownInv, totalInv - shownInv == 1 ? "" : "s"),
                     (int)mx, (int)(191 + oy), FS, UI_MUTED);
            oy += 30;
        }
    }

    /* ---- right column: contributions ---- */
    long long hoverDay = 0;
    DrawContributions((Rectangle){mx, 191 + oy, mw, 160}, u->id, &hoverDay);
    if (screen != SCR_PROFILE) return;                       /* a day was clicked */

    /* ---- right column: repositories ---- */
    float ty = 367 + oy;
    if (mine) {
        if (ButtonEx((Rectangle){mx, ty, 100, 40}, "Yours", homeView == 0 ? UI_BLUE : UI_BTN, true))       { homeView = 0; scProfile = 0; }
        if (ButtonEx((Rectangle){mx + 106, ty, 120, 40}, "Everyone", homeView == 1 ? UI_BLUE : UI_BTN, true)) { homeView = 1; scProfile = 0; }
        TextFieldUI(&tfSearch, (Rectangle){mx + 240, ty, mw - 240 - 130, 40}, "Find a repository...");
        if (Button((Rectangle){mx + mw - 120, ty, 120, 40}, "New", UI_GREEN)) {
            Unfocus();
            TF_Clear(&tfName);
            TF_Clear(&tfDesc);
            optReadme = true;
            screen = SCR_NEW;
            tfName.active = true;
            return;
        }
    } else {
        const char *title = TextFormat("%s's repositories", u->name);
        float sw = mw * 0.4f;
        DrawFit(title, (int)mx, (int)ty + 10, FS, (int)(mw - sw - 20), UI_TEXT);
        TextFieldUI(&tfSearch, (Rectangle){mx + mw - sw, ty, sw, 40}, "Find a repository...");
    }
    bool everyone = mine && homeView == 1;

    /* which repos to list: filter, then newest activity first */
    int order[MAX_REPOS], n = 0;
    for (int i = 0; i < repoCount; i++) {
        Repo *r = &repos[i];
        if (!everyone && r->owner != u->id && !(mine && IsCollab(r, u->id))) continue;   /* yours: owned + collaborating */
        if (!ContainsCI(r->name, tfSearch.buf) && !ContainsCI(UserName(r->owner), tfSearch.buf)) continue;
        order[n++] = i;
    }
    for (int a = 0; a < n; a++)
        for (int b = a + 1; b < n; b++)
            if (LastActivity(&repos[order[b]]) > LastActivity(&repos[order[a]])) { int t = order[a]; order[a] = order[b]; order[b] = t; }

    Rectangle la = {mx, ty + 54, mw, (float)H - 20 - (ty + 54)};
    float rowH = 100;
    ScrollArea(la, &scProfile, n * rowH - 10);
    if (n == 0) {
        const char *m = tfSearch.len ? "No repositories match your search."
                      : everyone     ? "Nobody has made a repository yet."
                      : mine         ? "You have no repositories yet - press New to make one."
                                     : TextFormat("%s hasn't made any repositories yet.", u->name);
        DrawText(m, (int)la.x, (int)la.y + 6, FS, UI_MUTED);
    }

    BeginScissorMode((int)la.x, (int)la.y, (int)la.width, (int)la.height);
    for (int k = 0; k < n; k++) {
        Repo *r = &repos[order[k]];
        Rectangle card = {la.x, la.y + k * rowH - scProfile, la.width - 12, rowH - 10};
        if (card.y > la.y + la.height || card.y + card.height < la.y) continue;
        bool hot = Hover(la) && Hover(card);
        if (hot) wantCursor = MOUSE_CURSOR_POINTING_HAND;
        DrawRectangleRec(card, hot ? UI_HOVER : UI_PANEL);
        DrawRectangleLinesEx(card, 1, UI_BORDER);

        int tx = (int)card.x + 18;
        if (everyone || r->owner != u->id) {       /* show "owner / name" when it isn't theirs */
            const char *own = TextFormat("%s / ", UserName(r->owner));
            DrawText(own, tx, (int)card.y + 12, FS, UI_LINK);
            tx += MeasureText(own, FS);
        }
        DrawBold(r->name, tx, (int)card.y + 12, FS, UI_LINK);
        tx += MeasureText(r->name, FS) + 16;
        if (IsCollab(r, curUser)) { DrawText("collaborator", tx, (int)card.y + 12, FS, UI_MUTED); tx += MeasureText("collaborator", FS) + 16; }
        if (r->isFork) { DrawText("fork", tx, (int)card.y + 12, FS, UI_MUTED); tx += MeasureText("fork", FS) + 16; }
        if (HasStarred(r, curUser)) DrawText("starred", tx, (int)card.y + 12, FS, UI_YELLOW);

        DrawFit(r->desc[0] ? r->desc : "No description", (int)card.x + 18, (int)card.y + 38, FS, (int)card.width - 36, UI_MUTED);
        int sx = (int)card.x + 18;
        BuildLanguages(r, -1);                       /* main language of the latest commit */
        if (langPartCount && langParts[0].lang >= 0) {
            DrawCircle(sx + 5, (int)card.y + 72, 5, langParts[0].color);
            DrawText(LANG_NAMES[langParts[0].lang], sx + 16, (int)card.y + 62, FS, UI_TEXT);
            sx += 16 + MeasureText(LANG_NAMES[langParts[0].lang], FS) + 24;
        }
        int nf = ForkCount(r);
        DrawFit(TextFormat("%d commit%s   |   %d open issue%s   |   %d star%s   |   %d fork%s   |   updated %s",
                           r->commitCount, r->commitCount == 1 ? "" : "s",
                           OpenIssues(r), OpenIssues(r) == 1 ? "" : "s",
                           r->starCount, r->starCount == 1 ? "" : "s",
                           nf, nf == 1 ? "" : "s",
                           TimeAgo(LastActivity(r))),
                sx, (int)card.y + 62, FS, (int)(card.x + card.width) - 18 - sx, UI_MUTED);
        if (Hover(la) && Clicked(card)) { EndScissorMode(); OpenRepo(order[k]); return; }
    }
    EndScissorMode();
    DrawScrollbar(la, scProfile, n * rowH - 10);

    if (hoverDay) DrawDayTooltip(u->id, hoverDay);          /* last, so it sits on top of everything */
}

/* ---------------- everyone on this computer ---------------- */
static void ScreenPeople(void)
{
    DrawBold("People on this computer", 20, 80, 30, UI_TEXT);
    DrawText("Click someone to see their profile.", 20, 118, FS, UI_MUTED);
    if (Button((Rectangle){(float)W - 160, 80, 140, 40}, "Back", UI_BTN)) { GoHome(); return; }

    Rectangle la = {20, 160, (float)W - 40, (float)H - 180};
    float rowH = 104;
    ScrollArea(la, &scPeople, userCount * rowH - 10);
    BeginScissorMode((int)la.x, (int)la.y, (int)la.width, (int)la.height);
    for (int i = 0; i < userCount; i++) {
        User *u = &users[i];
        Rectangle card = {la.x, la.y + i * rowH - scPeople, la.width - 12, rowH - 10};
        if (card.y > la.y + la.height || card.y + card.height < la.y) continue;
        bool hot = Hover(la) && Hover(card);
        if (hot) wantCursor = MOUSE_CURSOR_POINTING_HAND;
        DrawRectangleRec(card, hot ? UI_HOVER : UI_PANEL);

        /* a strip of their theme along the right-hand side */
        float sw = card.width * 0.3f;
        Rectangle strip = {card.x + card.width - sw, card.y, sw, card.height};
        Rectangle vis = Intersect(strip, la);
        if (vis.height > 0) {
            DrawRectangleRec(vis, UI_BASE);
            DrawThemeClip(UserTheme(u->id), strip, vis, 1.0f);   /* this ends the list's clipping... */
            BeginScissorMode((int)la.x, (int)la.y, (int)la.width, (int)la.height);   /* ...so turn it back on */
        }
        DrawRectangleLinesEx(card, 1, UI_BORDER);

        DrawAvatar(u->id, (int)card.x + 50, (int)(card.y + card.height / 2), 32);
        int tx = (int)card.x + 100, tw = (int)(card.width - sw) - 120;
        DrawBold(u->name, tx, (int)card.y + 12, FS, UI_LINK);
        if (u->id == curUser) DrawText("(you)", tx + MeasureText(u->name, FS) + 12, (int)card.y + 12, FS, UI_MUTED);
        DrawFit(u->bio[0] ? u->bio : "No bio yet", tx, (int)card.y + 38, FS, tw, u->bio[0] ? UI_TEXT : UI_MUTED);
        int owned = ReposOwnedBy(u->id);
        DrawFit(TextFormat("%d repositor%s   |   joined %s   |   theme: %s", owned, owned == 1 ? "y" : "ies",
                           TimeAgo(u->created), THEMES[UserTheme(u->id)].name),
                tx, (int)card.y + 64, FS, tw, UI_MUTED);
        if (Hover(la) && Clicked(card)) { int id = u->id; EndScissorMode(); OpenProfile(id); return; }
    }
    EndScissorMode();
    DrawScrollbar(la, scPeople, userCount * rowH - 10);
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
        DraftSetText(r, curUser, "README.md", TextFormat("# %s\n\n%s\n", nm, tfDesc.buf[0] ? tfDesc.buf : "A new project."));
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

/* ------------------------------------------------------------------ */
/* Downloading                                                        */
/*                                                                    */
/* Files are saved into your normal Downloads folder (or, if there    */
/* isn't one, a "downloads" folder next to the program). A whole      */
/* repository is saved as one .zip file that any computer can open.   */
/* ------------------------------------------------------------------ */
#ifdef _WIN32
#define PATH_SEP '\\'
#else
#define PATH_SEP '/'
#endif

/* One file to download: its name and its bytes. */
typedef struct { const char *name; const char *data; int len; } OutFile;

static bool usingFallbackFolder;          /* true when there was no Downloads folder */

static const char *DownloadFolder(void)
{
    static char dir[1024];
    const char *home = getenv("USERPROFILE");          /* Windows */
    if (!home || !*home) home = getenv("HOME");        /* Linux and macOS */
    if (home && *home) {
        snprintf(dir, sizeof dir, "%s%cDownloads", home, PATH_SEP);
        if (DirectoryExists(dir)) { usingFallbackFolder = false; return dir; }
    }
    snprintf(dir, sizeof dir, "downloads");
    if (!DirectoryExists(dir)) MakeDirectory(dir);
    usingFallbackFolder = true;
    return dir;
}

/* "notes.txt", or "notes (1).txt", "notes (2).txt"... if that's taken - like a web browser. */
static const char *FreePath(const char *dir, const char *fileName)
{
    static char path[1200];
    char stem[NAME_LEN * 2], ext[NAME_LEN];
    snprintf(stem, sizeof stem, "%s", fileName);
    ext[0] = 0;
    char *dot = strrchr(stem, '.');
    if (dot && dot != stem) { snprintf(ext, sizeof ext, "%s", dot); *dot = 0; }
    snprintf(path, sizeof path, "%s%c%s%s", dir, PATH_SEP, stem, ext);
    for (int i = 1; FileExists(path) && i < 1000; i++)
        snprintf(path, sizeof path, "%s%c%s (%d)%s", dir, PATH_SEP, stem, i, ext);
    return path;
}

/* Tells you where a download went. */
static void SavedToast(const char *path)
{
    const char *name = GetFileName(path);
    if (usingFallbackFolder) Toast(TextFormat("Saved %.60s in the \"downloads\" folder next to Mini GitHub", name));
    else                     Toast(TextFormat("Saved %.60s to your Downloads folder", name));
}

/* Saves one file's bytes, exactly as they are. */
static void DownloadFile(const char *name, const char *data, int len)
{
    const char *slash = strrchr(name, '/');                 /* "src/main.c" is saved as main.c */
    const char *path = FreePath(DownloadFolder(), slash ? slash + 1 : name);
    FILE *f = fopen(path, "wb");
    if (!f) { Toast("Couldn't save the file - is the Downloads folder read-only?"); return; }
    bool ok = fwrite(data, 1, (size_t)len, f) == (size_t)len;
    ok = fclose(f) == 0 && ok;
    if (!ok) { remove(path); Toast("Couldn't save the file - is the disk full?"); return; }
    SavedToast(path);
}

/* ---------- .zip files ----------
   A zip is a list of files, each with a small header saying its name, size
   and a checksum (CRC-32) so damage can be spotted, followed by a table of
   contents at the end. Each file is squeezed with DEFLATE (the standard zip
   compression, which raylib can do); if that doesn't make it smaller, it's
   stored as it is. */
static uint32_t Crc32(const unsigned char *d, int n)
{
    static uint32_t table[256];
    static bool ready;
    if (!ready) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        ready = true;
    }
    uint32_t c = 0xFFFFFFFFu;
    for (int i = 0; i < n; i++) c = table[(c ^ d[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

static void Put16(FILE *f, unsigned v) { fputc((int)(v & 0xFF), f); fputc((int)((v >> 8) & 0xFF), f); }
static void Put32(FILE *f, uint32_t v) { Put16(f, v & 0xFFFF); Put16(f, v >> 16); }

/* Writes files into a .zip, all inside one folder called folder/.
   Returns NULL if it worked, or a message saying why not. */
static const char *WriteZip(const char *path, const char *folder, OutFile *files, int n)
{
    FILE *f = fopen(path, "wb");
    if (!f) return "Couldn't save the zip - is the Downloads folder read-only?";

    /* zip files store dates the old MS-DOS way: packed into two 16-bit numbers */
    time_t now = time(NULL);
    struct tm *lt = localtime(&now);
    unsigned dosTime = (unsigned)(lt->tm_hour << 11 | lt->tm_min << 5 | lt->tm_sec / 2);
    unsigned dosDate = (unsigned)((lt->tm_year - 80) << 9 | (lt->tm_mon + 1) << 5 | lt->tm_mday);

    typedef struct { uint32_t crc, csize, offset; int method; char name[PATH_LEN + NAME_LEN + 24]; int len; } ZipEnt;
    ZipEnt *ent = malloc((size_t)(n + 1) * sizeof(ZipEnt));
    if (!ent) { fclose(f); remove(path); return "Out of memory"; }
    int count = 0;
    bool ok = true;

    /* the folder itself comes first, so it opens as a neat folder */
    snprintf(ent[count].name, sizeof ent[count].name, "%s/", folder);
    ent[count].crc = 0; ent[count].csize = 0; ent[count].len = 0; ent[count].method = 0;
    ent[count].offset = (uint32_t)ftell(f);
    count++;

    for (int i = 0; i <= n && ok; i++) {
        bool isFolder = i == 0;
        const unsigned char *data = isFolder ? NULL : (const unsigned char *)files[i - 1].data;
        int len = isFolder ? 0 : files[i - 1].len;
        int e = isFolder ? 0 : count;
        if (!isFolder) {
            snprintf(ent[e].name, sizeof ent[e].name, "%s/%s", folder, files[i - 1].name);
            ent[e].offset = (uint32_t)ftell(f);
            ent[e].len = len;
            ent[e].crc = Crc32(data, len);
        }
        /* try to squeeze it */
        Buf pk = {0};
        bool usePacked = len > 64 && RawDeflate(data, (size_t)len, &pk, (size_t)len - 8);
        const unsigned char *packed = pk.d;
        int packedLen = (int)pk.n;
        ent[e].method = usePacked ? 8 : 0;
        ent[e].csize = (uint32_t)(usePacked ? packedLen : len);

        int nameLen = (int)strlen(ent[e].name);
        Put32(f, 0x04034b50);                 /* "local file header" marker */
        Put16(f, 20); Put16(f, 0);            /* zip version needed, flags  */
        Put16(f, (unsigned)ent[e].method);
        Put16(f, dosTime); Put16(f, dosDate);
        Put32(f, ent[e].crc); Put32(f, ent[e].csize); Put32(f, (uint32_t)ent[e].len);
        Put16(f, (unsigned)nameLen); Put16(f, 0);
        fwrite(ent[e].name, 1, (size_t)nameLen, f);
        if (ent[e].csize) ok = fwrite(usePacked ? packed : data, 1, ent[e].csize, f) == ent[e].csize;
        BufFree(&pk);
        if (!isFolder) count++;
    }

    /* the table of contents */
    uint32_t dirStart = (uint32_t)ftell(f);
    for (int e = 0; e < count; e++) {
        int nameLen = (int)strlen(ent[e].name);
        bool isFolder = ent[e].name[nameLen - 1] == '/';
        Put32(f, 0x02014b50);                 /* "central directory" marker */
        Put16(f, 20); Put16(f, 20); Put16(f, 0);
        Put16(f, (unsigned)ent[e].method);
        Put16(f, dosTime); Put16(f, dosDate);
        Put32(f, ent[e].crc); Put32(f, ent[e].csize); Put32(f, (uint32_t)ent[e].len);
        Put16(f, (unsigned)nameLen); Put16(f, 0); Put16(f, 0);
        Put16(f, 0); Put16(f, 0);
        Put32(f, isFolder ? 0x10 : 0);        /* "this is a folder" flag */
        Put32(f, ent[e].offset);
        fwrite(ent[e].name, 1, (size_t)nameLen, f);
    }
    uint32_t dirSize = (uint32_t)ftell(f) - dirStart;
    Put32(f, 0x06054b50);                     /* "end of central directory" marker */
    Put16(f, 0); Put16(f, 0);
    Put16(f, (unsigned)count); Put16(f, (unsigned)count);
    Put32(f, dirSize); Put32(f, dirStart);
    Put16(f, 0);
    free(ent);
    ok = !ferror(f) && ok;
    ok = fclose(f) == 0 && ok;
    if (!ok) { remove(path); return "Couldn't save the zip - is the disk full?"; }
    return NULL;
}

static void DownloadZip(const char *zipName, const char *folder, OutFile *files, int n)
{
    const char *path = FreePath(DownloadFolder(), zipName);
    char keep[1200];
    snprintf(keep, sizeof keep, "%s", path);
    const char *err = WriteZip(keep, folder, files, n);
    if (err) Toast(err);
    else SavedToast(keep);
}

/* The whole repository as you see it: the latest commit, plus your own
   uncommitted changes if you can edit it. */
static void DownloadRepo(Repo *r)
{
    static OutFile out[MAX_FILES + MAX_DRAFT];
    BuildView(r, CanEdit(r) ? curUser : -1);
    int n = 0;
    bool drafts = false;
    for (int i = 0; i < viewCount; i++) {
        if (view[i].status) drafts = true;
        if (ViewDeleted(&view[i])) continue;
        out[n++] = (OutFile){view[i].name, view[i].data, view[i].len};
    }
    if (!n) { Toast("There are no files to download yet"); return; }
    DownloadZip(TextFormat("%s.zip", r->name), r->name, out, n);
    if (drafts && toastMsg[0] && strncmp(toastMsg, "Saved", 5) == 0)
        Toast(TextFormat("%.90s (with your uncommitted changes)", toastMsg));
}

/* The files exactly as they were in one commit. */
static void DownloadCommit(Repo *r, int ci)
{
    static OutFile out[MAX_FILES];
    Commit *c = &r->commits[ci];
    for (int k = 0; k < c->fileCount; k++)
        out[k] = (OutFile){c->names[k], r->blobs[c->blobs[k]].data, r->blobs[c->blobs[k]].len};
    if (!c->fileCount) { Toast("This commit has no files"); return; }
    char folder[NAME_LEN + 16];
    snprintf(folder, sizeof folder, "%s-%s", r->name, c->id);
    DownloadZip(TextFormat("%s.zip", folder), folder, out, c->fileCount);
}

/* ------------------------------------------------------------------ */
/* Contributions per person (the pie chart) and the People tab         */
/* ------------------------------------------------------------------ */

/* Slice colours, in a fixed order. They were checked with a colour-blindness
   simulator to stay tellable apart (including the last and first, which touch
   where the circle closes) and to stand out against the dark background.
   A person's colour follows their place in the list (owner first, then
   collaborators in the order they joined), never how much they've done,
   so colours don't jump around as commits come in. Past 6 people, the rest
   share one grey "Others" slice. */
static const Color PIE_COLORS[6] = {
    {0x39, 0x87, 0xe5, 255}, {0xd9, 0x59, 0x26, 255}, {0x19, 0x9e, 0x70, 255},
    {0xc9, 0x85, 0x00, 255}, {0xd5, 0x51, 0x81, 255}, {0x00, 0x83, 0x00, 255}
};
static const Color PIE_OTHER = {0x6e, 0x76, 0x81, 255};

/* lines = lines added + lines removed: every changed line counts as work, so removing
   14 lines counts the same as adding 14. A slice's size comes from lines. */
typedef struct { int user; int commits; int adds, dels, lines; Color color; int people; } Share;   /* user -1 = "Others" */
static Share shares[6];
static int   shareCount, shareTotal;    /* shareTotal = all lines changed, by everyone */

/* Owner, collaborators, then anyone else who has committed (former
   collaborators, or deleted accounts that show as "ghost"). */
static void BuildShares(Repo *r)
{
    int ids[MAX_USERS * 2] = {0}, n = 0;
    #define ADD_ID(u) do { bool seen_ = false; for (int q_ = 0; q_ < n; q_++) if (ids[q_] == (u)) seen_ = true; \
                           if (!seen_ && n < MAX_USERS * 2) ids[n++] = (u); } while (0)
    ADD_ID(r->owner);
    for (int i = 0; i < r->collabCount; i++) ADD_ID(r->collabs[i]);
    for (int k = 0; k < r->commitCount; k++) ADD_ID(r->commits[k].author);
    #undef ADD_ID

    shareCount = shareTotal = 0;
    for (int i = 0; i < n; i++) {
        int c = 0, a = 0, d = 0;
        for (int k = 0; k < r->commitCount; k++)
            if (r->commits[k].author == ids[i]) { c++; CommitStats(r, k); a += r->commits[k].sAdds; d += r->commits[k].sDels; }
        shareTotal += a + d;
        if (n <= 6 || i < 5) shares[shareCount++] = (Share){ids[i], c, a, d, a + d, PIE_COLORS[i], 1};
        else {
            if (shareCount == 5) shares[shareCount++] = (Share){-1, 0, 0, 0, 0, PIE_OTHER, 0};
            shares[5].commits += c; shares[5].adds += a; shares[5].dels += d; shares[5].lines += a + d; shares[5].people++;
        }
    }
}

/* A repository counts as "multi-user" once it has a collaborator, or commits by more than one person. */
static bool IsMultiUser(Repo *r)
{
    if (r->collabCount) return true;
    for (int k = 1; k < r->commitCount; k++) if (r->commits[k].author != r->commits[0].author) return true;
    return false;
}

static const char *ShareName(Share *s)
{
    return s->user >= 0 ? UserName(s->user) : TextFormat("Others (%d people)", s->people);
}

/* A tooltip that's drawn last, on top of everything else on screen. */
static char tipText[3][TEXT_LEN];
static int  tipLines;
static void SetTip(const char *a, const char *b, const char *c)
{
    snprintf(tipText[0], TEXT_LEN, "%s", a ? a : "");
    snprintf(tipText[1], TEXT_LEN, "%s", b ? b : "");
    snprintf(tipText[2], TEXT_LEN, "%s", c ? c : "");
    tipLines = c ? 3 : b ? 2 : 1;
}
static void DrawTip(void)
{
    if (!tipLines) return;
    int w = 0;
    for (int i = 0; i < tipLines; i++) { int lw = MeasureText(tipText[i], FS); if (lw > w) w = lw; }
    int bw = w + 32, bh = tipLines * 26 + 18;
    Vector2 m = GetMousePosition();
    int x = (int)m.x + 18, y = (int)m.y + 20;
    if (x + bw > W - 8) x = (int)m.x - 18 - bw;
    if (y + bh > H - 8) y = (int)m.y - 12 - bh;
    Rectangle box = {(float)x, (float)y, (float)bw, (float)bh};
    DrawRectangleRounded((Rectangle){box.x + 3, box.y + 4, box.width, box.height}, 0.1f, 6, (Color){0, 0, 0, 120});
    DrawRectangleRounded(box, 0.1f, 6, (Color){36, 41, 49, 255});
    DrawRectangleRoundedLinesEx(box, 0.1f, 6, 1, UI_BORDER);
    for (int i = 0; i < tipLines; i++)
        if (i == 0) DrawBold(tipText[i], x + 16, y + 10, FS, UI_TEXT);
        else        DrawText(tipText[i], x + 16, y + 10 + i * 26, FS, UI_MUTED);
    tipLines = 0;
}

static int Percent(int part, int total) { return total ? (part * 100 + total / 2) / total : 0; }

/* Draws the pie for the shares worked out by BuildShares. Slices start at
   the top and go clockwise; a thin gap separates them. Returns the slice
   under the mouse (it's drawn slightly bigger), or -1. */
static int DrawPie(Vector2 c, float rad)
{
    if (shareTotal == 0) {                      /* no lines changed yet: an empty ring */
        DrawRing(c, rad - 3, rad, 0, 360, 48, UI_BORDER);
        return -1;
    }
    Vector2 m = GetMousePosition();
    float dx = m.x - c.x, dy = m.y - c.y;
    bool inside = dx * dx + dy * dy <= (rad + 4) * (rad + 4);
    float mang = atan2f(dy, dx) * RAD2DEG;       /* same direction as raylib: clockwise from the right */
    if (mang < -90) mang += 360;                 /* so it runs -90 (top) ... 270 */

    int hov = -1, slices = 0;
    float a = -90;
    for (int i = 0; i < shareCount; i++) {
        if (!shares[i].lines) continue;
        slices++;
        float sweep = 360.0f * shares[i].lines / shareTotal;
        if (inside && mang >= a && mang < a + sweep) hov = i;
        a += sweep;
    }
    a = -90;
    for (int i = 0; i < shareCount; i++) {
        if (!shares[i].lines) continue;
        float sweep = 360.0f * shares[i].lines / shareTotal;
        float rr = i == hov ? rad + 4 : rad;
        DrawCircleSector(c, rr, a, a + sweep, (int)(sweep / 3) + 4, shares[i].color);
        a += sweep;
    }
    if (slices > 1) {                            /* the gaps between slices */
        Color gap = {22, 27, 34, 255};
        a = -90;
        for (int i = 0; i < shareCount; i++) {
            if (!shares[i].lines) continue;
            Vector2 e = {c.x + cosf(a * DEG2RAD) * (rad + 6), c.y + sinf(a * DEG2RAD) * (rad + 6)};
            DrawLineEx(c, e, 2, gap);
            a += 360.0f * shares[i].lines / shareTotal;
        }
    }
    return hov;
}

static void ShareTip(int i)
{
    Share *s = &shares[i];
    SetTip(ShareName(s),
           TextFormat("%d line%s changed  (%d%%)", s->lines, s->lines == 1 ? "" : "s", Percent(s->lines, shareTotal)),
           TextFormat("+%d added, -%d removed, in %d commit%s", s->adds, s->dels, s->commits, s->commits == 1 ? "" : "s"));
}

/* The little pie in a repository's header. Returns true if it was clicked. */
static bool HeaderPie(Repo *r, float rightX, float cy)
{
    if (!IsMultiUser(r)) return false;
    BuildShares(r);
    int members = 1 + r->collabCount, workers = 0;        /* people with access, or everyone who worked on it (forks) */
    for (int i = 0; i < shareCount; i++) if (shares[i].commits) workers += shares[i].user >= 0 ? 1 : shares[i].people;
    if (workers > members) members = workers;
    const char *label = TextFormat("%d %s", members, members == 1 ? "person" : "people");
    Vector2 c = {rightX - 18, cy};
    int lw = MeasureText(label, FS);
    Rectangle hit = {rightX - 36 - 12 - lw, cy - 20, (float)lw + 48, 40};
    bool hot = Hover(hit);
    DrawText(label, (int)(rightX - 36 - 12 - lw), (int)cy - 10, FS, hot ? UI_LINK : UI_MUTED);
    int h = DrawPie(c, 16);
    if (hot) {
        wantCursor = MOUSE_CURSOR_POINTING_HAND;
        if (h >= 0) ShareTip(h);
        else SetTip("Contributions", "Click to see who did what", NULL);
    }
    return Clicked(hit);
}

/* ---------------- the People tab ---------------- */

static void TabPeople(Repo *r)
{
    bool owner = IsMine(r), invited = IsInvited(r, curUser);
    float top = 186;
    float lw = ((float)W - 60) * 0.55f;
    float x = 20;

    /* an invitation waiting for you */
    if (invited) {
        Rectangle b = {x, top, lw, 60};
        DrawRectangleRec(b, (Color){20, 50, 90, 255});
        DrawRectangleLinesEx(b, 1, UI_LINK);
        DrawFit(TextFormat("%s invited you to collaborate on %s", UserName(r->owner), r->name),
                (int)b.x + 16, (int)b.y + 19, FS, (int)b.width - 290, UI_TEXT);
        if (Button((Rectangle){b.x + b.width - 260, b.y + 12, 120, 36}, "Accept", UI_GREEN)) {
            AcceptInvite(r, curUser); MarkDirty();
            Toast(TextFormat("You can now edit %s", r->name));
            return;
        }
        if (Button((Rectangle){b.x + b.width - 132, b.y + 12, 120, 36}, "Decline", UI_BTN)) {
            RemoveInvite(r, curUser); MarkDirty();
            Toast("Invitation declined");
            return;
        }
        top += 76;
    }

    /* ---- left: who has access ---- */
    DrawBold("People with access", (int)x, (int)top + 4, FS, UI_TEXT);
    float listTop = top + 38, listBottom = (float)H - 20 - (owner ? 150 : 0);
    Rectangle la = {x, listTop, lw, listBottom - listTop};
    int rows = 1 + r->collabCount + r->inviteCount;
    float rowH = 62;
    ScrollArea(la, &scPeopleTab, rows * rowH - 6);
    int act = 0, actUser = -1;                    /* 1 remove, 2 cancel invite, 3 leave */
    BeginScissorMode((int)la.x, (int)la.y, (int)la.width, (int)la.height);
    for (int i = 0; i < rows; i++) {
        int uid = i == 0 ? r->owner : i <= r->collabCount ? r->collabs[i - 1] : r->invites[i - 1 - r->collabCount];
        bool pending = i > r->collabCount;
        Rectangle row = {la.x, la.y + i * rowH - scPeopleTab, la.width - 12, rowH - 6};
        if (row.y > la.y + la.height || row.y + row.height < la.y) continue;
        DrawRectangleRec(row, UI_PANEL);
        DrawRectangleLinesEx(row, 1, UI_BORDER);
        DrawAvatar(uid, (int)row.x + 34, (int)(row.y + row.height / 2), 20);
        const char *nm = UserName(uid);
        Rectangle nameHit = {row.x + 66, row.y + 8, (float)MeasureText(nm, FS), 24};
        bool nh = Hover(la) && Hover(nameHit);
        DrawBold(nm, (int)row.x + 66, (int)row.y + 9, FS, UI_LINK);
        if (nh) { wantCursor = MOUSE_CURSOR_POINTING_HAND; DrawLine((int)nameHit.x, (int)row.y + 31, (int)(nameHit.x + nameHit.width), (int)row.y + 31, UI_LINK); }
        if (nh && Clicked(nameHit)) { EndScissorMode(); OpenProfile(uid); return; }
        const char *role = i == 0 ? "Owner" : pending ? TextFormat("Invited %s - waiting for an answer", TimeAgo(r->inviteTime[i - 1 - r->collabCount]))
                                                      : "Collaborator";
        DrawFit(TextFormat("%s%s", role, uid == curUser ? "  (you)" : ""), (int)row.x + 66, (int)row.y + 32, FS, (int)row.width - 230, UI_MUTED);

        Rectangle btn = {row.x + row.width - 170, row.y + 12, 158, 32};
        if (owner && i > 0 && !pending && Hover(la) && ConfirmButton(btn, "Remove", "Sure?", 100 + i)) { act = 1; actUser = uid; }
        if (owner && pending && Hover(la) && ConfirmButton(btn, "Cancel invite", "Sure?", 100 + i)) { act = 2; actUser = uid; }
        if (!owner && !pending && i > 0 && uid == curUser && Hover(la) && ConfirmButton(btn, "Leave", "Really leave?", 100 + i)) { act = 3; actUser = uid; }
    }
    EndScissorMode();
    DrawScrollbar(la, scPeopleTab, rows * rowH - 6);
    if (act) {
        const char *who = UserName(actUser);
        char whoCopy[NAME_LEN];
        snprintf(whoCopy, NAME_LEN, "%s", who);
        RemoveMember(r, actUser);
        MarkDirty();
        if (act == 1) Toast(TextFormat("%s can no longer edit this repository", whoCopy));
        if (act == 2) Toast(TextFormat("Invitation to %s cancelled", whoCopy));
        if (act == 3) { Toast(TextFormat("You've left %s", r->name)); SelectFileName(""); }
        return;
    }

    /* ---- owner: invite someone ---- */
    if (owner) {
        float iy = (float)H - 20 - 140;
        DrawLine((int)x, (int)iy, (int)(x + lw), (int)iy, UI_BORDER);
        DrawBold("Invite a collaborator", (int)x, (int)iy + 12, FS, UI_TEXT);
        bool enter = TextFieldUI(&tfInvite, (Rectangle){x, iy + 42, lw - 130, 40}, "their username");
        if (Button((Rectangle){x + lw - 120, iy + 42, 120, 40}, "Invite", UI_GREEN) || enter) {
            const char *err = InviteUser(r, tfInvite.buf);
            if (err) Toast(err);
            else {
                Toast(TextFormat("Invited %s - they'll see it on their profile", tfInvite.buf));
                TF_Clear(&tfInvite);
                MarkDirty();
            }
        }
        /* everyone who could be invited, as quick picks */
        float cx2 = x + MeasureText("People here:", FS) + 12;
        DrawText("People here:", (int)x, (int)iy + 98, FS, UI_MUTED);
        int shown = 0;
        for (int i = 0; i < userCount; i++) {
            User *u = &users[i];
            if (u->id == r->owner || IsCollab(r, u->id) || IsInvited(r, u->id)) continue;
            int w = MeasureText(u->name, FS) + 20;
            if (cx2 + w > x + lw) break;
            Rectangle chip = {cx2, iy + 94, (float)w, 28};
            bool hot = Hover(chip);
            DrawRectangleRounded(chip, 0.5f, 6, hot ? UI_HOVER : UI_BTN);
            DrawText(u->name, (int)chip.x + 10, (int)chip.y + 4, FS, UI_LINK);
            if (hot) wantCursor = MOUSE_CURSOR_POINTING_HAND;
            if (Clicked(chip)) { snprintf(tfInvite.buf, TEXT_LEN, "%s", u->name); tfInvite.len = (int)strlen(tfInvite.buf); }
            cx2 += w + 8;
            shown++;
        }
        if (!shown) DrawText("everyone is already here", (int)cx2, (int)iy + 98, FS, UI_MUTED);
    }

    /* ---- right: contributions ---- */
    float rx = x + lw + 20, rw = (float)W - 20 - rx;
    Rectangle card = {rx, 186, rw, (float)H - 206};
    DrawRectangleRec(card, UI_PANEL);
    DrawRectangleLinesEx(card, 1, UI_BORDER);
    DrawBold("Contributions", (int)rx + 18, (int)card.y + 16, FS, UI_TEXT);
    if (!IsMultiUser(r)) {
        DrawFit(TextFormat("Only %s has worked on this repository so far.", UserName(r->owner)),
                (int)rx + 18, (int)card.y + 52, FS, (int)rw - 36, UI_MUTED);
        DrawFit(owner ? "Invite a collaborator to see who did what here." : "Once more people take part, a breakdown appears here.",
                (int)rx + 18, (int)card.y + 78, FS, (int)rw - 36, UI_MUTED);
        return;
    }
    BuildShares(r);
    DrawFit(TextFormat("by lines changed (added + removed)  |  %d in total", shareTotal), (int)rx + 18, (int)card.y + 42, FS, (int)rw - 36, UI_MUTED);
    float rad = fminf(rw * 0.3f, 100);
    Vector2 c = {rx + rw / 2, card.y + 84 + rad};
    int h = DrawPie(c, rad);
    if (h >= 0) ShareTip(h);

    /* the key: every person with their colour, commits, share and lines */
    float ky = c.y + rad + 26;
    for (int i = 0; i < shareCount && ky + 24 < card.y + card.height; i++) {
        Share *s = &shares[i];
        DrawRectangleRounded((Rectangle){rx + 18, ky + 3, 14, 14}, 0.3f, 4, s->color);
        DrawFit(ShareName(s), (int)rx + 42, (int)ky, FS, (int)(rw * 0.42f), UI_TEXT);
        const char *val = TextFormat("%d line%s  %d%%", s->lines, s->lines == 1 ? "" : "s", Percent(s->lines, shareTotal));
        DrawText(val, (int)(rx + rw - 18) - MeasureText(val, FS), (int)ky, FS, UI_MUTED);
        ky += 30;
    }
}

/* Returns true if the repository was deleted or we left it (so stop drawing it). */
/* ------------------------------------------------------------------ */
/* Forks                                                              */
/*                                                                    */
/* A fork is a full copy of someone else's repository - every commit  */
/* and every blob - that now belongs to you. The commits keep their   */
/* ids, so later we can tell which commits both repositories share:  */
/*                                                                    */
/*   original:  A - B - C - D - E          forkBase = C (copied up to) */
/*   fork:      A - B - C - X               X = your own commit       */
/*                                                                    */
/*   behind = commits of the original after forkBase (D, E)  = 2      */
/*   ahead  = commits of the fork the original doesn't have (X) = 1   */
/*                                                                    */
/* Sync fork: if you're not ahead, D and E are simply copied across   */
/* ("fast-forward"). If you are, the two sides are combined file by   */
/* file into one new "merge" commit (a three-way merge, using C as    */
/* the common starting point).                                        */
/* ------------------------------------------------------------------ */
static int FindRepoOf(int owner, const char *name)
{
    for (int i = 0; i < repoCount; i++)
        if (repos[i].owner == owner && EqualsCI(repos[i].name, name)) return i;
    return -1;
}

static bool IsForkOf(Repo *fork, Repo *orig)
{
    return fork->isFork && fork->forkOwner == orig->owner && EqualsCI(fork->forkName, orig->name);
}

/* The original a fork came from (its index), or -1 if it isn't a fork or the original is gone. */
static int Upstream(Repo *r) { return r->isFork ? FindRepoOf(r->forkOwner, r->forkName) : -1; }

static int ForkCount(Repo *r)
{
    int n = 0;
    for (int i = 0; i < repoCount; i++) if (IsForkOf(&repos[i], r)) n++;
    return n;
}

/* uid's fork of repos[orig], or -1 */
static int FindForkOf(int uid, int orig)
{
    for (int i = 0; i < repoCount; i++)
        if (repos[i].owner == uid && IsForkOf(&repos[i], &repos[orig])) return i;
    return -1;
}

typedef struct {
    int  up;        /* index of the original, -1 = gone                              */
    bool related;   /* the two still share history                                   */
    int  base;      /* the original's newest commit this fork also has (-1 = none)   */
    int  behind;    /* the original's commits this fork doesn't have yet             */
    int  ahead;     /* this fork's commits the original doesn't have                 */
} ForkState;

/* Commits are matched by their git fingerprint, so it doesn't matter how
   they got here - Sync fork, or pushed with git. Worked out again only
   when either history changes. */
static ForkState GetForkState(Repo *r)
{
    static struct { const Repo *r, *p; unsigned rg, pg; int rc, pc; ForkState s; } memo[8];
    ForkState s = {-1, false, -1, 0, 0};
    s.up = Upstream(r);
    if (s.up < 0) return s;
    Repo *p = &repos[s.up];
    int slot = (int)(r - repos) % 8;
    if (memo[slot].r == r && memo[slot].p == p && memo[slot].rg == r->gen && memo[slot].pg == p->gen &&
        memo[slot].rc == r->commitCount && memo[slot].pc == p->commitCount && memo[slot].s.up == s.up) return memo[slot].s;
    /* compares the two default branches: what's in one's history and not the other's */
    uint8_t *mp = calloc((size_t)p->commitCount + 1, 1), *mr = calloc((size_t)r->commitCount + 1, 1);
    if (mp && mr) {
        MarkAnc(p, DefaultTip(p), mp, 1);
        MarkAnc(r, DefaultTip(r), mr, 1);
        for (int k = 0; k < p->commitCount; k++) {
            if (!mp[k]) continue;
            int there = CommitBySha(r, p->commits[k].sha);
            if (there >= 0 && mr[there]) s.base = k;
            else s.behind++;
        }
        for (int k = 0; k < r->commitCount; k++) {
            if (!mr[k]) continue;
            int there = CommitBySha(p, r->commits[k].sha);
            if (there < 0 || !mp[there]) s.ahead++;
        }
    }
    free(mp); free(mr);
    s.related = s.base >= 0 || !r->forkBase[0] || !p->commitCount || !r->commitCount;
    memo[slot].r = r; memo[slot].p = p; memo[slot].rg = r->gen; memo[slot].pg = p->gen;
    memo[slot].rc = r->commitCount; memo[slot].pc = p->commitCount; memo[slot].s = s;
    return s;
}

/* Copies commit ci of src onto the end of dst - the very same git commit,
   so it keeps its fingerprint. Blob numbers differ between repositories,
   so each file's contents are stored again in dst (InternBlob reuses a blob
   when dst already has it). The commits it was built on must already be in dst. */
static bool CopyCommit(Repo *dst, Repo *src, int ci)
{
    Commit *s = &src->commits[ci];
    if (CommitBySha(dst, s->sha) >= 0) return true;               /* already there */
    Commit c = *s;
    int n = s->fileCount;
    c.names = malloc((size_t)(n ? n : 1) * sizeof *c.names);
    c.blobs = malloc((size_t)(n ? n : 1) * sizeof *c.blobs);
    c.modes = malloc((size_t)(n ? n : 1));
    c.raw   = malloc((size_t)s->rawLen + 1);
    if (!c.names || !c.blobs || !c.modes || !c.raw) { FreeCommit(&c); return false; }
    memcpy(c.names, s->names, (size_t)n * sizeof *c.names);
    memcpy(c.modes, s->modes, (size_t)n);
    memcpy(c.raw, s->raw, (size_t)s->rawLen + 1);
    for (int j = 0; j < n; j++) {
        Blob *b = &src->blobs[s->blobs[j]];
        c.blobs[j] = InternBlobSha(dst, b->data, b->len, b->sha);
        if (c.blobs[j] < 0) { FreeCommit(&c); return false; }
    }
    for (int q = 0; q < s->parentCount; q++) {
        c.parents[q] = CommitBySha(dst, src->commits[s->parents[q]].sha);
        if (c.parents[q] < 0) { FreeCommit(&c); return false; }
    }
    c.copied = true;
    if (!AppendCommit(dst, &c)) { FreeCommit(&c); return false; }
    return true;
}

/* Copies every commit of src that dst doesn't have, oldest first: all of
   them, or only the history of commit "tip" (if it isn't -1).
   If it fails part way, dst goes back to how it was. */
static bool CopyMissing(Repo *dst, Repo *src, int tip)
{
    int before = dst->commitCount;
    uint8_t *m = NULL;
    if (tip >= 0) { m = calloc((size_t)src->commitCount + 1, 1); if (!m) return false; MarkAnc(src, tip, m, 1); }
    for (int k = 0; k < src->commitCount; k++)
        if ((!m || m[k]) && !CopyCommit(dst, src, k)) {
            free(m);
            while (dst->commitCount > before) FreeCommit(&dst->commits[--dst->commitCount]);
            MapFree(&dst->commitMap);
            for (int j = 0; j < dst->commitCount; j++) MapPut(&dst->commitMap, dst->commits[j].sha, j);
            dst->gen++;
            return false;
        }
    free(m);
    return true;
}

static void TryForkRepo(int orig)
{
    Repo *p = &repos[orig];
    int mineAlready = FindForkOf(curUser, orig);
    if (mineAlready >= 0) { OpenRepo(mineAlready); Toast("You already have a fork of this repository - here it is"); return; }
    if (repoCount >= MAX_REPOS) { Toast("This computer has reached the maximum number of repositories"); return; }

    /* same name if it's free, otherwise name-fork, name-fork-2, ... */
    char nm[NAME_LEN];
    snprintf(nm, NAME_LEN, "%s", p->name);
    for (int n = 1; FindRepoOf(curUser, nm) >= 0; n++)
        snprintf(nm, NAME_LEN, n == 1 ? "%.50s-fork" : "%.50s-fork-%d", p->name, n);

    Repo *r = NewRepo(curUser, nm, p->desc);       /* p is still valid: repos[] never moves */
    r->isFork    = true;
    r->forkOwner = p->owner;
    snprintf(r->forkName, NAME_LEN, "%s", p->name);
    snprintf(r->branch, NAME_LEN, "%s", p->branch);
    if (p->branchCount) ShaHex(p->commits[DefaultTip(p)].sha, r->forkBase); else r->forkBase[0] = 0;
    if (!CopyMissing(r, p, -1)) {
        DeleteRepo(repoCount - 1);
        Toast("Couldn't copy that repository - it's too big");
        return;
    }
    for (int b = 0; b < p->branchCount; b++)                 /* every branch comes too */
        AddBranch(r, p->branches[b].name, CommitBySha(r, p->commits[p->branches[b].tip].sha));
    r->defBranch = r->curBranch = p->defBranch;
    MarkDirty();
    OpenRepo(repoCount - 1);
    Toast(TextFormat("Forked %s / %s - this copy is yours to change", UserName(p->owner), p->name));
}

/* A file's contents in a commit ("has" = false: the file isn't there). */
typedef struct { bool has; const char *data; int len; int mode; } Version;
static Version VersionIn(Repo *r, Commit *c, const char *name)
{
    Version v = {false, NULL, 0, 0};
    int k = c ? FindInCommit(c, name) : -1;
    if (k >= 0) { v.has = true; v.data = r->blobs[c->blobs[k]].data; v.len = r->blobs[c->blobs[k]].len; v.mode = c->modes[k]; }
    return v;
}
static bool SameVersion(Version a, Version b)
{
    if (a.has != b.has) return false;
    return !a.has || (a.len == b.len && a.mode == b.mode && memcmp(a.data, b.data, (size_t)a.len) == 0);
}

/* Brings the original's new commits into fork r.
   Returns NULL on success (and a message for the user in *done), or an error. */
static const char *SyncFork(Repo *r, char *done, int doneCap)
{
    ForkState s = GetForkState(r);
    if (s.up < 0)     return "The original repository has been deleted";
    if (!s.related)   return "The original repository's history no longer matches this fork";
    if (!s.behind)    return "Already up to date";
    Repo *p = &repos[s.up];
    char who[96];
    snprintf(who, sizeof who, "%.40s / %.40s", UserName(p->owner), p->name);
    int behind = s.behind;

    /* 1) fast-forward: you haven't committed anything of your own, so just copy their new commits */
    int ptip = DefaultTip(p);
    if (s.ahead == 0) {
        if (r->commitCount + behind > MAX_COMMITS) return "This fork has reached its commit limit";
        if (!CopyMissing(r, p, ptip)) return "This fork is out of storage space";
        int nt = CommitBySha(r, p->commits[ptip].sha);
        if (r->branchCount) r->branches[r->defBranch].tip = nt;
        else { AddBranch(r, DefBranchName(p), nt); r->defBranch = r->curBranch = 0; }
        r->gen++;
        ShaHex(p->commits[ptip].sha, r->forkBase);
        snprintf(done, (size_t)doneCap, "Synced: copied %d commit%s from %s", behind, behind == 1 ? "" : "s", who);
        return NULL;
    }

    /* 2) three-way merge. For every file, compare
            base   = the original's newest commit that this fork also has
            theirs = the original now
            ours   = this fork now
          If only one side changed the file, take that side. If both made the
          same change, fine. If both changed it differently, that's a conflict. */
    Commit *cb = s.base >= 0 ? &p->commits[s.base] : NULL, *ct = &p->commits[ptip], *co = &r->commits[DefaultTip(r)];
    static const char *names[MAX_FILES * 3];
    static char pick[MAX_FILES * 3];      /* 'o' ours, 't' theirs */
    int nn = 0;
    Commit *sides[3] = {co, ct, cb};
    Snap all = {0};
    for (int sd = 0; sd < 3; sd++)                                 /* every path on any side, once */
        for (int j = 0; sides[sd] && j < sides[sd]->fileCount; j++)
            if (SnapFind(&all, sides[sd]->names[j]) < 0 && !SnapSet(&all, sides[sd]->names[j], 0, 0)) { SnapFree(&all); return "Out of memory"; }
    for (int q = 0; q < all.n; q++) names[nn++] = all.names[q];
    SnapFree(&all);

    /* first pass: decide every file, and stop before changing anything if there's a conflict */
    int  conflicts = 0, keep = 0;
    const char *firstConflict = NULL;
    bool changesOurs = false;
    for (int q = 0; q < nn; q++) {
        Version b = VersionIn(p, cb, names[q]), t = VersionIn(p, ct, names[q]), o = VersionIn(r, co, names[q]);
        if      (SameVersion(o, t) || SameVersion(b, t)) pick[q] = 'o';   /* nothing new from them   */
        else if (SameVersion(b, o))                      pick[q] = 't';   /* only they changed it    */
        else { pick[q] = '!'; if (!conflicts++) firstConflict = names[q]; continue; }
        if (pick[q] == 't') changesOurs = true;
        if ((pick[q] == 'o' ? o : t).has) keep++;
    }
    if (conflicts)
        return TextFormat("Can't merge: you and %s both changed %s%s", UserName(p->owner), firstConflict,
                          conflicts > 1 ? TextFormat(" (and %d more file%s)", conflicts - 1, conflicts == 2 ? "" : "s") : "");
    if (keep > MAX_FILES) return TextFormat("The merged repository would have more than %d files", MAX_FILES);
    if (r->commitCount + behind + 1 > MAX_COMMITS) return "This fork has reached its commit limit";

    /* bring their commits in too (a merge commit points at both sides, so git needs both) */
    int ourHead = DefaultTip(r);
    if (!CopyMissing(r, p, ptip)) return "This fork is out of storage space";
    int theirHead = CommitBySha(r, ct->sha);
    co = &r->commits[ourHead];                                         /* the list may have moved */

    /* even if nothing of theirs changes your files, the merge commit records that you have their history */
    Snap m;
    memset(&m, 0, sizeof m);
    bool ok = SnapRoom(&m, keep > 0 ? keep : 1);
    for (int q = 0; q < nn && ok; q++) {
        Version v = pick[q] == 'o' ? VersionIn(r, co, names[q]) : VersionIn(p, ct, names[q]);
        if (!v.has) continue;                                           /* deleted on the winning side */
        int bl = InternBlob(r, v.data, v.len);
        ok = bl >= 0 && SnapSet(&m, names[q], bl, v.mode);
    }
    if (!ok) { SnapFree(&m); return "This fork is out of storage space"; }
    /* the merge sits on top of your latest commit (git's "first parent") and theirs.
       FinishCommit builds on the last commit in the list - theirs now - so it's put together here */
    Commit c;
    memset(&c, 0, sizeof c);
    c.fileCount = m.n; c.names = m.names; c.blobs = m.blobs; c.modes = m.modes;
    c.author = curUser;
    c.time = (long long)time(NULL);
    snprintf(c.message, TEXT_LEN, "Merge %d commit%s from %s", behind, behind == 1 ? "" : "s", who);
    c.parents[0] = ourHead; c.parents[1] = theirHead; c.parentCount = 2;
    const uint8_t *par[2] = {r->commits[ourHead].sha, r->commits[theirHead].sha};
    uint8_t tree[20];
    if (CommitTree(r, &c, tree, NULL, NULL))
        c.raw = MakeRawCommit(tree, par, 2, UserName(curUser), UserEmail(curUser), c.time, c.message, &c.rawLen);
    if (!c.raw || !AppendCommit(r, &c)) { FreeCommit(&c); return "Couldn't save the merge commit"; }
    r->branches[r->defBranch].tip = r->commitCount - 1;
    ShaHex(ct->sha, r->forkBase);
    if (!changesOurs) snprintf(done, (size_t)doneCap, "Up to date: you already had everything from %s", who);
    else snprintf(done, (size_t)doneCap, "Merged %d commit%s from %s into a new commit", behind, behind == 1 ? "" : "s", who);
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Branches and merging                                               */
/*                                                                    */
/* A branch is just a name pointing at a commit. Making a new branch  */
/* costs nothing: it starts at the commit you're on, and moves on by  */
/* itself as you commit to it. Merging brings one branch's work into  */
/* another with a "merge commit" that has both tips as parents - so    */
/* the history (and the graph) shows exactly where the two joined.    */
/* ------------------------------------------------------------------ */

/* Marks commit tip and every commit it was built on with bit. */
static void MarkAnc(Repo *r, int tip, uint8_t *mark, uint8_t bit)
{
    if (tip < 0) return;
    int *st = malloc(((size_t)r->commitCount + 1) * sizeof(int)), n = 0;
    if (!st) return;
    st[n++] = tip;
    while (n) {
        int k = st[--n];
        if (mark[k] & bit) continue;
        mark[k] |= bit;
        for (int q = 0; q < r->commits[k].parentCount; q++) {
            int p = r->commits[k].parents[q];
            if (!(mark[p] & bit) && n < r->commitCount) st[n++] = p;
        }
    }
    free(st);
}
/* Is commit a part of b's history (or b itself)? */
static bool IsAncestor(Repo *r, int a, int b)
{
    if (a < 0 || b < 0) return false;
    if (a == b) return true;
    if (a > b) return false;                         /* parents always come earlier in the list */
    uint8_t *m = calloc((size_t)r->commitCount, 1);
    if (!m) return false;
    MarkAnc(r, b, m, 1);
    bool yes = m[a] != 0;
    free(m);
    return yes;
}
/* The newest commit both a and b are built on: where the two lines of work split. */
static int MergeBase(Repo *r, int a, int b)
{
    uint8_t *m = calloc((size_t)r->commitCount + 1, 1);
    if (!m) return -1;
    MarkAnc(r, a, m, 1);
    MarkAnc(r, b, m, 2);
    int best = -1;
    for (int k = r->commitCount - 1; k >= 0 && best < 0; k--) if (m[k] == 3) best = k;   /* later in the list = newer */
    free(m);
    return best;
}
/* How many commits a has that b doesn't (b may be -1). */
static int CountAhead(Repo *r, int a, int b)
{
    uint8_t *m = calloc((size_t)r->commitCount + 1, 1);
    if (!m) return 0;
    MarkAnc(r, a, m, 1);
    MarkAnc(r, b, m, 2);
    int n = 0;
    for (int k = 0; k < r->commitCount; k++) if (m[k] == 1) n++;
    free(m);
    return n;
}

/* After a branch is deleted, the commits no branch leads to any more are let go
   (the list is closed up and every reference to a position is updated). */
static void PruneUnreachable(Repo *r)
{
    int n = r->commitCount;
    uint8_t *m = calloc((size_t)n + 1, 1);
    int *to = malloc(((size_t)n + 1) * sizeof(int));
    if (!m || !to) { free(m); free(to); return; }
    for (int b = 0; b < r->branchCount; b++) MarkAnc(r, r->branches[b].tip, m, 1);
    int k2 = 0;
    for (int k = 0; k < n; k++) {
        if (!m[k]) { FreeCommit(&r->commits[k]); to[k] = -1; continue; }
        to[k] = k2;
        r->commits[k2] = r->commits[k];
        for (int q = 0; q < r->commits[k2].parentCount; q++) r->commits[k2].parents[q] = to[r->commits[k2].parents[q]];
        k2++;
    }
    if (k2 != n) {
        r->commitCount = k2;
        for (int b = 0; b < r->branchCount; b++) r->branches[b].tip = to[r->branches[b].tip];
        MapFree(&r->commitMap);
        for (int k = 0; k < k2; k++) MapPut(&r->commitMap, r->commits[k].sha, k);
        r->gen++;
    }
    free(m); free(to);
}

/* Git's rules for a branch name, kept simple: letters, digits and - _ . /
   (so "feature/login" works), no "..", no "//", not starting or ending with / or . */
static const char *BranchNameProblem(const char *n)
{
    size_t len = strlen(n);
    if (!len) return "Type a name for the branch";
    if (len >= NAME_LEN) return "That name is too long";
    for (size_t i = 0; i < len; i++) {
        char c = n[i];
        if (!(isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '/')) return "Branch names can use letters, numbers and - _ . /";
    }
    if (n[0] == '/' || n[0] == '.' || n[0] == '-' || n[len - 1] == '/' || n[len - 1] == '.' ||
        strstr(n, "..") || strstr(n, "//") || strstr(n, "/.") || (len >= 5 && strcmp(n + len - 5, ".lock") == 0) || strcmp(n, "HEAD") == 0)
        return "That isn't a name git allows for a branch";
    return NULL;
}

/* A new branch starting at the commit you're on; you move onto it. */
static const char *CreateBranch(Repo *r, const char *name)
{
    const char *bad = BranchNameProblem(name);
    if (bad) return bad;
    if (!r->branchCount) return "Make the first commit before making branches";
    if (FindBranch(r, name) >= 0) return TextFormat("There's already a branch called %s", name);
    for (int b = 0; b < r->branchCount; b++) {             /* git can't have "a" and "a/b" both */
        size_t l = strlen(r->branches[b].name), nl = strlen(name);
        if ((l < nl && strncmp(name, r->branches[b].name, l) == 0 && name[l] == '/') ||
            (nl < l && strncmp(r->branches[b].name, name, nl) == 0 && r->branches[b].name[nl] == '/'))
            return TextFormat("That name clashes with the branch %s", r->branches[b].name);
    }
    int b = AddBranch(r, name, HeadIndex(r));
    if (b < 0) return TextFormat("A repository can have up to %d branches", MAX_BRANCHES);
    r->curBranch = b;
    r->gen++;
    return NULL;
}

/* Deletes a branch (never the default one). Its commits stay if another branch leads to them. */
static const char *DeleteBranch(Repo *r, int b)
{
    if (b < 0 || b >= r->branchCount) return "No such branch";
    if (b == r->defBranch) return "The default branch can't be deleted";
    RemoveDrafts(r, -1, r->branches[b].name);
    memmove(&r->branches[b], &r->branches[b + 1], (size_t)(r->branchCount - b - 1) * sizeof(Branch));
    r->branchCount--;
    if (r->defBranch > b) r->defBranch--;
    if (r->curBranch == b) r->curBranch = r->defBranch;
    else if (r->curBranch > b) r->curBranch--;
    PruneUnreachable(r);
    r->gen++;
    return NULL;
}

/* ---------- merging two versions of a text file, line by line ----------
   The classic "three-way merge": compare each side with the common starting
   version (base). Lines that are the same in all three are fixed points;
   between two fixed points, if only one side changed something, take that
   side; if both made the very same change, fine; if they changed it in
   different ways, that's a conflict - both are kept, between git's markers:
       <<<<<<< main
       (your lines)
       =======
       (their lines)
       >>>>>>> feature */
static void MapLines(const char *base, const char *other, int nb, int *map)
{
    for (int i = 0; i < nb; i++) map[i] = -1;
    ComputeDiff(base, other, NULL, NULL);
    for (int i = 0; i < dOpCount; i++) if (dOps[i].kind == ' ' && dOps[i].a < nb) map[dOps[i].a] = dOps[i].b;
}
static bool SameLines(LineRef *a, int a0, int a1, LineRef *b, int b0, int b1)
{
    if (a1 - a0 != b1 - b0) return false;
    for (int i = 0; i < a1 - a0; i++) if (!SameLine(a[a0 + i], b[b0 + i])) return false;
    return true;
}
static void AddLines(Buf *o, LineRef *l, int from, int to)
{
    for (int i = from; i < to; i++) { BufAdd(o, l[i].s, (size_t)l[i].n); BufByte(o, '\n'); }
}
static bool EndsNL(const char *t, int n) { return n > 0 && t[n - 1] == '\n'; }

/* Returns the merged text (malloc'd, NUL-terminated); *conflicts gets how many clashing places. */
static char *MergeText(const char *bt, int bn, const char *ot, int on, const char *tt, int tn,
                       const char *ourName, const char *theirName, int *outLen, int *conflicts)
{
    LineRef *B = NULL, *O = NULL, *T = NULL;
    int bc = 0, oc = 0, tcap = 0;
    int nB = SplitLines(bt, &B, &bc), nO = SplitLines(ot, &O, &oc), nT = SplitLines(tt, &T, &tcap);
    int *mO = malloc(((size_t)nB + 1) * sizeof(int)), *mT = malloc(((size_t)nB + 1) * sizeof(int));
    Buf out = {0};
    *conflicts = 0;
    if (mO && mT) {
        MapLines(bt, ot, nB, mO);
        MapLines(bt, tt, nB, mT);
        int pb = -1, po = -1, pt = -1;
        for (int i = 0; i <= nB; i++) {
            if (i < nB && (mO[i] < 0 || mT[i] < 0 || mO[i] <= po || mT[i] <= pt)) continue;   /* not a fixed point */
            int oi = i < nB ? mO[i] : nO, ti = i < nB ? mT[i] : nT;
            int b0 = pb + 1, o0 = po + 1, t0 = pt + 1;
            bool oSame = SameLines(O, o0, oi, B, b0, i), tSame = SameLines(T, t0, ti, B, b0, i);
            if (oSame)                                    AddLines(&out, T, t0, ti);    /* only they changed it  */
            else if (tSame || SameLines(O, o0, oi, T, t0, ti)) AddLines(&out, O, o0, oi);    /* only you, or the same */
            else {                                                                       /* both, differently     */
                (*conflicts)++;
                BufStr(&out, "<<<<<<< "); BufStr(&out, ourName); BufByte(&out, '\n');
                AddLines(&out, O, o0, oi);
                BufStr(&out, "=======\n");
                AddLines(&out, T, t0, ti);
                BufStr(&out, ">>>>>>> "); BufStr(&out, theirName); BufByte(&out, '\n');
            }
            if (i < nB) AddLines(&out, B, i, i + 1);
            pb = i; po = oi; pt = ti;
        }
        /* does the file end with a newline? take whichever side changed that */
        bool eb = EndsNL(bt, bn), eo = EndsNL(ot, on), et = EndsNL(tt, tn);
        bool want = eo == eb ? et : eo;
        if (!want && out.n > 0 && out.d[out.n - 1] == '\n' && !*conflicts) out.n--;
    }
    free(B); free(O); free(T); free(mO); free(mT);
    BufByte(&out, 0);
    if (out.oom) { BufFree(&out); return NULL; }
    *outLen = (int)out.n - 1;
    return (char *)out.d;
}

/* Does this text still have a conflict marker at the start of a line? */
static bool HasMarkers(const char *t, int n)
{
    for (int i = 0; i + 8 <= n; i++)
        if ((i == 0 || t[i - 1] == '\n') && memcmp(t + i, "<<<<<<< ", 8) == 0) return true;
    return false;
}

/* What happened in the last merge, for the message shown afterwards. */
static int mergeConflictFiles, mergeAutoFiles;
static char mergeNote[TEXT_LEN];

/* Merges branch "from" into the branch you're on.
   No clashes: a merge commit is made straight away. Clashes: the merged
   files go into your draft, the clashing places marked, and the merge is
   finished when you commit. Returns NULL (and *done) or an error. */
static const char *MergeBranch(Repo *r, int from, char *done, int doneCap)
{
    if (from < 0 || from >= r->branchCount || from == r->curBranch) return "Pick another branch to merge";
    char ours[NAME_LEN], theirs[NAME_LEN];
    snprintf(ours, sizeof ours, "%s", CurBranchName(r));
    snprintf(theirs, sizeof theirs, "%s", r->branches[from].name);
    int o = HeadIndex(r), t = r->branches[from].tip;
    if (IsAncestor(r, t, o)) return TextFormat("Nothing to merge - %s already has everything from %s", ours, theirs);
    Draft *d = GetDraft(r, curUser, false);
    if (d && d->merging) return "Finish (or abort) the merge you've started first";
    DraftTidy(r, curUser);
    int ch, cf;
    DraftCounts(r, curUser, &ch, &cf);
    if (ch || cf) return TextFormat("Commit or undo your changes on %s first", ours);
    if (r->commitCount >= MAX_COMMITS) return "This repository has reached its commit limit";

    int base = MergeBase(r, o, t);
    Commit *cb = base >= 0 ? &r->commits[base] : NULL, *co = &r->commits[o], *ct = &r->commits[t];
    Snap all = {0}, res;
    for (int sd = 0; sd < 3; sd++) {                       /* every path on any side */
        Commit *c = sd == 0 ? co : sd == 1 ? ct : cb;
        for (int j = 0; c && j < c->fileCount; j++)
            if (SnapFind(&all, c->names[j]) < 0 && !SnapSet(&all, c->names[j], 0, 0)) { SnapFree(&all); return "Out of memory"; }
    }
    if (!SnapFrom(&res, co)) { SnapFree(&all); return "Out of memory"; }
    /* conflicting texts wait here until we know whether there are any */
    typedef struct { const char *name; char *text; int len; } Clash;
    Clash *clash = NULL;
    int ncl = 0, ccap = 0, auto_ = 0, kept = 0;
    const char *err = NULL;
    for (int q = 0; q < all.n && !err; q++) {
        const char *nm = all.names[q];
        Version b = VersionIn(r, cb, nm), vt = VersionIn(r, ct, nm), vo = VersionIn(r, co, nm);
        if (SameVersion(vo, vt) || SameVersion(b, vt)) continue;                       /* nothing new from them */
        if (SameVersion(b, vo)) {                                                       /* only they changed it  */
            if (!vt.has) { SnapRemove(&res, nm); continue; }
            int bl = InternBlob(r, vt.data, vt.len);
            if (bl < 0 || !SnapSet(&res, nm, bl, vt.mode)) err = "This repository is out of storage space";
            continue;
        }
        /* both changed it */
        if (!vo.has || !vt.has) { kept++; if (!vo.has) { int bl = InternBlob(r, vt.data, vt.len); if (bl < 0 || !SnapSet(&res, nm, bl, vt.mode)) err = "Out of space"; } continue; }  /* deleted on one side: keep the changed one */
        if (LooksBinary(vo.data, vo.len) || LooksBinary(vt.data, vt.len)) { kept++; continue; }   /* can't merge pictures: keep yours */
        int ml = 0, nc = 0;
        char *m = MergeText(b.has ? b.data : "", b.len, vo.data, vo.len, vt.data, vt.len, ours, theirs, &ml, &nc);
        if (!m) { err = "Out of memory"; break; }
        if (nc) {
            clash = GrowArr(clash, &ccap, ncl + 1, sizeof(Clash));
            if (ncl < ccap) clash[ncl++] = (Clash){nm, m, ml}; else free(m);
            continue;
        }
        auto_++;
        int bl = InternBlob(r, m, ml);
        free(m);
        if (bl < 0 || !SnapSet(&res, nm, bl, vo.mode)) err = "This repository is out of storage space";
    }
    SnapFree(&all);
    const char *clashName = !err ? PathClash(res.names, res.n) : NULL;
    if (clashName && !err) err = TextFormat("Can't merge: %s would be both a file and a folder", clashName);
    if (err) { SnapFree(&res); for (int i = 0; i < ncl; i++) free(clash[i].text); free(clash); return err; }

    mergeConflictFiles = ncl; mergeAutoFiles = auto_;
    mergeNote[0] = 0;
    if (kept) snprintf(mergeNote, sizeof mergeNote, " (%d file%s changed on both sides couldn't be combined - kept %s's version)", kept, kept == 1 ? "" : "s", ours);
    char msg[TEXT_LEN];
    snprintf(msg, sizeof msg, "Merge branch '%s' into %s", theirs, ours);
    if (!ncl) {                                              /* clean: commit it now */
        if (!FinishCommit(r, &res, msg, t)) return "Couldn't save the merge commit";
        snprintf(done, (size_t)doneCap, "Merged %s into %s%s%s", theirs, ours,
                 auto_ ? TextFormat(" - %d file%s changed on both sides were combined line by line", auto_, auto_ == 1 ? "" : "s") : "", mergeNote);
        return NULL;
    }
    /* clashes: everything the merge changes goes into your draft, and you finish it by committing */
    d = GetDraft(r, curUser, true);
    if (!d) { SnapFree(&res); for (int i = 0; i < ncl; i++) free(clash[i].text); free(clash); return "Too many drafts in this repository"; }
    int ri = 0, hi = 0;
    while (ri < res.n || hi < co->fileCount) {               /* res vs your head, side by side (both sorted) */
        int c = ri >= res.n ? 1 : hi >= co->fileCount ? -1 : strcmp(res.names[ri], co->names[hi]);
        if (c == 0) {
            if (res.blobs[ri] != co->blobs[hi]) { WorkFile *e = DraftEdit(r, curUser, res.names[ri]); if (e) WF_SetN(e, r->blobs[res.blobs[ri]].data, r->blobs[res.blobs[ri]].len); }
            ri++; hi++;
        } else if (c < 0) { WorkFile *e = DraftEdit(r, curUser, res.names[ri]); if (e) WF_SetN(e, r->blobs[res.blobs[ri]].data, r->blobs[res.blobs[ri]].len); ri++; }
        else { WorkFile *e = DraftEdit(r, curUser, co->names[hi]); if (e) e->deleted = true; hi++; }
    }
    for (int i = 0; i < ncl; i++) {
        WorkFile *e = DraftEdit(r, curUser, clash[i].name);
        if (e) { e->deleted = false; WF_SetN(e, clash[i].text, clash[i].len); }
        free(clash[i].text);
    }
    free(clash);
    SnapFree(&res);
    d = GetDraft(r, curUser, false);
    d->merging = true;
    memcpy(d->mergeSha, ct->sha, 20);
    snprintf(d->mergeName, NAME_LEN, "%s", theirs);
    snprintf(done, (size_t)doneCap, "%d file%s clashes - fix the marked parts, then commit to finish the merge", ncl, ncl == 1 ? " has" : "s have");
    return NULL;
}

/* Gives up on a merge with clashes: back to how your branch was. */
static void AbortMerge(Repo *r)
{
    DraftClear(r, curUser);
}
/* The first of your files that still has conflict markers, or NULL. */
static const char *MarkersLeft(Repo *r)
{
    Draft *d = GetDraft(r, curUser, false);
    for (int i = 0; d && i < d->count; i++)
        if (!d->files[i].deleted && !d->files[i].binary && HasMarkers(d->files[i].text, d->files[i].len)) return d->files[i].name;
    return NULL;
}

/* The strip at the top of a fork's Code tab: where it came from, ahead / behind, and Sync. */
static float DrawForkBar(Repo *r, float top)
{
    if (!r->isFork) return top;
    Rectangle bar = {20, top, (float)W - 40, 46};
    ForkState s = GetForkState(r);
    bool canSync = CanEdit(r) && s.up >= 0 && s.related && s.behind > 0;
    Color edge = canSync ? UI_YELLOW : UI_BORDER;
    DrawRectangleRec(bar, UI_PANEL);
    DrawRectangleLinesEx(bar, 1, edge);

    const char *from = TextFormat("%s / %s", UserName(r->forkOwner), r->forkName);
    const char *msg;
    if (s.up < 0)                 msg = TextFormat("Forked from %s, which has since been deleted.", from);
    else if (!s.related)          msg = TextFormat("Forked from %s, but its history no longer matches this fork.", from);
    else if (!s.ahead && !s.behind) msg = TextFormat("This fork is up to date with %s.", from);
    else if (!s.behind)           msg = TextFormat("This fork is %d commit%s ahead of %s.", s.ahead, s.ahead == 1 ? "" : "s", from);
    else if (!s.ahead)            msg = TextFormat("This fork is %d commit%s behind %s.", s.behind, s.behind == 1 ? "" : "s", from);
    else                          msg = TextFormat("This fork is %d commit%s ahead of and %d behind %s.",
                                                   s.ahead, s.ahead == 1 ? "" : "s", s.behind, from);
    float bw = canSync ? 150 : 0;
    DrawFit(msg, (int)bar.x + 16, (int)bar.y + 13, FS, (int)(bar.width - 32 - bw - 12), canSync ? UI_TEXT : UI_MUTED);

    if (canSync && Button((Rectangle){bar.x + bar.width - 8 - bw, bar.y + 5, bw, 36}, "Sync fork", UI_GREEN)) {
        char done[TEXT_LEN] = "";
        const char *err = SyncFork(r, done, sizeof done);
        if (err) Toast(err);
        else { Toast(done); ForgetScratch(); MarkDirty(); }
    }
    return top + 46 + 12;
}

/* ---------- the Code menu: clone address and Download ZIP ---------- */
static const char *CloneUrl(Repo *r);
static bool      codeMenu;             /* the menu under the Code button is open */
static Rectangle codeBtn;              /* where the Code button is              */

/* The menu is drawn after everything else on the page so it sits on top,
   but it takes its clicks first (before the page under it can). So this is
   called twice a frame: draw = false to handle the mouse, true to draw. */
static void CodeMenu(Repo *r, bool draw)
{
    if (!codeMenu) return;
    float w = 600, h = gitPort ? 236 : 210;
    Rectangle m = {codeBtn.x + codeBtn.width - w, codeBtn.y + codeBtn.height + 6, w, h};
    if (m.x < 20) m.x = 20;
    const char *url = CloneUrl(r);
    Rectangle box  = {m.x + 16, m.y + 48, w - 32 - 96, 40};
    Rectangle copy = {box.x + box.width + 8, box.y, 88, 40};
    Rectangle zip  = {m.x + 16, m.y + h - 56, w - 32, 40};
    Vector2 mouse = GetMousePosition();

    if (!draw) {
        hoverBlock = m; hoverBlocked = true;
        if (!IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) return;
        if (CheckCollisionPointRec(mouse, codeBtn)) return;            /* the button itself toggles it */
        if (!CheckCollisionPointRec(mouse, m)) { codeMenu = false; return; }
        clickUsed = true;                                              /* nothing under the menu gets this click */
        if (gitPort && CheckCollisionPointRec(mouse, copy)) {
            SetClipboardText(url);
            Toast("Copied the address - use it with git clone");
        } else if (CheckCollisionPointRec(mouse, zip)) {
            codeMenu = false;
            DownloadRepo(r);
        }
        return;
    }
    hoverBlocked = false;
    DrawRectangleRounded((Rectangle){m.x + 3, m.y + 5, m.width, m.height}, 0.04f, 6, (Color){0, 0, 0, 110});
    DrawRectangleRounded(m, 0.04f, 6, (Color){22, 27, 34, 255});
    DrawRectangleRoundedLinesEx(m, 0.04f, 6, 1, UI_BORDER);
    DrawBold("Clone with git", (int)m.x + 16, (int)m.y + 14, FS, UI_TEXT);
    if (gitPort) {
        DrawRectangleRec(box, UI_BASE);
        DrawRectangleLinesEx(box, 1, UI_BORDER);
        DrawFit(url, (int)box.x + 10, (int)box.y + 10, FS, (int)box.width - 20, UI_TEXT);
        bool hc = CheckCollisionPointRec(mouse, copy);
        if (hc) wantCursor = MOUSE_CURSOR_POINTING_HAND;
        DrawRectangleRounded(copy, 0.25f, 6, hc ? ColorBrightness(UI_BTN, 0.15f) : UI_BTN);
        DrawText("Copy", (int)(copy.x + (copy.width - MeasureText("Copy", FS)) / 2), (int)copy.y + 10, FS, UI_TEXT);
        DrawFit("Works with git clone, pull and push. When git asks, sign", (int)m.x + 16, (int)m.y + 100, FS, (int)w - 32, UI_MUTED);
        DrawFit("in with your Mini GitHub username and password.", (int)m.x + 16, (int)m.y + 124, FS, (int)w - 32, UI_MUTED);
    } else {
        DrawFit("Mini GitHub's git server isn't running:", (int)m.x + 16, (int)m.y + 50, FS, (int)w - 32, UI_ORANGE);
        DrawFit(gitProblem, (int)m.x + 16, (int)m.y + 76, FS, (int)w - 32, UI_MUTED);
    }
    DrawLine((int)m.x + 1, (int)(zip.y - 14), (int)(m.x + m.width - 1), (int)(zip.y - 14), UI_BORDER);
    bool hz = CheckCollisionPointRec(mouse, zip);
    if (hz) wantCursor = MOUSE_CURSOR_POINTING_HAND;
    DrawRectangleRounded(zip, 0.25f, 6, hz ? ColorBrightness(UI_BTN, 0.15f) : UI_BTN);
    DrawText("Download ZIP", (int)(zip.x + (zip.width - MeasureText("Download ZIP", FS)) / 2), (int)zip.y + 10, FS, UI_TEXT);
}

/* ---------- the branch menu ---------- */
static bool      branchMenu;            /* open?                                   */
static Rectangle branchBtn;             /* the button it hangs from                */
static bool      branchMenuClick;       /* this frame's click landed in the menu   */
static float     scBranchMenu;

/* How far ahead / behind the default branch each branch is (worked out again only when commits change). */
static struct { const Repo *r; unsigned gen; int n; int ahead[MAX_BRANCHES], behind[MAX_BRANCHES]; } bstat;
static void BranchStats(Repo *r)
{
    if (bstat.r == r && bstat.gen == r->gen && bstat.n == r->branchCount) return;
    bstat.r = r; bstat.gen = r->gen; bstat.n = r->branchCount;
    int d = DefaultTip(r);
    for (int b = 0; b < r->branchCount; b++) {
        bstat.ahead[b]  = CountAhead(r, r->branches[b].tip, d);
        bstat.behind[b] = CountAhead(r, d, r->branches[b].tip);
    }
}

static void SwitchBranch(Repo *r, int b)
{
    if (b < 0 || b >= r->branchCount || b == r->curBranch) return;
    r->curBranch = b;
    ForgetScratch();
    scFiles = 0;
    Unfocus();
    Toast(TextFormat("Switched to branch %s", r->branches[b].name));
}

/* A little "branch" picture: two dots joined by a line that forks. */
static void DrawBranchIcon(float x, float y, Color c)
{
    DrawLineEx((Vector2){x, y - 7}, (Vector2){x, y + 7}, 2, c);
    DrawLineBezier((Vector2){x, y + 3}, (Vector2){x + 8, y - 5}, 2, c);
    DrawCircle((int)x, (int)y + 7, 3, c);
    DrawCircle((int)x + 8, (int)y - 6, 3, c);
    DrawCircle((int)x, (int)y - 7, 3, c);
}

/* The button showing the branch you're on; clicking opens the menu. */
static void BranchButton(Repo *r, Rectangle b)
{
    branchBtn = b;
    bool hot = Hover(b);
    if (hot) wantCursor = MOUSE_CURSOR_POINTING_HAND;
    DrawRectangleRounded(b, 0.25f, 6, hot || branchMenu ? ColorBrightness(UI_BTN, 0.15f) : UI_BTN);
    DrawRectangleRoundedLinesEx(b, 0.25f, 6, 1, UI_BORDER);
    DrawBranchIcon(b.x + 14, b.y + b.height / 2, UI_MUTED);
    const char *name = r->branchCount ? CurBranchName(r) : TextFormat("%s (no commits yet)", r->branch);
    DrawFit(name, (int)b.x + 34, (int)(b.y + (b.height - FS) / 2), FS, (int)b.width - 60, UI_TEXT);
    float cx = b.x + b.width - 16, cy = b.y + b.height / 2;
    DrawTriangle((Vector2){cx - 5, cy - 2}, (Vector2){cx, cy + 4}, (Vector2){cx + 5, cy - 2}, UI_MUTED);
    if (Clicked(b)) { branchMenu = !branchMenu; codeMenu = false; scBranchMenu = 0; TF_Clear(&tfBranch); }
}

/* Like the Code menu: called with draw = false early in the frame (it takes
   its clicks before the page under it), and with draw = true at the end. */
static void BranchMenu(Repo *r, bool draw)
{
    if (!branchMenu) { branchMenuClick = false; return; }
    bool edit = CanEdit(r) && r->branchCount;
    float w = 560, rowH = 40, top = branchBtn.y + branchBtn.height + 6;
    float footer = edit ? 98 : 0;
    float listH = r->branchCount * rowH, maxList = (float)H - top - 60 - footer - 50;
    if (maxList < rowH * 3) maxList = rowH * 3;
    if (listH > maxList) listH = maxList;
    if (!r->branchCount) listH = 64;
    Rectangle m = {branchBtn.x, top, w, 50 + listH + footer + 8};
    if (m.x + m.width > W - 20) m.x = W - 20 - m.width;
    Vector2 mouse = GetMousePosition();

    if (!draw) {
        branchMenuClick = false;
        hoverBlock = m; hoverBlocked = true;
        if (!IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) return;
        if (CheckCollisionPointRec(mouse, branchBtn)) return;
        if (!CheckCollisionPointRec(mouse, m)) { branchMenu = false; return; }
        branchMenuClick = true;
        clickUsed = true;                                    /* the page under the menu doesn't get it */
        return;
    }
    clickUsed = !branchMenuClick;                            /* the menu's own buttons can use this click */
    hoverBlocked = false;
    DrawRectangleRounded((Rectangle){m.x + 3, m.y + 5, m.width, m.height}, 0.03f, 6, (Color){0, 0, 0, 110});
    DrawRectangleRounded(m, 0.03f, 6, (Color){22, 27, 34, 255});
    DrawRectangleRoundedLinesEx(m, 0.03f, 6, 1, UI_BORDER);
    DrawBold("Branches", (int)m.x + 16, (int)m.y + 14, FS, UI_TEXT);
    if (r->branchCount > 1) {
        const char *cmp = TextFormat("ahead / behind %s", DefBranchName(r));
        DrawText(cmp, (int)(m.x + m.width - 16 - MeasureText(cmp, FS)), (int)m.y + 14, FS, UI_MUTED);
    }
    Rectangle la = {m.x + 1, m.y + 46, m.width - 2, listH};
    if (!r->branchCount) {
        DrawText("No branches yet - the first commit makes one.", (int)la.x + 15, (int)la.y + 12, FS, UI_MUTED);
    } else {
        BranchStats(r);
        ScrollArea(la, &scBranchMenu, r->branchCount * rowH);
        /* default first, then the most recently changed */
        int order[MAX_BRANCHES], n = r->branchCount;
        for (int i = 0; i < n; i++) order[i] = i;
        for (int i = 0; i < n; i++)
            for (int j = i + 1; j < n; j++) {
                int a = order[i], b = order[j];
                bool swap = b == r->defBranch || (a != r->defBranch && r->commits[r->branches[b].tip].time > r->commits[r->branches[a].tip].time);
                if (swap) { order[i] = b; order[j] = a; }
            }
        BeginScissorMode((int)la.x, (int)la.y, (int)la.width, (int)la.height);
        int act = -1, actKind = 0;                           /* what got clicked: 1 switch, 2 merge, 3 default, 4 delete */
        for (int i = 0; i < n; i++) {
            int b = order[i];
            Rectangle row = {la.x, la.y + i * rowH - scBranchMenu, la.width - 12, rowH};
            if (row.y + rowH < la.y || row.y > la.y + la.height) continue;
            bool hot = CheckCollisionPointRec(mouse, row) && CheckCollisionPointRec(mouse, la);
            bool cur = b == r->curBranch;
            if (hot) DrawRectangleRec(row, UI_HOVER);
            if (cur) DrawRectangle((int)row.x, (int)row.y + 6, 3, (int)rowH - 12, uiAccent);
            DrawBranchIcon(row.x + 18, row.y + rowH / 2, cur ? UI_TEXT : UI_MUTED);
            int nx = (int)row.x + 38;
            const char *name = r->branches[b].name;
            float right = row.x + row.width - 12;
            /* buttons when hovering a row you can act on */
            float bx = right;
            if (hot && edit) {
                if (b != r->defBranch) {
                    Rectangle del = {bx - 76, row.y + 6, 76, 28};
                    if (ConfirmButton(del, "Delete", "Sure?", 40 + b)) { act = b; actKind = 4; }
                    bx -= 84;
                }
                if (IsMine(r) && b != r->defBranch) {
                    Rectangle df = {bx - 92, row.y + 6, 92, 28};
                    if (ButtonEx(df, "Default", UI_BTN, true)) { act = b; actKind = 3; }
                    bx -= 100;
                }
                if (!cur) {
                    const char *ml = TextFormat("Merge into %s", CurBranchName(r));
                    float mw = (float)MeasureText(ml, FS) + 24;
                    if (mw > 230) { ml = "Merge in here"; mw = (float)MeasureText(ml, FS) + 24; }
                    Rectangle mb = {bx - mw, row.y + 6, mw, 28};
                    if (ButtonEx(mb, ml, UI_GREEN, true)) { act = b; actKind = 2; }
                    bx -= mw + 8;
                }
            } else if (b != r->defBranch) {
                const char *ab = !bstat.ahead[b] && !bstat.behind[b] ? "same as default"
                               : TextFormat("%d ahead  |  %d behind", bstat.ahead[b], bstat.behind[b]);
                DrawText(ab, (int)(right - MeasureText(ab, FS)), (int)row.y + 10, FS, UI_MUTED);
                bx = right - MeasureText(ab, FS) - 12;
            }
            int maxW = (int)(bx - nx - (b == r->defBranch ? 90 : 10));
            if (cur) DrawBold(TextFormat("%s", name), nx, (int)row.y + 10, FS, UI_TEXT);
            DrawFit(name, nx, (int)row.y + 10, FS, maxW, cur ? UI_TEXT : UI_LINK);
            if (b == r->defBranch) {
                int lw = MeasureText(name, FS); if (lw > maxW) lw = maxW;
                Rectangle pill = {(float)(nx + lw + 10), row.y + 9, (float)MeasureText("default", FS) + 16, 23};
                DrawRectangleRounded(pill, 0.5f, 6, UI_BTN);
                DrawText("default", (int)pill.x + 8, (int)pill.y + 2, FS, UI_MUTED);
            }
            if (hot) wantCursor = MOUSE_CURSOR_POINTING_HAND;
            if (hot && act < 0 && Clicked(row)) { act = b; actKind = 1; }
        }
        EndScissorMode();
        DrawScrollbar(la, scBranchMenu, r->branchCount * rowH);
        if (actKind == 1) { SwitchBranch(r, act); branchMenu = false; }
        else if (actKind == 3) { r->defBranch = act; r->gen++; MarkDirty(); Toast(TextFormat("%s is now the default branch", r->branches[act].name)); }
        else if (actKind == 4) {
            char nm[NAME_LEN];
            snprintf(nm, sizeof nm, "%s", r->branches[act].name);
            const char *err = DeleteBranch(r, act);
            Toast(err ? err : TextFormat("Deleted branch %s", nm));
            if (!err) { ForgetScratch(); MarkDirty(); }
        } else if (actKind == 2) {
            char done[TEXT_LEN * 2] = "";
            const char *err = MergeBranch(r, act, done, sizeof done);
            branchMenu = false;
            if (err) Toast(err);
            else {
                Toast(done);
                ForgetScratch();
                MarkDirty();
                Draft *d = GetDraft(r, curUser, false);
                if (d && d->merging) {                        /* clashes: off to the Code tab to fix them */
                    snprintf(tfCommit.buf, TEXT_LEN, "Merge branch '%s' into %s", d->mergeName, CurBranchName(r));
                    tfCommit.len = (int)strlen(tfCommit.buf);
                    tab = TAB_CODE; screen = SCR_REPO;
                    const char *first = MarkersLeft(r);
                    if (first) SelectFileName(first);
                }
            }
        }
    }
    /* make a new branch from the one you're on */
    if (edit && branchMenu) {
        float fy = m.y + 46 + listH + 10;
        DrawLine((int)m.x + 1, (int)fy - 4, (int)(m.x + m.width - 1), (int)fy - 4, UI_BORDER);
        DrawText(TextFormat("New branch, starting from %s:", CurBranchName(r)), (int)m.x + 16, (int)fy + 6, FS, UI_MUTED);
        bool enter = TextFieldUI(&tfBranch, (Rectangle){m.x + 16, fy + 36, w - 32 - 170, 40}, "e.g. feature/dark-mode");
        if (Button((Rectangle){m.x + w - 16 - 160, fy + 36, 160, 40}, "Create branch", UI_GREEN) || enter) {
            char nm[NAME_LEN];
            snprintf(nm, sizeof nm, "%.*s", NAME_LEN - 1, tfBranch.buf);
            for (char *c = nm; *c; c++) if (*c == ' ') *c = '-';
            const char *err = CreateBranch(r, nm);
            if (err) Toast(err);
            else { Toast(TextFormat("Made branch %s - you're on it now", nm)); TF_Clear(&tfBranch); branchMenu = false; ForgetScratch(); MarkDirty(); }
        }
    }
    if (branchMenuClick) {                                   /* only the menu's own box keeps focus */
        bool keep = tfBranch.active;
        Unfocus();
        tfBranch.active = keep;
    }
    clickUsed = true;
}

/* While a merge with clashes is being finished: what's left to do, and a way out. */
static float DrawMergeBanner(Repo *r, Rectangle bar)
{
    Draft *d = GetDraft(r, curUser, false);
    if (!d || !d->merging) return 0;
    const char *left = MarkersLeft(r);
    int nLeft = 0;
    for (int i = 0; i < d->count; i++)
        if (!d->files[i].deleted && !d->files[i].binary && HasMarkers(d->files[i].text, d->files[i].len)) nLeft++;
    Color edge = nLeft ? UI_ORANGE : UI_GREENT;
    DrawRectangleRec(bar, nLeft ? (Color){74, 40, 18, 255} : (Color){18, 52, 30, 255});
    DrawRectangleLinesEx(bar, 1, edge);
    const char *msg = nLeft ? TextFormat("Merging %s into %s: %d file%s to fix, then commit", d->mergeName, CurBranchName(r),
                                         nLeft, nLeft == 1 ? "" : "s")
                            : TextFormat("Merging %s into %s: all fixed - commit to finish", d->mergeName, CurBranchName(r));
    float bw = 150, nw = nLeft ? 130 : 0;
    DrawFit(msg, (int)bar.x + 14, (int)bar.y + 10, FS, (int)(bar.width - 28 - bw - nw - 16), UI_TEXT);
    if (nLeft && Button((Rectangle){bar.x + bar.width - 8 - bw - 8 - nw, bar.y + 4, nw, bar.height - 8}, "Next clash", UI_BTN)) {
        /* the next clashing file after the open one (round to the start) */
        const char *pick = left;
        bool after = false;
        for (int i = 0; i < d->count; i++) {
            WorkFile *e = &d->files[i];
            if (e->deleted || e->binary || !HasMarkers(e->text, e->len)) continue;
            if (after) { pick = e->name; break; }
            if (strcmp(e->name, selName) == 0) after = true;
        }
        SelectFileName(pick);
    }
    if (ConfirmButton((Rectangle){bar.x + bar.width - 8 - bw, bar.y + 4, bw, bar.height - 8}, "Abort merge", "Sure? Undo it", 60)) {
        AbortMerge(r);
        TF_Clear(&tfCommit);
        ForgetScratch();
        MarkDirty();
        Toast("Merge aborted - your branch is as it was");
    }
    return bar.height;
}

static bool DrawRepoHeader(Repo *r, int activeTab)
{
    CodeMenu(r, false);                    /* the open menus take their clicks first */
    BranchMenu(r, false);
    int x = 20, y = 72;
    const char *owner = UserName(r->owner);
    int w1 = MeasureText(owner, 30), w2 = MeasureText(" / ", 30), w3 = MeasureText(r->name, 30);
    Rectangle ownerHit = {(float)x, (float)y, (float)w1, 30};
    if (Hover(ownerHit)) wantCursor = MOUSE_CURSOR_POINTING_HAND;
    DrawText(owner, x, y, 30, UI_LINK);
    DrawText(" / ", x + w1, y, 30, UI_MUTED);
    DrawBold(r->name, x + w1 + w2, y, 30, UI_TEXT);
    bool mine = IsMine(r);
    const char *badge = mine ? "Yours" : IsCollab(r, curUser) ? "Collaborator" : "Read-only";
    Rectangle pill = {(float)(x + w1 + w2 + w3 + 16), (float)y + 2, (float)MeasureText(badge, FS) + 20, 26};
    DrawRectangleRounded(pill, 0.5f, 6, UI_BTN);
    DrawText(badge, (int)pill.x + 10, (int)pill.y + 3, FS, UI_MUTED);
    if (Clicked(ownerHit)) { OpenProfile(r->owner); return true; }

    /* next to the badge: how many people have forked this */
    int forks = ForkCount(r), ix = (int)(pill.x + pill.width + 16);
    const char *extra = TextFormat("%d branch%s%s", r->branchCount, r->branchCount == 1 ? "" : "es",
                                   forks ? TextFormat("   %d fork%s", forks, forks == 1 ? "" : "s") : "");
    if (r->branchCount && W - 570 - ix > MeasureText(extra, FS)) DrawText(extra, ix, y + 5, FS, UI_MUTED);

    /* under the name: "forked from owner / name" (click to open the original), then the description */
    int dx = x, dmax = W - 400;
    if (r->isFork) {
        int up = Upstream(r);
        const char *pre = "forked from ";
        const char *src = TextFormat("%s / %s", UserName(r->forkOwner), r->forkName);
        int pw = MeasureText(pre, FS), sw = MeasureText(src, FS);
        DrawText(pre, dx, y + 40, FS, UI_MUTED);
        DrawText(src, dx + pw, y + 40, FS, up >= 0 ? UI_LINK : UI_MUTED);
        Rectangle hit = {(float)(dx + pw), (float)y + 36, (float)sw, FS + 8};
        if (up >= 0 && Hover(hit)) { wantCursor = MOUSE_CURSOR_POINTING_HAND; DrawLine((int)hit.x, y + 41 + FS, (int)(hit.x + hit.width), y + 41 + FS, UI_LINK); }
        if (up >= 0 && Clicked(hit)) { OpenRepo(up); return true; }
        const char *gap = "   |   ";
        DrawText(gap, dx + pw + sw, y + 40, FS, UI_MUTED);
        dx += pw + sw + MeasureText(gap, FS);
        dmax -= dx - x;
    }
    if (dmax > 40) DrawFit(r->desc[0] ? r->desc : "No description", dx, y + 40, FS, dmax, UI_MUTED);

    bool starred = HasStarred(r, curUser);
    if (Button((Rectangle){(float)W - 170, (float)y, 150, 38},
               TextFormat("%s  %d", starred ? "Starred" : "Star", r->starCount),
               starred ? (Color){110, 80, 20, 255} : UI_BTN)) {
        ToggleStar(r, curUser);
        MarkDirty();
    }
    codeBtn = (Rectangle){(float)W - (mine ? 550 : 360), (float)y, 180, 38};
    if (Button(codeBtn, codeMenu ? "Code  ^" : "Code  v", UI_GREEN)) { codeMenu = !codeMenu; branchMenu = false; }
    if (!mine) {                       /* you can't fork your own repository - you already own it */
        int myFork = FindForkOf(curUser, curRepo);
        if (Button((Rectangle){(float)W - 550, (float)y, 180, 38},
                   myFork >= 0 ? "Your fork" : TextFormat("Fork  %d", ForkCount(r)), UI_BTN)) {
            TryForkRepo(curRepo);
            return true;
        }
    }
    if (mine && ConfirmButton((Rectangle){(float)W - 360, (float)y, 180, 38}, "Delete repo", "Really delete?", 1)) {
        int i = curRepo;
        GoHome();
        DeleteRepo(i);
        MarkDirty();
        Toast("Repository deleted");
        return true;
    }

    if (HeaderPie(r, (float)W - 20, 150)) { Unfocus(); tab = TAB_PEOPLE; screen = SCR_REPO; }

    int tx = 20;
    for (int t = 0; t < 4; t++) {
        const char *label = t == 0 ? TextFormat("Code  %d", ShownCount(r))
                          : t == 1 ? TextFormat("Commits  %d", r->commitCount)
                          : t == 2 ? TextFormat("Issues  %d", OpenIssues(r))
                                   : TextFormat("People  %d", 1 + r->collabCount);
        int w = MeasureText(label, FS) + 28;
        Rectangle tr = {(float)tx, 132, (float)w, 38};
        bool on = (t == activeTab);
        if (Hover(tr)) {
            wantCursor = MOUSE_CURSOR_POINTING_HAND;
            if (!on) DrawRectangleRounded((Rectangle){(float)tx, 134, (float)w, 30}, 0.3f, 6, UI_HOVER);
        }
        DrawText(label, tx + 14, 140, FS, on ? UI_TEXT : UI_MUTED);
        if (on) DrawRectangle(tx, 167, w, 3, uiAccent);
        if (Clicked(tr)) { Unfocus(); tab = (Tab)t; screen = SCR_REPO; }
        tx += w + 6;
    }
    DrawLine(0, 170, W, 170, UI_BORDER);
    return false;
}

/* ---------- the file list, as folders ----------
   Paths like "src/ui/button.c" are shown as a tree: a "src" folder with a
   "ui" folder inside. Folders start closed (big projects stay tidy) and
   open with a click; the folders holding the open file, and any folder
   with changes of yours in it, are opened for you. */
static const Repo  *openForRepo;
static const char  *openFolders[512];
static int          openFolderCount;

static int FolderOpenIndex(const char *path)
{
    for (int i = 0; i < openFolderCount; i++) if (openFolders[i] == path) return i;
    return -1;
}
static void OpenFolder(const char *path)
{
    if (path && FolderOpenIndex(path) < 0 && openFolderCount < 512) openFolders[openFolderCount++] = path;
}
static void ToggleFolder(const char *path)
{
    int i = FolderOpenIndex(path);
    if (i >= 0) openFolders[i] = openFolders[--openFolderCount];
    else OpenFolder(path);
}
/* Opens every folder above a file, e.g. "src" and "src/ui" for "src/ui/button.c". */
static void RevealPath(const char *name)
{
    char buf[PATH_LEN];
    snprintf(buf, sizeof buf, "%s", name);
    for (char *s = strchr(buf, '/'); s; s = strchr(s + 1, '/')) { *s = 0; OpenFolder(Intern(buf)); *s = '/'; }
}

typedef struct { const char *name; int nameLen; int depth; int vi; bool folder; const char *path; int end; char status; } FileRow;
static FileRow *fileRows;
static int      fileRowCount, fileRowCap;

/* Turns the (sorted) view into rows: a row for each folder, then its contents. */
static void BuildFileRows(void)
{
    fileRowCount = 0;
    int stack[64], depth = 0;                        /* rows of the folders we're inside */
    char buf[PATH_LEN];
    for (int i = 0; i <= viewCount; i++) {
        const char *p = i < viewCount ? view[i].name : "";
        /* how many of the current folders this path is still inside */
        int keep = 0;
        while (keep < depth) {
            FileRow *f = &fileRows[stack[keep]];
            size_t pl = strlen(f->path);
            if (i < viewCount && strncmp(p, f->path, pl) == 0 && p[pl] == '/') keep++;
            else break;
        }
        while (depth > keep) fileRows[stack[--depth]].end = fileRowCount;
        if (i == viewCount) break;
        /* new folders on the way to this file */
        const char *seg = p;
        for (int d = 0; d < keep; d++) seg = strchr(seg, '/') + 1;
        for (const char *sl = strchr(seg, '/'); sl && depth < 64; sl = strchr(seg, '/')) {
            fileRows = GrowArr(fileRows, &fileRowCap, fileRowCount + 2, sizeof(FileRow));
            if (fileRowCount + 2 > fileRowCap) return;
            snprintf(buf, sizeof buf, "%.*s", (int)(sl - p), p);
            FileRow *f = &fileRows[fileRowCount];
            memset(f, 0, sizeof *f);
            f->name = seg; f->nameLen = (int)(sl - seg); f->depth = depth; f->vi = i; f->folder = true;
            f->path = Intern(buf);
            if (!f->path) return;
            stack[depth++] = fileRowCount++;
            seg = sl + 1;
        }
        fileRows = GrowArr(fileRows, &fileRowCap, fileRowCount + 1, sizeof(FileRow));
        if (fileRowCount + 1 > fileRowCap) return;
        FileRow *f = &fileRows[fileRowCount++];
        memset(f, 0, sizeof *f);
        f->name = seg; f->nameLen = (int)strlen(seg); f->depth = depth; f->vi = i;
        /* folders show the "strongest" change inside them: conflict, then any change */
        char st = view[i].status;
        for (int d = 0; d < depth && st; d++) {
            FileRow *fo = &fileRows[stack[d]];
            if (fo->status != 'C') fo->status = st == 'C' ? 'C' : 'M';
        }
    }
}

/* File list on the left: the latest commit, plus your own changes if you can edit. */
static void DrawFileList(Repo *r, Rectangle la)
{
    bool edit = CanEdit(r);
    BuildView(r, edit ? curUser : -1);
    if (openForRepo != r) { openForRepo = r; openFolderCount = 0; }
    BuildFileRows();
    for (int i = 0; i < fileRowCount; i++)           /* open folders with your changes in them */
        if (fileRows[i].folder && fileRows[i].status) OpenFolder(fileRows[i].path);
    if (selName[0]) RevealPath(selName);

    /* which rows show: everything inside a closed folder is hidden */
    static int *shown;
    static int shownCap;
    shown = GrowArr(shown, &shownCap, fileRowCount + 1, sizeof(int));
    int n = 0;
    for (int i = 0; i < fileRowCount && shownCap > fileRowCount; i++) {
        shown[n++] = i;
        if (fileRows[i].folder && FolderOpenIndex(fileRows[i].path) < 0) i = fileRows[i].end - 1;
    }

    float rowH = 30;
    ScrollArea(la, &scFiles, n * rowH);
    if (!viewCount) DrawText(edit ? "No files yet" : "Nothing committed yet", (int)la.x + 14, (int)la.y + 6, FS, UI_MUTED);
    BeginScissorMode((int)la.x, (int)la.y, (int)la.width, (int)la.height);
    for (int k = 0; k < n; k++) {
        FileRow *f = &fileRows[shown[k]];
        Rectangle row = {la.x, la.y + k * rowH - scFiles, la.width, rowH};
        if (row.y > la.y + la.height || row.y + rowH < la.y) continue;
        bool hot = Hover(la) && Hover(row);
        int x = (int)row.x + 10 + f->depth * 14;
        const char *label = TextFormat("%.*s", f->nameLen, f->name);
        if (f->folder) {
            bool open = FolderOpenIndex(f->path) >= 0;
            if (hot) { DrawRectangleRec(row, UI_HOVER); wantCursor = MOUSE_CURSOR_POINTING_HAND; }
            float cx = (float)x + 7, cy = row.y + 15;
            if (open) DrawTriangle((Vector2){cx - 5, cy - 3}, (Vector2){cx, cy + 4}, (Vector2){cx + 5, cy - 3}, UI_MUTED);
            else      DrawTriangle((Vector2){cx - 3, cy - 5}, (Vector2){cx - 3, cy + 5}, (Vector2){cx + 4, cy}, UI_MUTED);
            DrawFit(label, x + 20, (int)row.y + 5, FS, (int)(row.x + row.width) - x - 56, UI_TEXT);
            if (f->status) DrawCircle((int)(row.x + row.width - 22), (int)row.y + 15, 4, f->status == 'C' ? UI_ORANGE : UI_YELLOW);
            if (Hover(la) && Clicked(row)) ToggleFolder(f->path);
            continue;
        }
        ViewFile *v = &view[f->vi];
        if (strcmp(v->name, selName) == 0) DrawRectangleRec(row, (Color){56, 139, 253, 40});
        else if (hot)                       DrawRectangleRec(row, UI_HOVER);
        if (hot) wantCursor = MOUSE_CURSOR_POINTING_HAND;
        bool gone = ViewDeleted(v);
        int fl = gone ? LANG_TEXT : ViewLang(r, v, NULL);
        if (fl >= 0 && fl < LANG_FIRST_NONCODE) DrawCircle(x + 7, (int)row.y + 15, 5, LangColor(fl));
        else DrawCircleLines(x + 7, (int)row.y + 15, 4, UI_BORDER);         /* not code */
        int maxW = (int)(row.x + row.width) - x - 60;
        DrawFit(label, x + 20, (int)row.y + 5, FS, maxW, gone ? UI_MUTED : UI_TEXT);
        if (gone) { int lw = MeasureText(label, FS); DrawLine(x + 20, (int)row.y + 15, x + 20 + (lw < maxW ? lw : maxW), (int)row.y + 15, UI_MUTED); }
        if (v->status) {
            const char *t = v->status == 'C' ? "!" : TextFormat("%c", v->status);
            Color c = v->status == 'A' ? UI_GREENT : v->status == 'M' ? UI_YELLOW : v->status == 'D' ? UI_REDT : UI_ORANGE;
            DrawBold(t, (int)(row.x + row.width - 28), (int)row.y + 5, FS, c);
        }
        if (hot && MeasureText(label, FS) > maxW) SetTip(v->name, NULL, NULL);
        if (Hover(la) && Clicked(row)) SelectFileName(v->name);
    }
    EndScissorMode();
    DrawScrollbar(la, scFiles, n * rowH);
}

/* ------------------------------------------------------------------ */
/* Uploading files                                                    */
/*                                                                    */
/* Files dropped onto the window (or picked by typing their path) are */
/* "staged": held in memory and listed so you can check them first.   */
/* "Commit changes" then saves them all as one commit, built on top   */
/* of the latest commit. Anything else you were in the middle of      */
/* editing stays as it was, still waiting for its own commit.         */
/* ------------------------------------------------------------------ */
#define MAX_STAGED 200
typedef struct { char name[PATH_LEN]; char *data; int len; bool binary; int lang; } Staged;
static Staged staged[MAX_STAGED];
static int    stagedCount;
static float  scUpload;

static void ClearStaged(void)
{
    for (int i = 0; i < stagedCount; i++) free(staged[i].data);
    memset(staged, 0, sizeof staged);
    stagedCount = 0;
}

static int FindStaged(const char *name)
{
    for (int i = 0; i < stagedCount; i++) if (strcmp(staged[i].name, name) == 0) return i;
    return -1;
}

/* "512 bytes", "12.3 KB", "4.5 MB" */
static const char *FormatSize(int n)
{
    static char b[4][32];
    static int w;
    w = (w + 1) % 4;
    if (n < 1024)             snprintf(b[w], 32, "%d byte%s", n, n == 1 ? "" : "s");
    else if (n < 1024 * 1024) snprintf(b[w], 32, "%.1f KB", n / 1024.0);
    else                      snprintf(b[w], 32, "%.1f MB", n / (1024.0 * 1024.0));
    return b[w];
}

static int CountLines(const char *d, int len)
{
    if (len == 0) return 0;
    int n = 0;
    for (int i = 0; i < len; i++) if (d[i] == '\n') n++;
    return d[len - 1] == '\n' ? n : n + 1;
}

/* Removes quotes and spaces around a typed or pasted path
   ("Copy as path" on Windows adds quotes). */
static void CleanPath(const char *raw, char *path, int cap)
{
    while (*raw == ' ' || *raw == '"' || *raw == '\'') raw++;
    snprintf(path, (size_t)cap, "%s", raw);
    int n = (int)strlen(path);
    while (n > 0 && (path[n - 1] == ' ' || path[n - 1] == '"' || path[n - 1] == '\'')) path[--n] = 0;
}

/* Reads one file from your computer into the staging list.
   Returns NULL if it worked, or a message saying why not. */
static const char *StageFile(const char *raw)
{
    static char msg[TEXT_LEN];
    char path[1024];
    CleanPath(raw, path, sizeof path);
    if (!path[0])           return "Type or paste a file's path first";
    if (DirectoryExists(path)) return "Folders can't be uploaded - open the folder and drag the files inside";
    if (!FileExists(path))  return "Couldn't find a file at that path";
    const char *base = GetFileName(path);
    int size = GetFileLength(path);
    if (size < 0 || size > MAX_UPLOAD) {
        snprintf(msg, sizeof msg, "%.60s is too big - files can be up to %s", base, FormatSize(MAX_UPLOAD));
        return msg;
    }

    char nm[NAME_LEN];
    SanitizeName(base, nm);
    if (!nm[0]) snprintf(nm, NAME_LEN, "file");
    int k = FindStaged(nm);
    if (k < 0 && stagedCount >= MAX_STAGED) return "That's as many files as one upload can hold";

    char *data = NULL;
    int len = 0;
    if (size == 0) data = calloc(1, 1);                          /* an empty file */
    else {
        int got = 0;
        unsigned char *d = LoadFileData(path, &got);
        if (!d || got <= 0) { snprintf(msg, sizeof msg, "Couldn't open %.60s", base); return msg; }
        data = malloc((size_t)got + 1);
        if (data) { memcpy(data, d, (size_t)got); data[got] = 0; len = got; }
        UnloadFileData(d);
    }
    if (!data) return "Out of memory";

    bool bin = LooksBinary(data, len);      /* kept exactly as it is, byte for byte, like git does */
    if (k < 0) k = stagedCount++;                                /* same name again: replace it */
    else free(staged[k].data);
    snprintf(staged[k].name, PATH_LEN, "%s", nm);
    staged[k].data = data;
    staged[k].len = len;
    staged[k].binary = bin;
    LangCache lc;
    FillLangCache(&lc, data, len, bin);
    staged[k].lang = LangOf(&lc, nm);
    return NULL;
}

/* 'A' = a new file, 'M' = replaces a different version, '=' = same as the latest commit */
static char StagedKind(Repo *r, Staged *s)
{
    Commit *h = Head(r);
    int k = h ? FindInCommit(h, s->name) : -1;
    if (k < 0) return 'A';
    Blob *b = &r->blobs[h->blobs[k]];
    return (b->len == s->len && memcmp(b->data, s->data, (size_t)s->len) == 0) ? '=' : 'M';
}

/* True if you've edited this file since the last commit and the upload would replace those edits. */
static bool StagedReplacesEdits(Repo *r, Staged *s)
{
    WorkFile *f = DraftFind(r, curUser, s->name);
    if (!f || !DraftStatus(r, f)) return false;
    return f->deleted || !(f->len == s->len && memcmp(f->text, s->data, (size_t)s->len) == 0);
}

/* Saves the staged files as one commit. Returns NULL or a message. */
static const char *DoUploadCommit(Repo *r, const char *msg)
{
    if (!stagedCount) return "Add some files first";
    Commit *h = Head(r);
    int changed = 0, newInCommit = 0;
    for (int i = 0; i < stagedCount; i++) {
        char kd = StagedKind(r, &staged[i]);
        if (kd != '=') changed++;
        if (kd == 'A') newInCommit++;
    }
    if (!changed)                      return "Nothing to commit - these are the same as the latest versions";
    if (r->commitCount >= MAX_COMMITS) return "This repository has reached its commit limit";
    if ((h ? h->fileCount : 0) + newInCommit > MAX_FILES)
        return TextFormat("A repository can hold up to %d files", MAX_FILES);

    /* start from the latest commit, then put the uploaded files in */
    Snap c;
    if (!SnapFrom(&c, h)) return "Out of memory";
    for (int i = 0; i < stagedCount; i++) {
        Staged *s = &staged[i];
        if (StagedKind(r, s) == '=') continue;
        int b = InternBlob(r, s->data, s->len);
        if (b < 0 || !SnapSet(&c, s->name, b, -1)) { SnapFree(&c); return "This repository is out of storage space"; }
    }
    const char *clash = PathClash(c.names, c.n);
    if (clash) { SnapFree(&c); return TextFormat("There's already a folder called %s", clash); }
    if (!FinishCommit(r, &c, msg[0] ? msg : "Add files via upload", -1)) return "Couldn't save the commit (out of memory?)";
    MarkDirty();

    /* the uploaded versions replace any uncommitted edits you had to those files */
    for (int i = 0; i < stagedCount; i++) DraftDropName(r, curUser, staged[i].name);
    return NULL;
}

/* ---------------- previews of pictures and other non-text files ---------------- */

/* Up to 4 pictures are kept ready to draw; the one used longest ago is replaced first. */
#define PREVIEW_SLOTS 4
static struct {
    const char *data; int len, gen, state, w, h;     /* state: 1 = ready, -1 = couldn't be read */
    Texture2D tex;
    unsigned long long used;
} prevs[PREVIEW_SLOTS];
static unsigned long long prevClock;

static bool IsPictureBytes(const char *d, int n)
{
    const unsigned char *u = (const unsigned char *)d;
    return (n > 8 && memcmp(d, "\x89PNG", 4) == 0) || (n > 3 && u[0] == 0xFF && u[1] == 0xD8) ||
           (n > 6 && memcmp(d, "GIF8", 4) == 0)    || (n > 4 && memcmp(d, "qoif", 4) == 0);
}

/* Returns which slot holds this picture, or -1 if it isn't a picture we can show. */
static int Preview(const char *data, int len)
{
    if (!IsPictureBytes(data, len)) return -1;
    prevClock++;
    for (int i = 0; i < PREVIEW_SLOTS; i++)
        if (prevs[i].state && prevs[i].data == data && prevs[i].len == len && prevs[i].gen == filesGen) {
            prevs[i].used = prevClock;
            return prevs[i].state == 1 ? i : -1;
        }
    int s = 0;
    for (int i = 1; i < PREVIEW_SLOTS; i++) if (prevs[i].used < prevs[s].used) s = i;
    if (prevs[s].state == 1) UnloadTexture(prevs[s].tex);
    memset(&prevs[s], 0, sizeof prevs[s]);
    prevs[s].data = data; prevs[s].len = len; prevs[s].gen = filesGen; prevs[s].used = prevClock;
    prevs[s].state = -1;

    const char *err = NULL;
    Image img = LoadAnyImage((const unsigned char *)data, len, &err);
    if (err) return -1;
    prevs[s].w = img.width;
    prevs[s].h = img.height;
    int big = img.width > img.height ? img.width : img.height;
    if (big > 2048) ImageResize(&img, img.width * 2048 / big, img.height * 2048 / big);   /* keeps graphics cards happy */
    prevs[s].tex = LoadTextureFromImage(img);
    UnloadImage(img);
    SetTextureFilter(prevs[s].tex, TEXTURE_FILTER_BILINEAR);
    prevs[s].state = 1;
    return s;
}

/* Draws slot s's picture as big as fits in box (small pictures up to 3 times bigger),
   on a chequered background so see-through parts are visible. */
static void DrawPicture(int s, Rectangle box)
{
    Texture2D t = prevs[s].tex;
    float sc = fminf(box.width / t.width, box.height / t.height);
    float most = (t.width < 200 && t.height < 200) ? 3.0f : 1.0f;
    if (sc > most) sc = most;
    float w = t.width * sc, h = t.height * sc;
    Rectangle dst = {box.x + (box.width - w) / 2, box.y + (box.height - h) / 2, w, h};
    BeginScissorMode((int)dst.x, (int)dst.y, (int)dst.width + 1, (int)dst.height + 1);
    for (int y = 0; y * 12 < dst.height; y++)
        for (int x = 0; x * 12 < dst.width; x++)
            DrawRectangle((int)dst.x + x * 12, (int)dst.y + y * 12, 12, 12,
                          (x + y) % 2 ? (Color){60, 66, 74, 255} : (Color){44, 49, 56, 255});
    EndScissorMode();
    DrawTexturePro(t, (Rectangle){0, 0, (float)t.width, (float)t.height}, dst, (Vector2){0, 0}, 0, WHITE);
}

/* What the Code tab shows instead of the editor for a non-text file. */
static void DrawBinaryView(const char *data, int len, Rectangle area)
{
    DrawRectangleRec(area, UI_BASE);
    int s = Preview(data, len);
    if (s >= 0) {
        DrawPicture(s, (Rectangle){area.x + 20, area.y + 20, area.width - 40, area.height - 60});
        const char *cap = TextFormat("%d x %d pixels   |   %s", prevs[s].w, prevs[s].h, FormatSize(len));
        DrawText(cap, (int)(area.x + area.width / 2) - MeasureText(cap, FS) / 2, (int)(area.y + area.height) - 32, FS, UI_MUTED);
        return;
    }
    /* a simple "document" shape with a folded corner */
    float cx = area.x + area.width / 2, cy = area.y + area.height / 2 - 50;
    Rectangle doc = {cx - 30, cy - 38, 60, 76};
    DrawRectangleLinesEx(doc, 2, UI_MUTED);
    DrawTriangle((Vector2){doc.x + 40, doc.y}, (Vector2){doc.x + 60, doc.y + 20}, (Vector2){doc.x + 60, doc.y}, UI_BASE);
    DrawLineEx((Vector2){doc.x + 40, doc.y}, (Vector2){doc.x + 60, doc.y + 20}, 2, UI_MUTED);
    DrawText("01", (int)cx - MeasureText("01", FS) / 2, (int)cy - 4, FS, UI_MUTED);
    const char *l1 = IsPictureBytes(data, len) ? TextFormat("Picture   |   %s", FormatSize(len))
                                               : TextFormat("Binary file   |   %s", FormatSize(len));
    const char *l2 = IsPictureBytes(data, len) ? "This kind of picture can't be previewed, but it's stored safely."
                                               : "This isn't a text file, so it can't be shown here - but it's stored safely.";
    DrawText(l1, (int)cx - MeasureText(l1, FS) / 2, (int)cy + 56, FS, UI_TEXT);
    DrawFit(l2, (int)fmaxf(area.x + 16, cx - MeasureText(l2, FS) / 2.0f), (int)cy + 86, FS, (int)area.width - 32, UI_MUTED);
}

/* ---------------- the upload page ---------------- */
static void OpenUpload(void)
{
    Unfocus();
    scUpload = 0;
    TF_Clear(&tfUpPath);
    TF_Clear(&tfUpMsg);
    screen = SCR_UPLOAD;
}

/* Stages every path in a list (from a drop), and says how it went. */
static void StagePaths(char **paths, int count)
{
    int ok = 0;
    char err[TEXT_LEN] = "";
    for (int i = 0; i < count; i++) {
        const char *e = StageFile(paths[i]);
        if (e) snprintf(err, sizeof err, "%s", e);
        else ok++;
    }
    if (err[0] && ok) Toast(TextFormat("Added %d file%s, but: %.100s", ok, ok == 1 ? "" : "s", err));
    else if (err[0])  Toast(err);
}

static void DashedRect(Rectangle r, Color c)
{
    for (float x = r.x; x < r.x + r.width; x += 16) {
        float e = fminf(x + 8, r.x + r.width);
        DrawLineEx((Vector2){x, r.y}, (Vector2){e, r.y}, 2, c);
        DrawLineEx((Vector2){x, r.y + r.height}, (Vector2){e, r.y + r.height}, 2, c);
    }
    for (float y = r.y; y < r.y + r.height; y += 16) {
        float e = fminf(y + 8, r.y + r.height);
        DrawLineEx((Vector2){r.x, y}, (Vector2){r.x, e}, 2, c);
        DrawLineEx((Vector2){r.x + r.width, y}, (Vector2){r.x + r.width, e}, 2, c);
    }
}

static void BackToCode(void) { Unfocus(); screen = SCR_REPO; tab = TAB_CODE; }

static void ScreenUpload(Repo *r)
{
    if (!CanEdit(r)) { BackToCode(); return; }
    TextField *order[] = {&tfUpPath, &tfUpMsg};
    TabCycle(order, 2);

    DrawBold(TextFormat("Upload files to %s", r->name), 20, 190, 30, UI_TEXT);
    DrawText("Everything you upload here is saved together as one commit.", 20, 228, FS, UI_MUTED);

    /* the drop zone, with a path box for people who'd rather type */
    Rectangle zone = {20, 262, (float)W - 40, 116};
    DrawRectangleRec(zone, UI_PANEL);
    DashedRect(zone, uiAccent);
    const char *big = "Drag files from your computer onto this window";
    DrawBold(big, (int)(zone.x + zone.width / 2) - MeasureText(big, FS) / 2, (int)zone.y + 16, FS, UI_TEXT);
    const char *orTxt = "or paste a path:";
    int ow = MeasureText(orTxt, FS);
    DrawText(orTxt, (int)zone.x + 24, (int)zone.y + 64, FS, UI_MUTED);
    float px = zone.x + 24 + ow + 12, pw = zone.x + zone.width - 24 - 100 - px;
    bool enter = TextFieldUI(&tfUpPath, (Rectangle){px, zone.y + 54, pw, 40}, "e.g. C:/Users/jo/Documents/notes.txt");
    if (Button((Rectangle){zone.x + zone.width - 24 - 90, zone.y + 54, 90, 40}, "Add", UI_BTN) || enter) {
        const char *e = StageFile(tfUpPath.buf);
        if (e) Toast(e);
        else TF_Clear(&tfUpPath);
    }

    /* the files waiting to be committed */
    int changed = 0;
    for (int i = 0; i < stagedCount; i++) if (StagedKind(r, &staged[i]) != '=') changed++;
    DrawBold(stagedCount ? TextFormat("Files to upload (%d)", stagedCount) : "Files to upload", 20, 394, FS, UI_TEXT);

    float bottom = (float)H - 112;
    Rectangle la = {20, 424, (float)W - 40, bottom - 424 - 8};
    float rowH = 50;
    ScrollArea(la, &scUpload, stagedCount * rowH);
    if (!stagedCount) DrawText("Nothing added yet.", 20, 430, FS, UI_MUTED);
    int removeAt = -1;
    BeginScissorMode((int)la.x, (int)la.y, (int)la.width, (int)la.height);
    for (int i = 0; i < stagedCount; i++) {
        Staged *s = &staged[i];
        Rectangle row = {la.x, la.y + i * rowH - scUpload, la.width - 12, rowH - 6};
        if (row.y > la.y + la.height || row.y + row.height < la.y) continue;
        DrawRectangleRec(row, UI_PANEL);
        DrawRectangleLinesEx(row, 1, UI_BORDER);

        char kd = StagedKind(r, s);
        const char *badge = kd == 'A' ? "New" : kd == 'M' ? "Replaces" : "Unchanged";
        Color bc = kd == 'A' ? UI_GREEN : kd == 'M' ? (Color){150, 105, 20, 255} : UI_BTN;
        Rectangle pill = {row.x + 10, row.y + 9, 112, 26};
        DrawRectangleRounded(pill, 0.5f, 6, bc);
        DrawText(badge, (int)(pill.x + pill.width / 2) - MeasureText(badge, FS) / 2, (int)pill.y + 3, FS, UI_TEXT);

        int tx = (int)row.x + 136;
        DrawBold(s->name, tx, (int)row.y + 12, FS, UI_TEXT);
        tx += MeasureText(s->name, FS) + 18;
        const char *kind = s->binary ? (IsPictureBytes(s->data, s->len) ? "picture" : "binary file")
                                     : TextFormat("%s, %d line%s", LangLabel(s->lang), CountLines(s->data, s->len), CountLines(s->data, s->len) == 1 ? "" : "s");
        const char *detail = TextFormat("%s, %s", kind, FormatSize(s->len));
        int room = (int)(row.x + row.width) - 120 - tx;
        DrawFit(detail, tx, (int)row.y + 12, FS, room, UI_MUTED);
        if (StagedReplacesEdits(r, s)) {
            const char *w = "   - replaces your uncommitted edits";
            int dw = MeasureText(detail, FS);
            if (dw + MeasureText(w, FS) < room) DrawText(w, tx + dw, (int)row.y + 12, FS, UI_YELLOW);
        }
        if (ButtonEx((Rectangle){row.x + row.width - 106, row.y + 7, 96, 30}, "Remove", UI_BTN, Hover(la))) removeAt = i;
    }
    EndScissorMode();
    DrawScrollbar(la, scUpload, stagedCount * rowH);
    if (removeAt >= 0) {
        free(staged[removeAt].data);
        memmove(&staged[removeAt], &staged[removeAt + 1], (size_t)(stagedCount - removeAt - 1) * sizeof(Staged));
        stagedCount--;
        memset(&staged[stagedCount], 0, sizeof(Staged));
    }

    /* commit message and buttons */
    DrawLine(20, (int)bottom, W - 20, (int)bottom, UI_BORDER);
    DrawText("Commit message", 20, (int)bottom + 14, FS, UI_TEXT);
    if (stagedCount && !changed) DrawText("These are all the same as the latest versions - nothing to commit.",
                                          20 + MeasureText("Commit message", FS) + 24, (int)bottom + 14, FS, UI_YELLOW);
    bool msgEnter = TextFieldUI(&tfUpMsg, (Rectangle){20, bottom + 44, (float)W - 40 - 360, 44}, "Add files via upload");
    bool cancel = Button((Rectangle){(float)W - 350, bottom + 44, 130, 44}, "Cancel", UI_BTN);
    bool go = ButtonEx((Rectangle){(float)W - 210, bottom + 44, 190, 44}, "Commit changes", changed ? UI_GREEN : UI_BTN, changed > 0);
    if (cancel) { BackToCode(); return; }
    if (go || (msgEnter && changed)) {
        char first[PATH_LEN];
        snprintf(first, PATH_LEN, "%s", staged[0].name);
        const char *err = DoUploadCommit(r, tfUpMsg.buf);
        if (err) { Toast(err); return; }
        Toast(TextFormat("Uploaded %d file%s in commit %s", changed, changed == 1 ? "" : "s", Head(r)->id));
        ClearStaged();
        MarkDirty();
        BackToCode();
        SelectFileName(first);
    }
}

/* The bar above a file: its name, a tag like "modified", and where the cursor is
   (or the size, for a non-text file). rightGap leaves room for a button. */
static void DrawFileHeader(Rectangle fh, const char *name, const char *tag, Color tagColor, WorkFile *f, int rightGap, const char *langName)
{
    DrawRectangleRec(fh, UI_PANEL);
    DrawRectangleLinesEx(fh, 1, UI_BORDER);
    DrawBold(name, (int)fh.x + 14, (int)fh.y + 11, FS, UI_TEXT);
    if (tag) DrawText(tag, (int)fh.x + 30 + MeasureText(name, FS), (int)fh.y + 11, FS, tagColor);
    /* the details on the right: the long version, a shorter one if space is tight, just the language, or none */
    char full[128], brief[80], tiny[40];
    if (f->binary) {
        int s = Preview(f->text, f->len);
        if (s >= 0) snprintf(full, sizeof full, "%d x %d   |   %s", prevs[s].w, prevs[s].h, FormatSize(f->len));
        else        snprintf(full, sizeof full, "binary file   |   %s", FormatSize(f->len));
        snprintf(brief, sizeof brief, "%s", FormatSize(f->len));
        tiny[0] = 0;
    } else {
        int ls = LineStart(f->text, ed.cursor), lines = CountLines(f->text, f->len);   /* same count as the language breakdown */
        snprintf(full, sizeof full, "%s   |   Ln %d, Col %d   |   %d lines", langName, LineOf(f->text, ed.cursor) + 1, ed.cursor - ls + 1, lines);
        snprintf(brief, sizeof brief, "%s   |   %d lines", langName, lines);
        snprintf(tiny, sizeof tiny, "%s", langName);
    }
    int leftEnd = (int)fh.x + 14 + MeasureText(name, FS) + (tag ? 16 + MeasureText(tag, FS) : 0) + 20;
    int right = (int)(fh.x + fh.width) - 14 - rightGap;
    const char *info = right - MeasureText(full, FS) >= leftEnd ? full : right - MeasureText(brief, FS) >= leftEnd ? brief
                     : tiny[0] && right - MeasureText(tiny, FS) >= leftEnd ? tiny : NULL;
    if (info) DrawText(info, right - MeasureText(info, FS), (int)fh.y + 11, FS, UI_MUTED);
}

/* A private copy of a committed file for the editor to work on. Nothing is
   saved while you only look or move the cursor; the moment you actually
   change something, the file is copied into your draft. */
static WorkFile    scratch;
static const Repo *scratchRepo;
static int         scratchBlob = -2;
static char        scratchName[PATH_LEN];
static WorkFile *Scratch(Repo *r, ViewFile *v)
{
    if (scratchRepo != r || scratchBlob != v->blob || strcmp(scratchName, v->name) != 0) {
        WF_SetN(&scratch, v->data, v->len);
        scratchRepo = r;
        scratchBlob = v->blob;
        snprintf(scratchName, PATH_LEN, "%s", v->name);
        snprintf(scratch.name, PATH_LEN, "%s", v->name);
    }
    return &scratch;
}
static void ForgetScratch(void) { scratchBlob = -2; }

/* Explains a conflict and offers the two ways out. Returns true if one was chosen. */
static bool ConflictBar(Repo *r, ViewFile *v, Rectangle bar)
{
    WorkFile *e = v->mine;
    int cur = HeadBlobOf(r, v->name), who = LastChangedBy(r, v->name);
    const char *by = who >= 0 ? (who == curUser ? "You" : UserName(who)) : "Someone";
    const char *why = e->deleted          ? TextFormat("You deleted this file, but %s changed it after that.", who == curUser ? "you" : by)
                    : cur < 0             ? TextFormat("%s deleted this file after you started editing it.", by)
                    : e->base < 0         ? TextFormat("%s also added a file called %s.", by, v->name)
                                          : TextFormat("%s committed a newer version after you started editing it.", by);
    DrawRectangleRec(bar, (Color){74, 40, 18, 255});
    DrawRectangleLinesEx(bar, 1, UI_ORANGE);
    float tw = bar.width - 32 - 300;
    DrawBold("Conflict - this file was changed by someone else too", (int)bar.x + 16, (int)bar.y + 10, FS, UI_ORANGE);
    DrawFit(why, (int)bar.x + 16, (int)bar.y + 36, FS, (int)tw, UI_TEXT);
    if (Button((Rectangle){bar.x + bar.width - 300, bar.y + 14, 140, 36}, "Keep mine", UI_BTN)) {
        e->base = cur;                       /* yours now counts as a change to the latest version */
        MarkDirty();
        Toast("Keeping your version - it will replace theirs when you commit");
        return true;
    }
    if (Button((Rectangle){bar.x + bar.width - 152, bar.y + 14, 140, 36}, "Use theirs", UI_BTN)) {
        DraftDropName(r, curUser, v->name);  /* forget your change; you see the latest version */
        MarkDirty();
        Toast("Switched to the latest version - your change was dropped");
        return true;
    }
    return false;
}

/* Would a new file called nm clash with a folder of the same name (or the
   other way round, e.g. a new "notes.txt/a.md" when notes.txt is a file)? Uses the current view. */
static bool FolderOrFileClash(const char *nm)
{
    size_t n = strlen(nm);
    for (int i = 0; i < viewCount; i++) {
        if (ViewDeleted(&view[i])) continue;
        const char *v = view[i].name;
        size_t vl = strlen(v);
        if (vl > n && strncmp(v, nm, n) == 0 && v[n] == '/') return true;     /* nm is a folder already */
        if (n > vl && strncmp(nm, v, vl) == 0 && nm[vl] == '/') return true;  /* a file sits where a folder would go */
    }
    return false;
}

static void TabCode(Repo *r)
{
    bool edit = CanEdit(r);
    float top = 186, bottom = (float)H - 20;
    top = DrawForkBar(r, top);
    if (edit) DraftTidy(r, curUser);
    BuildView(r, edit ? curUser : -1);

    /* keep the open file, or open the first one */
    if (viewCount == 0) selName[0] = 0;
    else if (!selName[0] || ViewIndex(selName) < 0) {
        int first = 0;
        for (int i = 0; i < viewCount; i++) if (!ViewDeleted(&view[i])) { first = i; break; }
        SelectFileName(view[first].name);
    }

    /* ---- left: file list (with each file's language as a coloured dot) ---- */
    BuildLanguages(r, edit ? curUser : -1);
    Rectangle lp = {20, top, 260, bottom - top};
    DrawRectangleRec(lp, UI_PANEL);
    DrawRectangleLinesEx(lp, 1, UI_BORDER);
    BranchButton(r, (Rectangle){lp.x + 8, top + 6, lp.width - 16, 32});      /* which branch the files are from */
    DrawFileList(r, (Rectangle){lp.x + 1, top + 44, lp.width - 2, lp.height - 44 - (edit ? 156 : 4)});

    if (edit) {
        float by = lp.y + lp.height - 148;
        DrawLine((int)lp.x, (int)by - 10, (int)(lp.x + lp.width), (int)by - 10, UI_BORDER);
        bool addEnter = TextFieldUI(&tfNewFile, (Rectangle){lp.x + 12, by, lp.width - 24, 38}, "new-file.txt");
        if (Button((Rectangle){lp.x + 12, by + 46, 114, 36}, "Add file", UI_GREEN) || addEnter) {
            char nm[PATH_LEN];
            SanitizePath(tfNewFile.buf, nm);
            BuildView(r, curUser);
            int vi = ViewIndex(nm);
            if (!nm[0])                                    Toast("Type a file name first, e.g. notes.txt or docs/notes.txt");
            else if (!ValidPath(nm))                       Toast("That isn't a usable file name");
            else if (FolderOrFileClash(nm))                Toast("That name is already used by a folder (or a file on the way to it)");
            else if (vi >= 0 && !ViewDeleted(&view[vi]))   Toast("A file with that name already exists");
            else if (vi < 0 && ShownCount(r) >= MAX_FILES) Toast(TextFormat("A repository can hold up to %d files", MAX_FILES));
            else {
                WorkFile *e = DraftEdit(r, curUser, nm);
                if (!e) Toast("You're changing too many files at once - commit some first");
                else {
                    e->deleted = false;
                    WF_SetN(e, "", 0);
                    SelectFileName(nm);
                    Unfocus();
                    ed.focused = true;
                    TF_Clear(&tfNewFile);
                    MarkDirty();
                }
            }
        }
        if (Button((Rectangle){lp.x + lp.width - 126, by + 46, 114, 36}, "Upload", UI_BTN)) { OpenUpload(); return; }
        int nch, ncf;
        DraftCounts(r, curUser, &nch, &ncf);
        const char *st = ncf ? TextFormat("%d change%s, %d conflict%s", nch + ncf, nch + ncf == 1 ? "" : "s", ncf, ncf == 1 ? "" : "s")
                       : nch ? TextFormat("%d change%s to commit", nch, nch == 1 ? "" : "s")
                             : "All changes committed";
        DrawFit(st, (int)lp.x + 14, (int)by + 100, FS, (int)lp.width - 28, ncf ? UI_ORANGE : nch ? UI_YELLOW : UI_MUTED);
    }

    /* ---- right: the language breakdown, then the editor (or a viewer) + bottom bar ---- */
    BuildLanguages(r, edit ? curUser : -1);
    if (!(edit && DrawMergeBanner(r, (Rectangle){300, top, (float)W - 320, 40})))
        DrawLanguageBar((Rectangle){300, top, (float)W - 320, 40});
    Rectangle rp  = {300, top + 48, (float)W - 320, bottom - top - 48};
    Rectangle fh  = {rp.x, rp.y, rp.width, 42};
    Rectangle edr = {rp.x, rp.y + 42, rp.width, rp.height - 42 - 62};
    Rectangle cb  = {rp.x, rp.y + rp.height - 48, rp.width, 48};

    BuildView(r, edit ? curUser : -1);
    int sel = ViewIndex(selName);
    if (sel >= 0) {
        ViewFile *v = &view[sel];
        Rectangle area = edr;
        if (v->status == 'C') {
            if (ConflictBar(r, v, (Rectangle){edr.x, edr.y, edr.width, 64})) return;
            area.y += 64;
            area.height -= 64;
        }
        WorkFile shown = {0};                      /* what the header describes */
        snprintf(shown.name, PATH_LEN, "%s", v->name);
        shown.text = (char *)v->data; shown.len = v->len; shown.binary = v->binary;

        if (ViewDeleted(v)) {
            DrawRectangleRec(area, UI_BASE);
            float cx = area.x + area.width / 2, cy = area.y + area.height / 2;
            const char *l1 = "You've deleted this file.";
            const char *l2 = "It will be removed from the repository when you commit.";
            DrawBold(l1, (int)cx - MeasureText(l1, FS) / 2, (int)cy - 50, FS, UI_TEXT);
            DrawText(l2, (int)cx - MeasureText(l2, FS) / 2, (int)cy - 22, FS, UI_MUTED);
            if (v->status != 'C' && Button((Rectangle){cx - 80, cy + 16, 160, 40}, "Undo delete", UI_BTN)) {
                DraftDropName(r, curUser, v->name);
                MarkDirty();
                return;
            }
        } else if (v->binary) {
            DrawBinaryView(v->data, v->len, area);
        } else if (v->mine) {
            if (EditorUI(v->mine, area, false)) MarkDirty();
            shown = *v->mine;
        } else {
            WorkFile *sc = Scratch(r, v);
            if (EditorUI(sc, area, !edit) && edit) {
                WorkFile *e = DraftEdit(r, curUser, v->name);
                if (e) { WF_SetN(e, sc->text, sc->len); MarkDirty(); }
                else Toast("You're changing too many files at once - commit some first");
                ForgetScratch();
            }
            shown.text = sc->text; shown.len = sc->len;
        }
        DrawRectangleLinesEx(area, 1, UI_BORDER);

        const char *tag = NULL;
        Color tc = UI_MUTED;
        if (!edit)                  { Commit *h = Head(r); tag = h ? TextFormat("as of commit %s", h->id) : NULL; }
        else if (v->status == 'A')  { tag = "new file"; tc = UI_GREENT; }
        else if (v->status == 'M')  { tag = "modified"; tc = UI_YELLOW; }
        else if (v->status == 'D')  { tag = "deleted";  tc = UI_REDT; }
        else if (v->status == 'C')  { tag = "conflict"; tc = UI_ORANGE; }
        bool canDelete = edit && !ViewDeleted(v), canDownload = !ViewDeleted(v);
        DrawFileHeader(fh, v->name, tag, tc, &shown, (canDelete ? 104 : 0) + (canDownload ? 136 : 0), LangLabel(ViewLang(r, v, NULL)));
        if (canDownload && Button((Rectangle){fh.x + fh.width - (canDelete ? 106 : 0) - 136, fh.y + 6, 128, 30}, "Download", UI_BTN))
            DownloadFile(v->name, shown.text, shown.len);       /* exactly what's on screen */
        if (canDelete && ConfirmButton((Rectangle){fh.x + fh.width - 106, fh.y + 6, 98, 30}, "Delete", "Sure?", 3)) {
            WorkFile *e = DraftFind(r, curUser, v->name);
            if (e && e->base < 0 && HeadBlobOf(r, v->name) < 0) {   /* never committed: just remove it */
                DraftDropName(r, curUser, v->name);
                SelectFileName("");
            } else {
                e = DraftEdit(r, curUser, v->name);
                if (e) e->deleted = true;
            }
            MarkDirty();
            Toast("File deleted - commit to record it in the history");
            return;
        }
    } else {
        Rectangle e = {rp.x, rp.y, rp.width, rp.height - 62};
        DrawRectangleRec(e, UI_PANEL);
        DrawRectangleLinesEx(e, 1, UI_BORDER);
        const char *m = edit ? "Pick a file on the left, add a new one, or upload some." : "This repository has no committed files yet.";
        DrawText(m, (int)(e.x + e.width / 2) - MeasureText(m, FS) / 2, (int)(e.y + e.height / 2) - 10, FS, UI_MUTED);
        if (edit && !r->commitCount && gitPort) {          /* empty: say how to push a project that's already on this computer */
            const char *lines[3] = {"Or push a project you already have, from its folder:",
                                    TextFormat("git push %s main", CloneUrl(r)),
                                    "(use master instead of main if that's what your branch is called)"};
            for (int i = 0; i < 3; i++) {
                int lx = (int)(e.x + e.width / 2) - MeasureText(lines[i], FS) / 2;
                if (lx < (int)e.x + 10) lx = (int)e.x + 10;
                DrawFit(lines[i], lx, (int)(e.y + e.height / 2) + 40 + i * 30, FS, (int)e.width - 20, i == 1 ? UI_TEXT : UI_MUTED);
            }
        }
    }

    if (!edit) {
        const char *o = UserName(r->owner);
        const char *note = r->collabCount
            ? TextFormat("Only %s and their collaborators can change this. Press Fork to get your own copy.", o)
            : TextFormat("Only %s can change this. Press Fork to get your own copy.", o);
        DrawFit(note, (int)cb.x, (int)cb.y + 14, FS, (int)cb.width, UI_MUTED);
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
            ForgetScratch();
            MarkDirty();
        }
    }
}

/* ---------- the commit graph ----------
   Like the graph in VS Code's source control view: the newest commit at the
   top, each commit a dot, and lines joining every commit to the one(s) it
   was built on. When two people work from the same commit the line splits
   into two "lanes"; a merge commit joins them back together.

   The lanes are worked out from the newest commit down. Each lane is
   "waiting" for one commit (the next one further down that line). A commit
   goes in the lane that's waiting for it; any other lanes waiting for it
   too curve in and end there. Then its lane waits for its first parent,
   and a merge's second parent gets a lane of its own, curving out. */
typedef struct { int col, at, n; } GraphRow;      /* the dot's lane; the lanes below it: [at, at + n) */
static struct {
    const Repo *r; unsigned gen; int count;
    GraphRow *rows; int rowCap;
    int *wait, *from; int cap, used;              /* per lane below a row: the commit it waits for (-1 = none), and the lane its line starts from */
    int maxLanes;
    int *order, orderCap;                         /* which commit is on each row */
} graph;

/* The order of the rows: newest first, but a commit always comes after
   (below) every commit built on it - the same order as git log --date-order.
   Each step takes the newest commit whose children are all placed already. */
static long long *orderTime;                     /* when each commit was committed (git's "committer" time) */
static bool HeapLess(Repo *r, int a, int b)
{
    (void)r;
    long long ta = orderTime[a], tb = orderTime[b];
    return ta != tb ? ta < tb : a < b;
}
/* The committer time: when the commit was actually made (the author time is
   when the change was first written - they differ for re-applied work). */
static long long CommitterTime(Commit *c)
{
    const char *p = c->raw ? strstr(c->raw, "\ncommitter ") : NULL;
    const char *gt = p ? strchr(p, '>') : NULL;
    const char *nl = p ? strchr(p + 1, '\n') : NULL;
    if (!gt || (nl && gt > nl)) return c->time;
    return strtoll(gt + 1, NULL, 10);
}
static void GraphOrder(Repo *r)
{
    int n = r->commitCount;
    graph.order = GrowArr(graph.order, &graph.orderCap, n + 1, sizeof(int));
    int *kids = calloc((size_t)n + 1, sizeof(int)), *heap = malloc(((size_t)n + 1) * sizeof(int));
    orderTime = malloc(((size_t)n + 1) * sizeof(long long));
    if (!kids || !heap || !orderTime || graph.orderCap < n) {   /* out of memory: plain newest-first */
        for (int i = 0; i < n && graph.orderCap >= n; i++) graph.order[i] = n - 1 - i;
        free(kids); free(heap); free(orderTime); orderTime = NULL; return;
    }
    for (int k = 0; k < n; k++) orderTime[k] = CommitterTime(&r->commits[k]);
    for (int k = 0; k < n; k++)
        for (int q = 0; q < r->commits[k].parentCount; q++) kids[r->commits[k].parents[q]]++;
    int hn = 0, out = 0;
    #define HPUSH(v) do { int i_ = hn++; heap[i_] = (v); \
        while (i_ > 0 && HeapLess(r, heap[(i_ - 1) / 2], heap[i_])) { int t_ = heap[i_]; heap[i_] = heap[(i_ - 1) / 2]; heap[(i_ - 1) / 2] = t_; i_ = (i_ - 1) / 2; } } while (0)
    for (int k = 0; k < n; k++) if (!kids[k]) HPUSH(k);
    while (hn) {
        int top = heap[0];
        heap[0] = heap[--hn];
        for (int i = 0;;) {                                  /* sift down */
            int a = 2 * i + 1, b = a + 1, m = i;
            if (a < hn && HeapLess(r, heap[m], heap[a])) m = a;
            if (b < hn && HeapLess(r, heap[m], heap[b])) m = b;
            if (m == i) break;
            int t = heap[i]; heap[i] = heap[m]; heap[m] = t; i = m;
        }
        graph.order[out++] = top;
        Commit *c = &r->commits[top];
        for (int q = 0; q < c->parentCount; q++) if (--kids[c->parents[q]] == 0) HPUSH(c->parents[q]);
    }
    #undef HPUSH
    for (int k = 0; out < n && k < n; k++) if (kids[k] > 0) graph.order[out++] = k;   /* can't happen, but never leave a gap */
    free(kids); free(heap); free(orderTime); orderTime = NULL;
}
static int commitsView = 1;                       /* 0 = list, 1 = graph */

static void BuildGraph(Repo *r)
{
    if (graph.r == r && graph.gen == r->gen && graph.count == r->commitCount) return;
    int n = r->commitCount;
    graph.r = r; graph.gen = r->gen; graph.count = n; graph.used = 0; graph.maxLanes = 0;
    graph.rows = GrowArr(graph.rows, &graph.rowCap, n + 1, sizeof(GraphRow));
    GraphOrder(r);
    if (graph.orderCap < n) { graph.count = -1; return; }
    int *lanes = NULL, L = 0, lcap = 0, *from = NULL, fcap = 0;
    for (int i = 0; i < n && graph.rowCap > n; i++) {
        int c = graph.order[i];                    /* row 0 is the newest commit */
        Commit *cm = &r->commits[c];
        int col = -1;
        for (int k = 0; k < L; k++)
            if (lanes[k] == c) { if (col < 0) col = k; else lanes[k] = -1; }   /* lines joining here end here */
        if (col < 0) {                             /* nothing above leads here: the newest commit of a line */
            for (int k = 0; k < L && col < 0; k++) if (lanes[k] < 0) col = k;
            if (col < 0) { lanes = GrowArr(lanes, &lcap, L + 1, sizeof(int)); col = L++; }
        }
        from = GrowArr(from, &fcap, L + 2, sizeof(int));
        for (int k = 0; k < L; k++) from[k] = k;
        lanes[col] = cm->parentCount > 0 ? cm->parents[0] : -1;
        from[col] = col;
        if (cm->parentCount > 1) {                 /* a merge: its other parent gets a lane, curving out of this dot */
            int f = -1;
            for (int k = 0; k < L && f < 0; k++) if (lanes[k] < 0) f = k;
            if (f < 0) { lanes = GrowArr(lanes, &lcap, L + 1, sizeof(int)); from = GrowArr(from, &fcap, L + 2, sizeof(int)); f = L++; }
            lanes[f] = cm->parents[1];
            from[f] = col;
        }
        while (L > 0 && lanes[L - 1] < 0) L--;      /* drop empty lanes on the right */
        graph.rows[i] = (GraphRow){col, graph.used, L};
        if (col + 1 > graph.maxLanes) graph.maxLanes = col + 1;
        if (L > graph.maxLanes) graph.maxLanes = L;
        if (graph.used + L > graph.cap) {
            int nc = graph.cap ? graph.cap : 1024;
            while (nc < graph.used + L) nc *= 2;
            int *w = realloc(graph.wait, (size_t)nc * sizeof(int)), *fr = w ? realloc(graph.from, (size_t)nc * sizeof(int)) : NULL;
            if (w) graph.wait = w;
            if (fr) graph.from = fr;
            if (!w || !fr) { graph.rows[i].n = 0; continue; }
            graph.cap = nc;
        }
        memcpy(graph.wait + graph.used, lanes, (size_t)L * sizeof(int));
        memcpy(graph.from + graph.used, from, (size_t)L * sizeof(int));
        graph.used += L;
    }
    free(lanes); free(from);
}

/* The same colour-blind-checked colours as the languages and the pie, one per lane. */
static Color LaneColor(int k) { return LANG_PALETTE[(k + 0) % 8]; }

static const char *LongTime(long long t)
{
    static char b[40];
    time_t tt = (time_t)t;
    strftime(b, sizeof b, "%d %b %Y, %H:%M", localtime(&tt));
    return b;
}

/* The details of a commit, next to the mouse (like the contribution calendar's). */
static void DrawCommitTip(Repo *r, int ci)
{
    Commit *c = &r->commits[ci];
    CommitStats(r, ci);
    static char        kinds[MAX_FILES * 2];
    static const char *names[MAX_FILES * 2];
    int nf = CommitFileList(r, ci, kinds, names, MAX_FILES * 2);

    enum { MAXL = 16 };
    char  text[MAXL][200];
    Color col[MAXL];
    bool  bold[MAXL] = {0}, rule[MAXL] = {0}, adds[MAXL] = {0};
    char  kind[MAXL] = {0};
    int   n = 0;
    snprintf(text[n], 200, "%.70s", c->message); bold[n] = true; col[n++] = UI_TEXT;
    snprintf(text[n], 200, "%s   |   %s", CommitAuthorName(c), LongTime(c->time)); col[n++] = UI_MUTED;
    if (c->parentCount > 1)
        snprintf(text[n], 200, "merge %s   |   joins %s and %s", c->id, r->commits[c->parents[0]].id, r->commits[c->parents[1]].id);
    else if (c->parentCount)
        snprintf(text[n], 200, "commit %s   |   built on %s", c->id, r->commits[c->parents[0]].id);
    else
        snprintf(text[n], 200, "commit %s   |   the first commit", c->id);
    col[n++] = UI_MUTED;
    rule[n] = true;
    snprintf(text[n], 200, "%d file%s changed   ", c->sFiles, c->sFiles == 1 ? "" : "s"); adds[n] = true; col[n++] = UI_TEXT;
    int shown = nf < 6 ? nf : 6;
    for (int i = 0; i < shown; i++) {
        snprintf(text[n], 200, "%.80s", names[i]);
        kind[n] = kinds[i];
        col[n++] = UI_TEXT;
    }
    if (nf > shown) { snprintf(text[n], 200, "...and %d more", nf - shown); col[n++] = UI_MUTED; }
    if (c->copied) { snprintf(text[n], 200, "(brought in from the original repository)"); col[n++] = UI_MUTED; }
    rule[n] = true;
    snprintf(text[n], 200, "Click to see what changed"); col[n++] = UI_MUTED;

    int w = 0, rules = 0;
    for (int i = 0; i < n; i++) {
        int lw = MeasureText(text[i], FS) + (kind[i] ? 30 : 0);
        if (adds[i]) lw += MeasureText(TextFormat("+%d -%d", c->sAdds, c->sDels), FS) + 10;
        if (lw > w) w = lw;
        if (rule[i]) rules++;
    }
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
        int tx = x + 16;
        if (kind[i]) {
            Color kc = kind[i] == 'A' ? UI_GREENT : kind[i] == 'D' ? UI_REDT : UI_YELLOW;
            char k[2] = {kind[i], 0};
            DrawBold(k, tx, ty, FS, kc);
            tx += 30;
        }
        if (bold[i]) DrawBold(text[i], tx, ty, FS, col[i]);
        else         DrawText(text[i], tx, ty, FS, col[i]);
        if (adds[i]) DrawAddsDels(c->sAdds, c->sDels, tx + MeasureText(text[i], FS), ty);
        ty += 26;
    }
}

static void CommitGraph(Repo *r, Rectangle la)
{
    BuildGraph(r);
    int n = r->commitCount;
    float rowH = 36, laneW = 18;
    float gx = la.x + 22;                                    /* the middle of lane 0 */
    float gw = graph.maxLanes * laneW;
    float maxGw = la.width * 0.4f;
    if (gw > maxGw) gw = maxGw;
    float textX = la.x + 22 + gw + 8;
    ScrollArea(la, &scGraph, n * rowH + 8);
    DrawRectangleRec(la, UI_PANEL);
    DrawRectangleLinesEx(la, 1, UI_BORDER);
    int first = (int)(scGraph / rowH) - 1, last = (int)((scGraph + la.height) / rowH) + 1;
    if (first < 0) first = 0;
    if (last > n - 1) last = n - 1;
    int hov = -1;
    bool pillHover = false;
    Vector2 mouse = GetMousePosition();

    BeginScissorMode((int)la.x + 1, (int)la.y + 1, (int)la.width - 14, (int)la.height - 2);
    /* row backgrounds first, so the lines go over them */
    for (int i = first; i <= last; i++) {
        Rectangle row = {la.x + 1, la.y + 4 + i * rowH - scGraph, la.width - 14, rowH};
        if (Hover(la) && CheckCollisionPointRec(mouse, row)) { hov = i; DrawRectangleRec(row, UI_HOVER); }
    }
    /* the lines: from each row's dot level down to the next row's */
    BeginScissorMode((int)la.x + 1, (int)la.y + 1, (int)(gw + 30), (int)la.height - 2);
    for (int i = first - 1; i <= last; i++) {
        if (i < 0 || i >= n - 1) continue;
        GraphRow *g = &graph.rows[i], *g2 = &graph.rows[i + 1];
        float y1 = la.y + 4 + i * rowH + rowH / 2 - scGraph, y2 = y1 + rowH;
        int below = graph.order[i + 1];                      /* the commit on the next row */
        for (int k = 0; k < g->n; k++) {
            int w = graph.wait[g->at + k];
            if (w < 0) continue;
            int fromK = graph.from[g->at + k];
            int toK = w == below ? g2->col : k;
            Vector2 a = {gx + fromK * laneW, y1}, b = {gx + toK * laneW, y2};
            Color lc = LaneColor(k);
            if (a.x == b.x) DrawLineEx(a, b, 2.5f, lc);
            else DrawLineBezier(a, b, 2.5f, lc);
        }
    }
    /* the dots */
    for (int i = first; i <= last; i++) {
        int ci = graph.order[i];
        Commit *c = &r->commits[ci];
        float cy = la.y + 4 + i * rowH + rowH / 2 - scGraph, cx = gx + graph.rows[i].col * laneW;
        Color lc = LaneColor(graph.rows[i].col);
        if (c->parentCount > 1) { DrawCircle((int)cx, (int)cy, 7, lc); DrawCircle((int)cx, (int)cy, 4, (Color){22, 27, 34, 255}); }   /* merges: a ring */
        else DrawCircle((int)cx, (int)cy, 6, lc);
        for (int b = 0; b < r->branchCount; b++)                                                             /* a branch's newest commit */
            if (r->branches[b].tip == ci) {
                DrawCircleLines((int)cx, (int)cy, 10, lc);
                if (b == r->curBranch) DrawCircleLines((int)cx, (int)cy, 11, lc);
            }
    }
    EndScissorMode();
    BeginScissorMode((int)la.x + 1, (int)la.y + 1, (int)la.width - 14, (int)la.height - 2);
    /* the words */
    for (int i = first; i <= last; i++) {
        int ci = graph.order[i];
        Commit *c = &r->commits[ci];
        float y = la.y + 4 + i * rowH - scGraph;
        float tx = textX;
        /* a name tag for each branch whose newest commit this is: the one you're on is filled in */
        for (int b = 0; b < r->branchCount; b++) {
            if (r->branches[b].tip != ci) continue;
            const char *nm = r->branches[b].name;
            bool cur = b == r->curBranch, def = b == r->defBranch;
            int bw = MeasureText(nm, FS) + 16 + (cur ? 18 : 0);
            if (tx + bw > la.x + la.width * 0.75f) break;
            Rectangle pill = {tx, y + 6, (float)bw, 24};
            Color base = def ? UI_GREENT : UI_LINK;
            bool ph = CheckCollisionPointRec(mouse, pill) && Hover(la);
            DrawRectangleRounded(pill, 0.5f, 6, cur ? Alpha(base, ph ? 0.55f : 0.40f) : Alpha(base, ph ? 0.30f : 0.16f));
            DrawRectangleRoundedLinesEx(pill, 0.5f, 6, 1, Alpha(base, 0.8f));
            if (cur) DrawCircle((int)tx + 12, (int)y + 18, 4, UI_TEXT);              /* you are here */
            DrawText(nm, (int)tx + 8 + (cur ? 18 : 0), (int)y + 8, FS, cur ? UI_TEXT : base);
            if (ph) {
                pillHover = true;
                wantCursor = MOUSE_CURSOR_POINTING_HAND;
                BranchStats(r);
                SetTip(TextFormat("%s%s", nm, def ? "  (default branch)" : ""),
                       def ? TextFormat("%s", cur ? "You're on this branch" : "The branch git clone gives you")
                           : TextFormat("%d commit%s ahead of %s, %d behind", bstat.ahead[b], bstat.ahead[b] == 1 ? "" : "s", DefBranchName(r), bstat.behind[b]),
                       cur ? "You're working on this branch" : "Click to switch to this branch");
                if (Clicked(pill)) SwitchBranch(r, b);
            }
            tx += bw + 8;
        }
        float right = la.x + la.width - 24;
        const char *when = TimeAgo(c->time);
        const char *who = CommitAuthorName(c);
        int idW = MeasureText(c->id, FS), whenW = MeasureText(when, FS);
        float idX = right - idW, whenX = idX - 24 - whenW;
        float whoW = (float)MeasureText(who, FS);
        if (whoW > 220) whoW = 220;
        float whoX = whenX - 24 - whoW;
        bool roomy = whoX - 16 - tx > 160;                    /* narrow window: just the message and id */
        DrawFit(c->message, (int)tx, (int)y + 8, FS, (int)((roomy ? whoX - 40 : idX - 20) - tx), c->copied ? UI_MUTED : UI_TEXT);
        if (roomy) {
            DrawAvatar(c->author, (int)whoX - 16, (int)y + 18, 10);
            DrawFit(who, (int)whoX, (int)y + 8, FS, (int)whoW, UI_MUTED);
            DrawText(when, (int)whenX, (int)y + 8, FS, UI_MUTED);
        }
        DrawText(c->id, (int)idX, (int)y + 8, FS, UI_LINK);
    }
    EndScissorMode();
    DrawScrollbar(la, scGraph, n * rowH + 8);

    if (hov >= 0 && !pillHover) {
        wantCursor = MOUSE_CURSOR_POINTING_HAND;
        int ci = graph.order[hov];
        if (Clicked((Rectangle){la.x, la.y, la.width - 14, la.height})) { OpenCommit(ci); return; }
        DrawCommitTip(r, ci);
    }
}

/* The commits in the current branch's history, newest first (kept until the commits or the branch change). */
static struct { const Repo *r; unsigned gen; int tip, count, n, cap; int *idx; } hist;
static void BuildHistory(Repo *r)
{
    int tip = HeadIndex(r);
    if (hist.r == r && hist.gen == r->gen && hist.tip == tip && hist.count == r->commitCount) return;
    hist.r = r; hist.gen = r->gen; hist.tip = tip; hist.count = r->commitCount; hist.n = 0;
    uint8_t *m = calloc((size_t)r->commitCount + 1, 1);
    hist.idx = GrowArr(hist.idx, &hist.cap, r->commitCount + 1, sizeof(int));
    if (!m || hist.cap <= r->commitCount) { free(m); return; }
    MarkAnc(r, tip, m, 1);
    for (int k = r->commitCount - 1; k >= 0; k--) if (m[k]) hist.idx[hist.n++] = k;
    free(m);
}

static void TabCommits(Repo *r)
{
    int total = r->commitCount;
    BuildHistory(r);
    int n = hist.n;
    /* Graph or List, and which branch */
    if (total) {
        const char *labels[2] = {"List", "Graph"};
        float bx = 20;
        for (int v = 0; v < 2; v++) {
            Rectangle b = {bx, 184, 110, 34};
            bool on = commitsView == v;
            if (ButtonEx(b, labels[v], on ? UI_BLUE : UI_BTN, true) && !on) commitsView = v;
            bx += 118;
        }
        BranchButton(r, (Rectangle){bx + 8, 184, 260, 34});
        int merges = 0;
        for (int k = 0; k < total; k++) if (r->commits[k].parentCount > 1) merges++;
        const char *info = commitsView == 1
            ? TextFormat("%d commit%s on %d branch%s%s", total, total == 1 ? "" : "s", r->branchCount, r->branchCount == 1 ? "" : "es",
                         merges ? TextFormat(", %d merge%s", merges, merges == 1 ? "" : "s") : "")
            : TextFormat("%d commit%s in %s's history", n, n == 1 ? "" : "s", CurBranchName(r));
        if (W - 20 - MeasureText(info, FS) > bx + 290) DrawText(info, W - 20 - MeasureText(info, FS), 191, FS, UI_MUTED);
        if (commitsView == 1) { CommitGraph(r, (Rectangle){20, 230, (float)W - 40, (float)H - 250}); return; }
    }
    Rectangle la = {20, total ? 230.0f : 186.0f, (float)W - 40, (float)H - (total ? 250 : 206)};
    float rowH = 74;
    ScrollArea(la, &scCommits, n * rowH);
    if (!n) {
        DrawText("No commits yet.", 20, 196, FS, UI_TEXT);
        if (CanEdit(r)) DrawText("Edit some files in the Code tab, write a message, then press Commit.", 20, 226, FS, UI_MUTED);
        return;
    }
    BeginScissorMode((int)la.x, (int)la.y, (int)la.width, (int)la.height);
    for (int k = 0; k < n; k++) {
        int ci = hist.idx[k];
        Commit *c = &r->commits[ci];
        Rectangle row = {la.x, la.y + k * rowH - scCommits, la.width - 12, rowH};
        if (row.y > la.y + la.height || row.y + row.height < la.y) continue;
        bool hot = Hover(la) && Hover(row);
        if (hot) wantCursor = MOUSE_CURSOR_POINTING_HAND;
        DrawRectangleRec(row, hot ? UI_HOVER : UI_PANEL);
        DrawRectangleLinesEx(row, 1, UI_BORDER);
        DrawAvatar(c->author, (int)row.x + 36, (int)row.y + 37, 18);
        DrawFit(c->message, (int)row.x + 70, (int)row.y + 14, FS, (int)row.width - 310, UI_TEXT);
        DrawFit(TextFormat("%s committed %s", CommitAuthorName(c), TimeAgo(c->time)), (int)row.x + 70, (int)row.y + 42, FS, (int)row.width - 310, UI_MUTED);

        Rectangle idPill = {row.x + row.width - 120, row.y + 20, 102, 34};
        DrawRectangleRounded(idPill, 0.3f, 6, UI_BTN);
        DrawText(c->id, (int)idPill.x + 12, (int)idPill.y + 7, FS, UI_LINK);
        if (k == 0) {
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
        bool canManage = CanEdit(r) || is->author == curUser;
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

/* A non-text file in a commit: no lines to compare, so show the sizes,
   and for pictures the old and new versions side by side. */
static void DrawBinaryChange(Repo *r, Change *ch, Rectangle area)
{
    DrawRectangleRec(area, UI_BASE);
    DrawRectangleLinesEx(area, 1, UI_BORDER);
    int ol = BlobLen(r, ch->oldB), nl = BlobLen(r, ch->newB);
    const char *what = ch->kind == 'A' ? TextFormat("Added   |   %s", FormatSize(nl))
                     : ch->kind == 'D' ? TextFormat("Deleted   |   was %s", FormatSize(ol))
                                       : TextFormat("Changed   |   %s  ->  %s", FormatSize(ol), FormatSize(nl));
    DrawText(what, (int)area.x + 16, (int)area.y + 14, FS, UI_TEXT);
    Rectangle box = {area.x + 16, area.y + 54, area.width - 32, area.height - 70};
    int so = ch->oldB >= 0 ? Preview(BlobText(r, ch->oldB), ol) : -1;
    int sn = ch->newB >= 0 ? Preview(BlobText(r, ch->newB), nl) : -1;
    if (ch->kind == 'M' && so >= 0 && sn >= 0) {
        float half = (box.width - 20) / 2;
        DrawText("Before", (int)box.x, (int)box.y, FS, UI_REDT);
        DrawText("After", (int)(box.x + half + 20), (int)box.y, FS, UI_GREENT);
        DrawPicture(so, (Rectangle){box.x, box.y + 30, half, box.height - 30});
        DrawPicture(sn, (Rectangle){box.x + half + 20, box.y + 30, half, box.height - 30});
    } else if (sn >= 0 || so >= 0) {
        DrawPicture(sn >= 0 ? sn : so, box);
    } else {
        DrawText("This isn't a text file, so there are no lines to compare.", (int)box.x, (int)box.y, FS, UI_MUTED);
    }
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
    if (Button((Rectangle){(float)W - (CanEdit(r) ? 590 : 270), 186, 250, 38}, "Download this version", UI_BTN)) DownloadCommit(r, curCommit);
    if (CanEdit(r) &&
        ConfirmButton((Rectangle){(float)W - 330, 186, 310, 38}, "Restore files from here", "Click again to confirm", 2)) {
        RestoreCommit(r, curCommit);
        SelectFileName("");
        MarkDirty();
        Toast(TextFormat("Files restored to %s - commit them to keep this version", c->id));
        Unfocus();
        screen = SCR_REPO;
        tab = TAB_CODE;
        return;
    }

    DrawFit(c->message, 20, 238, 30, W - 40, UI_TEXT);
    /* who wrote it according to git, if that isn't simply the Mini GitHub user */
    CommitInfo info;
    bool parsed = ParseCommit(c->raw, c->rawLen, &info);
    const char *who = CommitAuthorName(c);
    const char *meta = TextFormat("%s committed %s   |   %s %s   |   %d file%s changed   ",
                                  who, TimeAgo(c->time), c->parentCount > 1 ? "merge" : "commit", c->id,
                                  changeCount, changeCount == 1 ? "" : "s");
    DrawText(meta, 20, 276, FS, UI_MUTED);
    int mx = 20 + MeasureText(meta, FS);
    if (Hover((Rectangle){20, 272, (float)mx - 20, 28})) {          /* the details git has */
        char hx[41], p1[41] = "", p2[41] = "";
        ShaHex(c->sha, hx);
        if (c->parentCount > 0) ShaHex(r->commits[c->parents[0]].sha, p1);
        if (c->parentCount > 1) ShaHex(r->commits[c->parents[1]].sha, p2);
        SetTip(TextFormat("commit %s", hx),
               parsed ? TextFormat("Author: %.60s <%.60s>", info.aname, info.aemail) : NULL,
               c->parentCount > 1 ? TextFormat("Merges %.7s and %.7s", p1, p2) : c->parentCount ? TextFormat("Built on %.7s", p1) : "The first commit");
    }
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
    const char *ad = ch->binary ? "binary" : TextFormat("+%d  -%d", ch->adds, ch->dels);
    DrawText(ad, (int)(fh.x + fh.width) - 14 - MeasureText(ad, FS), (int)fh.y + 11, FS, UI_MUTED);
    Rectangle area = {rp.x, rp.y + 42, rp.width, rp.height - 42};
    if (ch->binary) DrawBinaryChange(r, ch, area);
    else            DrawDiff(area);
}

/* ---------------- one day of contributions ---------------- */
static void ScreenDay(void)
{
    long long today = DayStart((long long)time(NULL));

    bool mine = dayUser == curUser;
    if (Button((Rectangle){20, 72, 110, 38}, "< Back", UI_BTN)) { if (mine) GoHome(); else OpenProfile(dayUser); return; }
    if (Button((Rectangle){(float)W - 380, 72, 175, 38}, "< Earlier day", UI_BTN)) { curDay = AddDays(curDay, -1); scDay = scDayRepos = 0; }
    bool canLater = curDay < today;
    if (ButtonEx((Rectangle){(float)W - 195, 72, 175, 38}, "Later day >", canLater ? UI_BTN : UI_PANEL, canLater)) {
        curDay = AddDays(curDay, 1);
        scDay = scDayRepos = 0;
    }

    char title[96];
    if (mine) snprintf(title, sizeof title, "%s%s", FormatDay(curDay, true), curDay == today ? "  (today)" : "");
    else      snprintf(title, sizeof title, "%s on %s", UserName(dayUser), FormatDay(curDay, true));
    DrawFit(title, 150, 75, 30, W - 560, UI_TEXT);

    GatherDay(dayUser, curDay);

    /* one-line summary */
    char b1[48], b2[48];
    int sy = 124;
    if (actTotal == 0) {
        DrawText(TextFormat("%s didn't make any commits or open any issues on this day.", mine ? "You" : UserName(dayUser)),
                 20, sy, FS, UI_MUTED);
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
        int nf = a->kind == 'C' ? CommitFileList(&repos[a->repo], a->idx, kinds, names, MAX_FILES * 2) : 0;
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

            int nf = CommitFileList(r, a->idx, kinds, names, MAX_FILES * 2);
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
/* Talking to real git                                                */
/*                                                                    */
/* Mini GitHub runs a tiny web server on this computer only           */
/* (http://localhost:7979), speaking git's "smart HTTP" language:     */
/*   git clone http://localhost:7979/jo/hello-world.git               */
/*   git push / git pull / git fetch                                  */
/* Pushing needs a Mini GitHub username and password, and only the    */
/* owner and collaborators may push.                                  */
/*                                                                    */
/* git sends and receives commits as a "pack": every object (commit,  */
/* tree, blob) squeezed with zlib, one after another. Inside a pack,  */
/* an object can also be a "delta": "take that other object, copy     */
/* these bits of it and add these new bytes" - which saves space.     */
/* ------------------------------------------------------------------ */
enum { OBJ_COMMIT = 1, OBJ_TREE = 2, OBJ_BLOB = 3, OBJ_TAG = 4, OBJ_OFS_DELTA = 6, OBJ_REF_DELTA = 7 };
static const char *KindName(int t) { return t == OBJ_COMMIT ? "commit" : t == OBJ_TREE ? "tree" : t == OBJ_BLOB ? "blob" : "tag"; }

#define MAX_PUSH   (512u * 1024 * 1024)       /* biggest request git can send: 512 MB */
#define MAX_OBJECT (256u * 1024 * 1024)       /* biggest single object in a pack       */

/* ---------- reading a pack ---------- */
typedef struct {
    size_t   off;                /* where it starts in the pack           */
    int      type;               /* OBJ_..., for deltas the final type    */
    uint8_t *data; size_t len;   /* the object (or, until resolved, the delta) */
    bool     delta, done, busy;
    size_t   baseOff;            /* OBJ_OFS_DELTA: where its base starts  */
    uint8_t  baseSha[20];        /* OBJ_REF_DELTA: its base's fingerprint */
    int      baseType;
    uint8_t  sha[20];
} PObj;
typedef struct { PObj *o; int n, cap; ShaMap map; } PackSet;

static void PackFree(PackSet *ps)
{
    for (int i = 0; i < ps->n; i++) free(ps->o[i].data);
    free(ps->o);
    MapFree(&ps->map);
    memset(ps, 0, sizeof *ps);
}

/* "take bits of base, add new bytes": builds the new object from a delta. */
static uint8_t *ApplyDelta(const uint8_t *base, size_t blen, const uint8_t *d, size_t dlen, size_t *outLen)
{
    size_t p = 0;
    uint64_t sz[2] = {0, 0};
    for (int k = 0; k < 2; k++) {
        int shift = 0;
        uint8_t c;
        do { if (p >= dlen || shift > 56) return NULL; c = d[p++]; sz[k] |= (uint64_t)(c & 0x7F) << shift; shift += 7; } while (c & 0x80);
    }
    if (sz[0] != blen || sz[1] > MAX_OBJECT) return NULL;
    uint8_t *out = malloc((size_t)sz[1] + 1);
    if (!out) return NULL;
    size_t o = 0;
    while (p < dlen) {
        uint8_t c = d[p++];
        if (c & 0x80) {                                   /* copy from the base */
            size_t off = 0, n = 0;
            for (int k = 0; k < 4; k++) if (c & (1 << k)) { if (p >= dlen) goto bad; off |= (size_t)d[p++] << (8 * k); }
            for (int k = 0; k < 3; k++) if (c & (16 << k)) { if (p >= dlen) goto bad; n |= (size_t)d[p++] << (8 * k); }
            if (!n) n = 0x10000;
            if (off + n > blen || o + n > sz[1]) goto bad;
            memcpy(out + o, base + off, n);
            o += n;
        } else if (c) {                                   /* new bytes */
            if (p + c > dlen || o + c > sz[1]) goto bad;
            memcpy(out + o, d + p, c);
            p += c; o += c;
        } else goto bad;
    }
    if (o != sz[1]) goto bad;
    out[o] = 0;
    *outLen = o;
    return out;
bad:
    free(out);
    return NULL;
}

static int PackAt(PackSet *ps, size_t off)
{
    int lo = 0, hi = ps->n - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (ps->o[mid].off == off) return mid;
        if (ps->o[mid].off < off) lo = mid + 1; else hi = mid - 1;
    }
    return -1;
}

/* Something the repository already has, for a delta built on it ("thin" packs). */
static bool RepoObject(Repo *r, const uint8_t *sha, const uint8_t **data, size_t *len, int *type);

/* Turns a delta into the real object (its base first, if that's a delta too). */
static bool Resolve(PackSet *ps, int i, Repo *r)
{
    PObj *o = &ps->o[i];
    if (!o->delta || o->done) return true;
    if (o->busy) return false;                            /* a loop: damaged pack */
    o->busy = true;
    const uint8_t *base = NULL;
    size_t blen = 0;
    int btype = 0;
    if (o->type == OBJ_OFS_DELTA) {
        int b = PackAt(ps, o->baseOff);
        if (b < 0 || !Resolve(ps, b, r)) return false;
        base = ps->o[b].data; blen = ps->o[b].len; btype = ps->o[b].type;
    } else {
        int b = MapGet(&ps->map, o->baseSha);
        if (b >= 0) { if (!Resolve(ps, b, r)) return false; base = ps->o[b].data; blen = ps->o[b].len; btype = ps->o[b].type; }
        else if (!RepoObject(r, o->baseSha, &base, &blen, &btype)) return false;
    }
    size_t nl = 0;
    uint8_t *n = ApplyDelta(base, blen, o->data, o->len, &nl);
    if (!n) return false;
    free(o->data);
    o->data = n; o->len = nl; o->type = btype;
    o->done = true; o->busy = false;
    ObjectSha(KindName(o->type), o->data, o->len, o->sha);
    MapPut(&ps->map, o->sha, i);
    return true;
}

/* Unpacks a whole pack. Returns NULL if it worked, or what was wrong. */
static const char *ReadPack(const uint8_t *p, size_t len, PackSet *ps, Repo *r)
{
    memset(ps, 0, sizeof *ps);
    if (len < 32 || memcmp(p, "PACK", 4) != 0) return "not a git pack";
    uint32_t ver = (uint32_t)p[4] << 24 | (uint32_t)p[5] << 16 | (uint32_t)p[6] << 8 | p[7];
    uint32_t count = (uint32_t)p[8] << 24 | (uint32_t)p[9] << 16 | (uint32_t)p[10] << 8 | p[11];
    if (ver != 2 && ver != 3) return "unknown pack version";
    if (count > 10000000) return "too many objects";
    uint8_t sum[20];
    Sha1 s;
    Sha1Init(&s); Sha1Update(&s, p, len - 20); Sha1Final(&s, sum);
    if (memcmp(sum, p + len - 20, 20) != 0) return "the pack is damaged (checksum)";
    size_t at = 12, end = len - 20;
    for (uint32_t k = 0; k < count; k++) {
        ps->o = GrowArr(ps->o, &ps->cap, ps->n + 1, sizeof(PObj));
        if (ps->n >= ps->cap) return "out of memory";
        PObj *o = &ps->o[ps->n];
        memset(o, 0, sizeof *o);
        o->off = at;
        if (at >= end) return "the pack is cut short";
        uint8_t c = p[at++];
        o->type = (c >> 4) & 7;
        uint64_t size = c & 15;
        int shift = 4;
        while (c & 0x80) { if (at >= end || shift > 56) return "the pack is damaged"; c = p[at++]; size |= (uint64_t)(c & 0x7F) << shift; shift += 7; }
        if (size > MAX_OBJECT) return "an object is too big";
        if (o->type == OBJ_OFS_DELTA) {
            if (at >= end) return "the pack is damaged";
            c = p[at++];
            uint64_t ofs = c & 0x7F;
            while (c & 0x80) { if (at >= end) return "the pack is damaged"; c = p[at++]; ofs = ((ofs + 1) << 7) | (c & 0x7F); }
            if (ofs == 0 || ofs > o->off) return "the pack is damaged (delta)";
            o->baseOff = o->off - (size_t)ofs;
            o->delta = true;
        } else if (o->type == OBJ_REF_DELTA) {
            if (at + 20 > end) return "the pack is damaged";
            memcpy(o->baseSha, p + at, 20);
            at += 20;
            o->delta = true;
        } else if (o->type < 1 || o->type > 4) return "the pack has an unknown kind of object";
        size_t used = 0, ol = 0;
        o->data = InflateZlib(p + at, end - at, &used, &ol, (size_t)size);
        if (!o->data || ol != size) return "the pack is damaged (couldn't unpack an object)";
        o->len = ol;
        at += used;
        if (!o->delta) {
            o->done = true;
            ObjectSha(KindName(o->type), o->data, o->len, o->sha);
            MapPut(&ps->map, o->sha, ps->n);
        }
        ps->n++;
    }
    if (at != end) return "the pack has extra data at the end";
    for (int i = 0; i < ps->n; i++) if (!Resolve(ps, i, r)) return "a delta's base object is missing";
    return NULL;
}

/* ---------- every folder (tree) the repository has ----------
   A push only sends what's new, so a commit's unchanged folders are
   looked up here. Kept between pushes and topped up with new commits. */
static struct {
    const Repo *r; int done; uint8_t last[20];
    ShaMap map; Buf store; size_t *off; int *len; int count, cap;
} tix;
static void TixReset(void)
{
    MapFree(&tix.map); BufFree(&tix.store); free(tix.off); free(tix.len);
    memset(&tix, 0, sizeof tix);
}
static void TixEmit(void *ctx, const uint8_t sha[20], const uint8_t *data, size_t len)
{
    (void)ctx;
    if (MapGet(&tix.map, sha) >= 0) return;
    if (tix.count >= tix.cap) {
        int nc = tix.cap ? tix.cap * 2 : 256;
        size_t *o = realloc(tix.off, (size_t)nc * sizeof *o);
        if (o) tix.off = o;
        int *l = realloc(tix.len, (size_t)nc * sizeof *l);
        if (l) tix.len = l;
        if (!o || !l) return;
        tix.cap = nc;
    }
    tix.off[tix.count] = tix.store.n;
    tix.len[tix.count] = (int)len;
    BufAdd(&tix.store, data, len);
    MapPut(&tix.map, sha, tix.count++);
}
static void TixUpdate(Repo *r)
{
    if (tix.r != r || tix.done > r->commitCount || (tix.done && memcmp(r->commits[tix.done - 1].sha, tix.last, 20) != 0)) {
        TixReset();
        tix.r = r;
    }
    uint8_t t[20];
    for (; tix.done < r->commitCount; tix.done++) CommitTree(r, &r->commits[tix.done], t, TixEmit, NULL);
    if (tix.done) memcpy(tix.last, r->commits[tix.done - 1].sha, 20);
}
static bool RepoObject(Repo *r, const uint8_t *sha, const uint8_t **data, size_t *len, int *type)
{
    int b = MapGet(&r->blobMap, sha);
    if (b >= 0) { *data = (const uint8_t *)r->blobs[b].data; *len = (size_t)r->blobs[b].len; *type = OBJ_BLOB; return true; }
    int c = CommitBySha(r, sha);
    if (c >= 0) { *data = (const uint8_t *)r->commits[c].raw; *len = (size_t)r->commits[c].rawLen; *type = OBJ_COMMIT; return true; }
    TixUpdate(r);
    int t = MapGet(&tix.map, sha);
    if (t >= 0) { *data = tix.store.d + tix.off[t]; *len = (size_t)tix.len[t]; *type = OBJ_TREE; return true; }
    return false;
}

/* ---------- taking in a push ---------- */

/* Whose commit is this? Matched by the email (yours from your profile, or
   the made-up name@minigithub.local), then by name; otherwise it counts
   as the work of the person pushing. */
static int UserForAuthor(const char *name, const char *email, int pusher)
{
    for (int i = 0; i < userCount; i++) if (users[i].email[0] && EqualsCI(users[i].email, email)) return users[i].id;
    for (int i = 0; i < userCount; i++) if (EqualsCI(email, TextFormat("%s@minigithub.local", users[i].name))) return users[i].id;
    for (int i = 0; i < userCount; i++) if (EqualsCI(users[i].name, name)) return users[i].id;
    return pusher;
}

/* One file of a pushed commit, before it's stored. */
typedef struct { const char *path; uint8_t sha[20]; uint8_t mode; } PEntry;
typedef struct { PEntry *e; int n, cap; } PList;

static int CmpPEntry(const void *a, const void *b) { return strcmp(((const PEntry *)a)->path, ((const PEntry *)b)->path); }

/* Unfolds a tree (and the trees inside it) into a flat list of paths. */
static const char *ExpandTree(PackSet *ps, Repo *r, const uint8_t *sha, const char *prefix, PList *out, int depth)
{
    if (depth > 60) return "folders nested too deeply";
    const uint8_t *d = NULL;
    size_t len = 0;
    int type = 0;
    int k = MapGet(&ps->map, sha);
    if (k >= 0) { d = ps->o[k].data; len = ps->o[k].len; type = ps->o[k].type; }
    else if (!RepoObject(r, sha, &d, &len, &type)) return "a folder (tree) is missing from the push";
    if (type != OBJ_TREE) return "something that should be a folder isn't";
    size_t p = 0;
    char path[PATH_LEN + 8];
    while (p < len) {
        const uint8_t *sp = memchr(d + p, ' ', len - p);
        if (!sp) return "a folder is damaged";
        int mlen = (int)(sp - (d + p));
        char mode[8];
        if (mlen <= 0 || mlen > 6) return "a folder is damaged";
        memcpy(mode, d + p, (size_t)mlen); mode[mlen] = 0;
        p += (size_t)mlen + 1;
        const uint8_t *z = memchr(d + p, 0, len - p);
        if (!z || (size_t)(z - d) + 21 > len) return "a folder is damaged";
        const char *name = (const char *)(d + p);
        const uint8_t *esha = z + 1;
        p = (size_t)(z - d) + 21;
        int n = snprintf(path, sizeof path, "%s%s", prefix, name);
        if (n >= PATH_LEN) return TextFormat("a file path is too long (Mini GitHub allows %d characters)", PATH_LEN - 1);
        if (!ValidPath(path)) return TextFormat("\"%.80s\" isn't a file name Mini GitHub can store", path);
        if (strcmp(mode, "40000") == 0) {
            char sub[PATH_LEN + 16];
            snprintf(sub, sizeof sub, "%s/", path);
            const char *err = ExpandTree(ps, r, esha, sub, out, depth + 1);
            if (err) return err;
            continue;
        }
        int m = strcmp(mode, "100644") == 0 ? MODE_FILE : strcmp(mode, "100755") == 0 ? MODE_EXEC : strcmp(mode, "120000") == 0 ? MODE_LINK : -1;
        if (strcmp(mode, "160000") == 0) return "submodules aren't supported";
        if (m < 0) return "a file has an unusual mode";
        if (out->n >= MAX_FILES) return TextFormat("a commit has more than %d files", MAX_FILES);
        out->e = GrowArr(out->e, &out->cap, out->n + 1, sizeof(PEntry));
        if (out->n >= out->cap) return "out of memory";
        PEntry *e = &out->e[out->n++];
        e->path = Intern(path);
        if (!e->path) return "out of memory";
        memcpy(e->sha, esha, 20);
        e->mode = (uint8_t)m;
    }
    return NULL;
}

/* A new commit from the push, checked and ready to store. */
typedef struct { int obj; CommitInfo info; PList files; int state; } PCommit;

/* Updates repository r's branch from old to new using the objects in ps.
   Returns NULL if it worked, or the reason to give git. *added gets how many commits came in. */
static const char *ApplyPush(Repo *r, int pusher, const uint8_t old[20], const uint8_t nw[20], const char *ref, PackSet *ps, int *added)
{
    *added = 0;
    if (strncmp(ref, "refs/tags/", 10) == 0) return "Mini GitHub doesn't keep tags";
    if (strncmp(ref, "refs/heads/", 11) != 0) return "only branches can be pushed";
    const char *br = ref + 11;
    const char *bad = BranchNameProblem(br);
    if (bad) return "Mini GitHub can't use that branch name (letters, numbers and - _ . / only)";
    int bi = FindBranch(r, br);
    Commit *h = bi >= 0 ? &r->commits[r->branches[bi].tip] : NULL;    /* the branch's tip now (NULL = a new branch) */
    if (memcmp(old, h ? h->sha : ZERO_SHA, 20) != 0) return "fetch first - someone else pushed or committed in the meantime";
    if (memcmp(nw, ZERO_SHA, 20) == 0) {                     /* git push --delete */
        if (bi < 0) return NULL;
        if (bi == r->defBranch) return "the default branch can't be deleted (choose another default in Mini GitHub first)";
        const char *e = DeleteBranch(r, bi);
        if (!e) *added = -1;                                 /* "something changed" */
        return e;
    }
    if (h && memcmp(nw, h->sha, 20) == 0) return NULL;          /* nothing new */
    if (bi < 0) {
        if (r->branchCount >= MAX_BRANCHES) return "this repository has as many branches as it can hold";
        for (int b = 0; b < r->branchCount; b++) {           /* git can't have "a" and "a/b" both */
            size_t l = strlen(r->branches[b].name), nl = strlen(br);
            if ((l < nl && strncmp(br, r->branches[b].name, l) == 0 && br[l] == '/') ||
                (nl < l && strncmp(r->branches[b].name, br, nl) == 0 && r->branches[b].name[nl] == '/'))
                return "that name clashes with an existing branch";
        }
    }
    {   /* pushing a commit Mini GitHub already has (e.g. a new branch at an existing commit) */
        int known = CommitBySha(r, nw);
        if (known >= 0) {
            if (h && !IsAncestor(r, (int)(h - r->commits), known))
                return "the push doesn't build on this branch's latest commit - pull first (git pull), then push again";
            if (bi >= 0) r->branches[bi].tip = known;
            else { bi = AddBranch(r, br, known); if (r->branchCount == 1) r->defBranch = r->curBranch = 0; }
            r->gen++;
            *added = -1;
            return NULL;
        }
    }

    /* 1) find every new commit, oldest first (each one after the commits it's built on) */
    PCommit *pc = NULL;
    int pcn = 0, pccap = 0;
    ShaMap seen = {0};
    int *stack = NULL, sn = 0, scap = 0;
    bool reachedHead = false;
    const char *err = NULL;
    int *order = NULL, on = 0, ocap = 0;

    #define PUSH_STACK(x) do { stack = GrowArr(stack, &scap, sn + 1, sizeof(int)); if (sn >= scap) { err = "out of memory"; goto done; } stack[sn++] = (x); } while (0)
    {
        int o = MapGet(&ps->map, nw);
        if (o < 0 || ps->o[o].type != OBJ_COMMIT) { err = "the pushed commit is missing from the pack"; goto done; }
        pc = GrowArr(pc, &pccap, 1, sizeof(PCommit));
        if (!pccap) { err = "out of memory"; goto done; }
        memset(&pc[0], 0, sizeof pc[0]);
        pc[0].obj = o;
        if (!ParseCommit((const char *)ps->o[o].data, (int)ps->o[o].len, &pc[0].info)) { err = "a commit is damaged"; goto done; }
        pcn = 1;
        MapPut(&seen, nw, 0);
        PUSH_STACK(0);
    }
    while (sn) {
        int i = stack[sn - 1];
        if (pc[i].state == 0) {
            pc[i].state = 1;
            for (int q = 0; q < pc[i].info.np; q++) {
                uint8_t ps1[20];                              /* a copy: pc[] may move as it grows */
                memcpy(ps1, pc[i].info.parents[q], 20);
                if (h && memcmp(ps1, h->sha, 20) == 0) reachedHead = true;
                int kn = CommitBySha(r, ps1);
                if (kn >= 0) { if (h && !reachedHead && IsAncestor(r, (int)(h - r->commits), kn)) reachedHead = true; continue; }
                int sp = MapGet(&seen, ps1);
                if (sp >= 0) {
                    /* already found, but not placed yet: it must be placed before this one, so
                       it goes on top of the pile again (the copy further down is skipped later) */
                    if (pc[sp].state == 0) PUSH_STACK(sp);
                    continue;
                }
                int o = MapGet(&ps->map, ps1);
                if (o < 0 || ps->o[o].type != OBJ_COMMIT) {
                    err = h ? "the push doesn't build on Mini GitHub's latest commit - pull first (git pull), then push again"
                            : "a commit is missing from the pack";
                    goto done;
                }
                if (pcn >= MAX_COMMITS) { err = "too many commits"; goto done; }
                pc = GrowArr(pc, &pccap, pcn + 1, sizeof(PCommit));
                if (pcn >= pccap) { err = "out of memory"; goto done; }
                memset(&pc[pcn], 0, sizeof pc[pcn]);
                pc[pcn].obj = o;
                if (!ParseCommit((const char *)ps->o[o].data, (int)ps->o[o].len, &pc[pcn].info)) { err = "a commit is damaged"; goto done; }
                MapPut(&seen, ps1, pcn);
                PUSH_STACK(pcn);
                pcn++;
            }
        } else {
            sn--;
            if (pc[i].state == 1) {
                pc[i].state = 2;
                order = GrowArr(order, &ocap, on + 1, sizeof(int));
                if (on >= ocap) { err = "out of memory"; goto done; }
                order[on++] = i;
            }
        }
    }
    #undef PUSH_STACK
    if (h && !reachedHead) {
        err = "the push doesn't build on Mini GitHub's latest commit - pull first (git pull), then push again";
        goto done;
    }
    if (r->commitCount + on > MAX_COMMITS) { err = "the repository would have too many commits"; goto done; }

    /* every commit must come after the ones it's built on - check before storing anything */
    {
        int *pos = malloc(((size_t)pcn + 1) * sizeof(int));
        if (!pos) { err = "out of memory"; goto done; }
        for (int k = 0; k < on; k++) pos[order[k]] = k;
        for (int k = 0; k < on && !err; k++)
            for (int q = 0; q < pc[order[k]].info.np; q++) {
                int sp = MapGet(&seen, pc[order[k]].info.parents[q]);
                if (sp >= 0 && pos[sp] >= k) { err = "Mini GitHub put the commits in the wrong order (a bug) - nothing was stored"; break; }
            }
        free(pos);
        if (err) goto done;
    }

    /* 2) unfold each commit's folders, and check every file is there */
    for (int k = 0; k < on; k++) {
        PCommit *c = &pc[order[k]];
        err = ExpandTree(ps, r, c->info.tree, "", &c->files, 0);
        if (err) goto done;
        qsort(c->files.e, (size_t)c->files.n, sizeof(PEntry), CmpPEntry);
        const char **names = malloc((size_t)(c->files.n + 1) * sizeof *names);
        uint8_t (*shas)[20] = malloc((size_t)(c->files.n + 1) * 20);
        uint8_t *modes = malloc((size_t)c->files.n + 1);
        if (!names || !shas || !modes) { free(names); free(shas); free(modes); err = "out of memory"; goto done; }
        for (int j = 0; j < c->files.n; j++) {
            PEntry *e = &c->files.e[j];
            if (j && strcmp(e->path, c->files.e[j - 1].path) == 0) { err = "a commit has the same file twice"; break; }
            int o = MapGet(&ps->map, e->sha);
            if ((o < 0 || ps->o[o].type != OBJ_BLOB) && MapGet(&r->blobMap, e->sha) < 0) { err = TextFormat("the contents of %.80s are missing from the push", e->path); break; }
            if (o >= 0 && ps->o[o].len > 0x7FFFFFF0u) { err = "a file is too big"; break; }
            names[j] = e->path; memcpy(shas[j], e->sha, 20); modes[j] = e->mode;
        }
        /* Mini GitHub rebuilds the folders itself, so make sure it gets exactly git's fingerprint */
        uint8_t t[20];
        if (!err && (!BuildTree(names, (const uint8_t (*)[20])shas, modes, 0, c->files.n, 0, t, NULL, NULL) || memcmp(t, c->info.tree, 20) != 0))
            err = "a commit has a folder Mini GitHub can't store exactly (empty or oddly sorted folders?)";
        free(names); free(shas); free(modes);
        if (err) goto done;
    }

    /* 3) all good: store the commits */
    {
        int up = Upstream(r);
        for (int k = 0; k < on; k++) {
            PCommit *pcm = &pc[order[k]];
            PObj *o = &ps->o[pcm->obj];
            Commit c;
            memset(&c, 0, sizeof c);
            int n = pcm->files.n;
            c.fileCount = n;
            c.names = malloc((size_t)(n ? n : 1) * sizeof *c.names);
            c.blobs = malloc((size_t)(n ? n : 1) * sizeof *c.blobs);
            c.modes = malloc((size_t)(n ? n : 1));
            c.raw = malloc(o->len + 1);
            if (!c.names || !c.blobs || !c.modes || !c.raw) { FreeCommit(&c); err = "out of memory"; break; }
            memcpy(c.raw, o->data, o->len + 1);
            c.rawLen = (int)o->len;
            for (int j = 0; j < n && !err; j++) {
                PEntry *e = &pcm->files.e[j];
                int b = MapGet(&r->blobMap, e->sha);
                if (b < 0) {
                    int po = MapGet(&ps->map, e->sha);
                    b = InternBlobSha(r, (const char *)ps->o[po].data, (int)ps->o[po].len, e->sha);
                }
                if (b < 0) err = "the repository is out of storage space";
                c.names[j] = e->path; c.blobs[j] = b; c.modes[j] = e->mode;
            }
            if (err) { FreeCommit(&c); break; }
            for (int q = 0; q < pcm->info.np && q < 2; q++) {
                c.parents[q] = CommitBySha(r, pcm->info.parents[q]);
                if (c.parents[q] < 0) err = "Mini GitHub put the commits in the wrong order (a bug) - nothing was stored";
            }
            if (err) { FreeCommit(&c); break; }
            c.parentCount = pcm->info.np < 2 ? pcm->info.np : 2;
            c.time = pcm->info.atime;
            snprintf(c.message, TEXT_LEN, "%s", pcm->info.summary);
            int inUp = up >= 0 ? CommitBySha(&repos[up], o->sha) : -1;
            if (inUp >= 0) { c.copied = true; c.author = repos[up].commits[inUp].author; }   /* the original's work, not new work */
            else c.author = UserForAuthor(pcm->info.aname, pcm->info.aemail, pusher);
            if (!AppendCommit(r, &c)) { FreeCommit(&c); err = "the repository has too many commits"; break; }
            (*added)++;
        }
        if (!err && *added) {                                  /* the branch moves to the pushed commit */
            int tip = CommitBySha(r, nw);
            if (bi >= 0) r->branches[bi].tip = tip;
            else {
                bi = AddBranch(r, br, tip);
                if (r->branchCount == 1) { r->defBranch = r->curBranch = 0; snprintf(r->branch, NAME_LEN, "%s", br); }
            }
            r->gen++;
        }
    }

done:
    for (int i = 0; i < pcn; i++) free(pc[i].files.e);
    free(pc); free(stack); free(order);
    MapFree(&seen);
    return err;
}

/* ---------- making a pack, for clone / fetch / pull ---------- */

/* A delta: the instructions that turn file version src into version dst
   ("copy bytes 0-4000 of the old one, add these 12 new bytes, copy..."),
   found by looking up 16-byte pieces of dst in an index of src.
   false if it wouldn't come out smaller than limit. */
#define DBLOCK 16
static void DeltaSize(Buf *o, size_t v) { do { uint8_t c = v & 0x7F; v >>= 7; if (v) c |= 0x80; BufByte(o, c); } while (v); }
static void DeltaInsert(Buf *o, const uint8_t *d, size_t n)
{
    while (n) { size_t k = n > 127 ? 127 : n; BufByte(o, (uint8_t)k); BufAdd(o, d, k); d += k; n -= k; }
}
static void DeltaCopy(Buf *o, size_t off, size_t n)
{
    while (n) {
        size_t k = n > 0xFFFFFF ? 0xFFFFFF : n;
        uint8_t cmd = 0x80, b[8];
        int nb = 0;
        for (int i = 0; i < 4; i++) if ((off >> (8 * i)) & 0xFF) { cmd |= (uint8_t)(1 << i); b[nb++] = (uint8_t)(off >> (8 * i)); }
        for (int i = 0; i < 3; i++) if ((k >> (8 * i)) & 0xFF) { cmd |= (uint8_t)(16 << i); b[nb++] = (uint8_t)(k >> (8 * i)); }
        BufByte(o, cmd); BufAdd(o, b, (size_t)nb);
        off += k; n -= k;
    }
}
static uint32_t BlockHash(const uint8_t *p)
{
    uint32_t h = 0;
    for (int i = 0; i < DBLOCK; i++) h = h * 257u + p[i];
    return h;
}
static bool MakeDelta(const uint8_t *src, size_t sn, const uint8_t *dst, size_t dn, Buf *out, size_t limit)
{
    if (sn < DBLOCK * 4 || dn < DBLOCK * 4 || sn > 0x7FFFFFFF) return false;
    size_t nb = sn / DBLOCK, tsize = 64;
    while (tsize < nb * 2) tsize *= 2;
    int *head = malloc(tsize * sizeof(int)), *next = malloc(nb * sizeof(int));
    if (!head || !next) { free(head); free(next); return false; }
    for (size_t i = 0; i < tsize; i++) head[i] = -1;
    int shift = 32;
    for (size_t t = tsize; t > 1; t >>= 1) shift--;
    for (size_t i = 0; i < nb; i++) {
        uint32_t s = (BlockHash(src + i * DBLOCK) * 2654435761u) >> shift;
        next[i] = head[s]; head[s] = (int)i;
    }
    uint32_t top = 1;                                       /* 257^(DBLOCK-1), to roll the hash along */
    for (int i = 0; i < DBLOCK - 1; i++) top *= 257u;
    size_t start = out->n;
    DeltaSize(out, sn); DeltaSize(out, dn);
    size_t j = 0, ins = 0;
    uint32_t h = BlockHash(dst);
    bool ok = true;
    while (j + DBLOCK <= dn) {
        size_t bestLen = 0, bestPos = 0;
        int tries = 0;
        for (int c = head[(h * 2654435761u) >> shift]; c >= 0 && tries < 8; c = next[c], tries++) {
            size_t pos = (size_t)c * DBLOCK;
            if (memcmp(src + pos, dst + j, DBLOCK) != 0) continue;
            size_t len = DBLOCK;
            while (pos + len < sn && j + len < dn && src[pos + len] == dst[j + len]) len++;
            if (len > bestLen) { bestLen = len; bestPos = pos; }
        }
        if (bestLen >= DBLOCK) {
            while (bestPos > 0 && j > ins && src[bestPos - 1] == dst[j - 1]) { bestPos--; j--; bestLen++; }
            DeltaInsert(out, dst + ins, j - ins);
            DeltaCopy(out, bestPos, bestLen);
            j += bestLen; ins = j;
            if (out->n - start > limit) { ok = false; break; }
            if (j + DBLOCK <= dn) h = BlockHash(dst + j);
            continue;
        }
        if (j + DBLOCK < dn) h = (h - dst[j] * top) * 257u + dst[j + DBLOCK];
        j++;
    }
    if (ok) {
        DeltaInsert(out, dst + ins, dn - ins);
        ok = out->n - start <= limit && !out->oom;
    }
    free(head); free(next);
    if (!ok) out->n = start;
    return ok;
}

typedef struct { int type; const uint8_t *data; size_t len; size_t storeOff; bool stored; uint8_t base[20]; } OutObj;
typedef struct { OutObj *o; int n, cap; ShaMap have; Buf store; } OutSet;

static void OutAdd(OutSet *s, int type, const uint8_t sha[20], const uint8_t *data, size_t len, bool copy)
{
    if (MapGet(&s->have, sha) >= 0) return;
    s->o = GrowArr(s->o, &s->cap, s->n + 1, sizeof(OutObj));
    if (s->n >= s->cap) return;
    OutObj *o = &s->o[s->n];
    o->type = type; o->len = len;
    if (copy) { o->stored = true; o->storeOff = s->store.n; BufAdd(&s->store, data, len); o->data = NULL; }
    else { o->stored = false; o->data = data; }
    MapPut(&s->have, sha, s->n++);
}
static void OutTreeEmit(void *ctx, const uint8_t sha[20], const uint8_t *data, size_t len) { OutAdd(ctx, OBJ_TREE, sha, data, len, true); }
static ShaMap knownTrees;                                   /* trees the other side already has */
static void KnownTreeEmit(void *ctx, const uint8_t sha[20], const uint8_t *data, size_t len)
{
    (void)ctx; (void)data; (void)len;
    MapPut(&knownTrees, sha, 1);
}
static void OutTreeEmitNew(void *ctx, const uint8_t sha[20], const uint8_t *data, size_t len)
{
    if (MapGet(&knownTrees, sha) < 0) OutTreeEmit(ctx, sha, data, len);
}

/* Marks commit i and everything it was built on. */
static void MarkAncestors(Repo *r, int i, uint8_t *mark, uint8_t bit)
{
    int *st = NULL, n = 0, cap = 0;
    st = GrowArr(st, &cap, 1, sizeof(int));
    if (!cap) return;
    st[n++] = i;
    while (n) {
        int k = st[--n];
        if (mark[k] & bit) continue;
        mark[k] |= bit;
        CommitInfo ci;
        if (!ParseCommit(r->commits[k].raw, r->commits[k].rawLen, &ci)) continue;
        for (int q = 0; q < ci.np; q++) {
            int p = CommitBySha(r, ci.parents[q]);
            if (p < 0 || (mark[p] & bit)) continue;
            st = GrowArr(st, &cap, n + 1, sizeof(int));
            if (n < cap) st[n++] = p;
        }
    }
    free(st);
}

/* The pack for "I want these commits; I already have those". */
static bool BuildPack(Repo *r, const int *wants, int nw, const int *haves, int nh, Buf *out)
{
    uint8_t *mark = calloc((size_t)(r->commitCount ? r->commitCount : 1), 1);
    uint8_t *sentBlob = calloc((size_t)(r->blobCount ? r->blobCount : 1), 1);
    if (!mark || !sentBlob) { free(mark); free(sentBlob); return false; }
    for (int i = 0; i < nh; i++) MarkAncestors(r, haves[i], mark, 2);
    for (int i = 0; i < nw; i++) MarkAncestors(r, wants[i], mark, 1);

    /* what the other side has right next to what it's getting: don't send those files and folders again */
    MapFree(&knownTrees);
    uint8_t t[20];
    for (int j = 0; j < r->commitCount; j++) {              /* a commit they have, with a child they don't */
        if (mark[j] != 1) continue;
        for (int q = 0; q < r->commits[j].parentCount; q++) {
            int k = r->commits[j].parents[q];
            if (mark[k] & 2) mark[k] |= 4;
        }
    }
    for (int k = 0; k < r->commitCount; k++) {
        if (!(mark[k] & 4)) continue;
        mark[k] &= 3;
        Commit *c = &r->commits[k];
        for (int f = 0; f < c->fileCount; f++) sentBlob[c->blobs[f]] = 1;
        CommitTree(r, c, t, KnownTreeEmit, NULL);
    }

    OutSet s;
    memset(&s, 0, sizeof s);
    /* a file's new version usually goes as a delta of the version before it (if that's in this pack too) */
    uint8_t *depth = calloc((size_t)(r->blobCount ? r->blobCount : 1), 1);   /* 0 = not in the pack; else 1 + delta depth */
    Buf delta = {0};
    for (int k = 0; k < r->commitCount && depth; k++) {
        if (mark[k] != 1) continue;                         /* wanted, and they don't have it */
        Commit *c = &r->commits[k], *p = ParentOf(r, k);
        OutAdd(&s, OBJ_COMMIT, c->sha, (const uint8_t *)c->raw, (size_t)c->rawLen, false);
        CommitTree(r, c, t, OutTreeEmitNew, &s);
        for (int f = 0; f < c->fileCount; f++) {
            int b = c->blobs[f];
            if (sentBlob[b]) continue;
            sentBlob[b] = 1;
            Blob *bl = &r->blobs[b];
            int q = p ? FindInCommit(p, c->names[f]) : -1, pb = q >= 0 ? p->blobs[q] : -1;
            delta.n = 0;
            if (pb >= 0 && depth[pb] && depth[pb] < 50 &&
                MakeDelta((const uint8_t *)r->blobs[pb].data, (size_t)r->blobs[pb].len, (const uint8_t *)bl->data, (size_t)bl->len, &delta, (size_t)bl->len / 2)) {
                int at = s.n;
                OutAdd(&s, OBJ_REF_DELTA, bl->sha, delta.d, delta.n, true);
                if (s.n > at) memcpy(s.o[at].base, r->blobs[pb].sha, 20);
                depth[b] = (uint8_t)(depth[pb] + 1);
            } else {
                OutAdd(&s, OBJ_BLOB, bl->sha, (const uint8_t *)bl->data, (size_t)bl->len, false);
                depth[b] = 1;
            }
        }
    }
    BufFree(&delta);
    bool built = depth != NULL;
    free(depth);
    free(mark); free(sentBlob);
    MapFree(&knownTrees);
    bool ok = !s.store.oom && built;

    /* write it: header, each object (kind + size, then zlib), then a SHA-1 of it all */
    size_t start = out->n;
    BufStr(out, "PACK");
    uint8_t hd[8] = {0, 0, 0, 2, (uint8_t)(s.n >> 24), (uint8_t)(s.n >> 16), (uint8_t)(s.n >> 8), (uint8_t)s.n};
    BufAdd(out, hd, 8);
    for (int i = 0; i < s.n && ok; i++) {
        OutObj *o = &s.o[i];
        const uint8_t *d = o->stored ? s.store.d + o->storeOff : o->data;
        size_t n = o->len;
        uint8_t c = (uint8_t)(o->type << 4 | (n & 15));
        n >>= 4;
        while (n) { BufByte(out, c | 0x80); c = n & 0x7F; n >>= 7; }
        BufByte(out, c);
        if (o->type == OBJ_REF_DELTA) BufAdd(out, o->base, 20);    /* which object it's a delta of */
        ZlibCompress(out, d ? d : (const uint8_t *)"", o->len);
        if (out->oom) ok = false;
    }
    if (ok) {
        Sha1 h;
        uint8_t sum[20];
        Sha1Init(&h); Sha1Update(&h, out->d + start, out->n - start); Sha1Final(&h, sum);
        BufAdd(out, sum, 20);
    }
    free(s.o); MapFree(&s.have); BufFree(&s.store);
    return ok && !out->oom;
}

/* ---------- pkt-lines ----------
   git's messages are "pkt-lines": 4 hex digits giving the length
   (including those 4), then the text. "0000" means "end of this part". */
static void Pkt(Buf *b, const char *s, size_t n)
{
    char hx[5];
    snprintf(hx, sizeof hx, "%04x", (unsigned)(n + 4));
    BufAdd(b, hx, 4);
    BufAdd(b, s, n);
}
static void PktStr(Buf *b, const char *s) { Pkt(b, s, strlen(s)); }
static void PktFlush(Buf *b) { BufAdd(b, "0000", 4); }

/* Reads the next pkt-line. Returns its length (-1 = a flush "0000", -2 = damaged or the end). */
static int PktRead(const uint8_t *d, size_t len, size_t *at, const char **line)
{
    if (*at + 4 > len) return -2;
    int n = 0;
    for (int i = 0; i < 4; i++) {
        if (!isxdigit(d[*at + i])) return -2;
        n = n * 16 + HexVal((char)d[*at + i]);
    }
    *at += 4;
    if (n == 0) return -1;
    if (n < 4 || *at + (size_t)(n - 4) > len) return -2;
    *line = (const char *)d + *at;
    *at += (size_t)(n - 4);
    n -= 4;
    if (n > 0 && (*line)[n - 1] == '\n') n--;             /* the newline isn't part of it */
    return n;
}

/* ---------- network connections ----------
   On Windows the network functions live in ws2_32.dll, which is loaded
   while the program runs - so building Mini GitHub needs nothing extra. */
#ifdef _WIN32
typedef uintptr_t Sock;
#define BAD_SOCK (~(uintptr_t)0)
struct MgAddr { unsigned short family, port; uint32_t addr; char zero[8]; };
__declspec(dllimport) void *__stdcall LoadLibraryA(const char *name);
__declspec(dllimport) void *__stdcall GetProcAddress(void *module, const char *name);
static int  (__stdcall *wsStartup)(unsigned short, void *);
static Sock (__stdcall *wsSocket)(int, int, int);
static int  (__stdcall *wsBind)(Sock, const void *, int);
static int  (__stdcall *wsListen)(Sock, int);
static Sock (__stdcall *wsAccept)(Sock, void *, int *);
static int  (__stdcall *wsRecv)(Sock, char *, int, int);
static int  (__stdcall *wsSend)(Sock, const char *, int, int);
static int  (__stdcall *wsClose)(Sock);
static int  (__stdcall *wsIoctl)(Sock, long, unsigned long *);
static int  (__stdcall *wsError)(void);
static bool NetInit(void)
{
    static int state;                                   /* 0 not tried, 1 ready, -1 failed */
    if (state) return state > 0;
    state = -1;
    void *dll = LoadLibraryA("ws2_32.dll");
    if (!dll) return false;
    *(void **)&wsStartup = GetProcAddress(dll, "WSAStartup");
    *(void **)&wsSocket  = GetProcAddress(dll, "socket");
    *(void **)&wsBind    = GetProcAddress(dll, "bind");
    *(void **)&wsListen  = GetProcAddress(dll, "listen");
    *(void **)&wsAccept  = GetProcAddress(dll, "accept");
    *(void **)&wsRecv    = GetProcAddress(dll, "recv");
    *(void **)&wsSend    = GetProcAddress(dll, "send");
    *(void **)&wsClose   = GetProcAddress(dll, "closesocket");
    *(void **)&wsIoctl   = GetProcAddress(dll, "ioctlsocket");
    *(void **)&wsError   = GetProcAddress(dll, "WSAGetLastError");
    if (!wsStartup || !wsSocket || !wsBind || !wsListen || !wsAccept || !wsRecv || !wsSend || !wsClose || !wsIoctl || !wsError) return false;
    static uint8_t wsaData[1024];
    if (wsStartup(0x0202, wsaData) != 0) return false;
    state = 1;
    return true;
}
static Sock SockListen(int port)
{
    Sock s = wsSocket(2 /* AF_INET */, 1 /* SOCK_STREAM */, 6 /* TCP */);
    if (s == BAD_SOCK) return BAD_SOCK;
    struct MgAddr a = {2, (unsigned short)((port >> 8) | ((port & 0xFF) << 8)), 0x0100007Fu, {0}};   /* 127.0.0.1 */
    unsigned long on = 1;
    if (wsBind(s, &a, (int)sizeof a) != 0 || wsListen(s, 16) != 0 || wsIoctl(s, (long)0x8004667Eu /* FIONBIO */, &on) != 0) { wsClose(s); return BAD_SOCK; }
    return s;
}
static Sock SockAccept(Sock l)
{
    Sock s = wsAccept(l, NULL, NULL);
    if (s == BAD_SOCK) return BAD_SOCK;
    unsigned long on = 1;
    wsIoctl(s, (long)0x8004667Eu, &on);
    return s;
}
/* >0 bytes, 0 = closed, -1 = nothing yet, -2 = broken */
static int SockRecv(Sock s, uint8_t *b, int n)
{
    int r = wsRecv(s, (char *)b, n, 0);
    if (r > 0) return r;
    if (r == 0) return 0;
    return wsError() == 10035 /* WSAEWOULDBLOCK */ ? -1 : -2;
}
static int SockSend(Sock s, const uint8_t *b, int n)
{
    int r = wsSend(s, (const char *)b, n, 0);
    if (r >= 0) return r;
    return wsError() == 10035 ? -1 : -2;
}
static void SockClose(Sock s) { wsClose(s); }
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
typedef int Sock;
#define BAD_SOCK (-1)
static bool NetInit(void)
{
    signal(SIGPIPE, SIG_IGN);                            /* a client hanging up mustn't stop the program */
    return true;
}
static Sock SockListen(int port)
{
    Sock s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return BAD_SOCK;
    int on = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons((unsigned short)port);
    a.sin_addr.s_addr = htonl(0x7F000001u);              /* 127.0.0.1: this computer only */
    if (bind(s, (struct sockaddr *)&a, sizeof a) != 0 || listen(s, 16) != 0) { close(s); return BAD_SOCK; }
    fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);
    return s;
}
static Sock SockAccept(Sock l)
{
    Sock s = accept(l, NULL, NULL);
    if (s < 0) return BAD_SOCK;
    fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);
#ifdef SO_NOSIGPIPE
    int on = 1;
    setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof on);
#endif
    return s;
}
static int SockRecv(Sock s, uint8_t *b, int n)
{
    ssize_t r = recv(s, b, (size_t)n, 0);
    if (r > 0) return (int)r;
    if (r == 0) return 0;
    return (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) ? -1 : -2;
}
static int SockSend(Sock s, const uint8_t *b, int n)
{
    ssize_t r = send(s, b, (size_t)n, 0);
    if (r >= 0) return (int)r;
    return (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) ? -1 : -2;
}
static void SockClose(Sock s) { close(s); }
#endif

/* ---------- the web server ----------
   It runs inside the normal frame loop: each frame it takes in whatever
   has arrived and sends what it can, without ever waiting. */
#define GIT_PORT  7979
#define MAX_CONNS 16
typedef struct {
    bool used; Sock s; Buf in, out; size_t sent; bool replied, continued; double last;
    int headLen;                         /* bytes of the request's header, once it's all arrived */
} Conn;
static Conn conns[MAX_CONNS];
static Sock gitListen = BAD_SOCK;
static int  gitPushes, gitClones;        /* since the program started */

static void GitServerStart(void)
{
    if (!NetInit()) { snprintf(gitProblem, sizeof gitProblem, "this computer's network functions couldn't be loaded"); return; }
    for (int p = GIT_PORT; p < GIT_PORT + 10; p++) {
        gitListen = SockListen(p);
        if (gitListen != BAD_SOCK) { gitPort = p; return; }
    }
    snprintf(gitProblem, sizeof gitProblem, "ports %d to %d are all in use by other programs", GIT_PORT, GIT_PORT + 9);
}
static void GitServerStop(void)
{
    for (int i = 0; i < MAX_CONNS; i++) if (conns[i].used) { SockClose(conns[i].s); BufFree(&conns[i].in); BufFree(&conns[i].out); }
    if (gitListen != BAD_SOCK) SockClose(gitListen);
    gitListen = BAD_SOCK;
    gitPort = 0;
}
/* The address git uses for a repository. */
static const char *CloneUrl(Repo *r)
{
    return TextFormat("http://localhost:%d/%s/%s.git", gitPort ? gitPort : GIT_PORT, UserName(r->owner), r->name);
}

/* What arrived: just the parts Mini GitHub needs. */
typedef struct {
    char method[8], path[512], query[256];
    long long contentLength;
    bool chunked, gzip, expect;
    char auth[512], host[128];
    const uint8_t *body; size_t bodyLen;
    uint8_t *bodyOwned;                  /* if the body had to be put back together */
} Req;

static const char *Status(int code)
{
    switch (code) {
    case 100: return "Continue";        case 200: return "OK";
    case 400: return "Bad Request";     case 401: return "Unauthorized";
    case 403: return "Forbidden";       case 404: return "Not Found";
    case 405: return "Method Not Allowed"; case 413: return "Payload Too Large";
    default:  return "Internal Server Error";
    }
}
static void Reply(Conn *c, int code, const char *type, const uint8_t *body, size_t n, const char *extra)
{
    char head[640];
    int hn = snprintf(head, sizeof head,
                      "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %llu\r\nCache-Control: no-cache\r\n"
                      "Connection: close\r\nServer: Mini-GitHub\r\n%s\r\n",
                      code, Status(code), type, (unsigned long long)n, extra ? extra : "");
    BufAdd(&c->out, head, (size_t)hn);
    BufAdd(&c->out, body, n);
    c->replied = true;
}
static void ReplyText(Conn *c, int code, const char *text, const char *extra)
{
    Reply(c, code, "text/plain; charset=utf-8", (const uint8_t *)text, strlen(text), extra);
}

/* Header lines are "Name: value"; case doesn't matter in the name. */
static bool HeaderIs(const char *line, int n, const char *name, const char **val, int *vn)
{
    int k = (int)strlen(name);
    if (n <= k || line[k] != ':') return false;
    for (int i = 0; i < k; i++) if (tolower((unsigned char)line[i]) != tolower((unsigned char)name[i])) return false;
    int s = k + 1;
    while (s < n && line[s] == ' ') s++;
    *val = line + s;
    *vn = n - s;
    return true;
}
static bool ValueHas(const char *v, int n, const char *word)
{
    int k = (int)strlen(word);
    for (int i = 0; i + k <= n; i++) {
        int j = 0;
        while (j < k && tolower((unsigned char)v[i + j]) == word[j]) j++;
        if (j == k) return true;
    }
    return false;
}

/* Reads the request line and headers. false if they don't make sense. */
static bool ParseHead(const uint8_t *d, int len, Req *q)
{
    memset(q, 0, sizeof *q);
    q->contentLength = -1;
    int p = 0, line = 0;
    while (p < len) {
        int e = p;
        while (e + 1 < len && !(d[e] == '\r' && d[e + 1] == '\n')) e++;
        const char *l = (const char *)d + p;
        int n = e - p;
        if (n == 0) break;
        if (line++ == 0) {
            char target[800];
            if (sscanf(l, "%7s %799s", q->method, target) != 2) return false;
            char *qm = strchr(target, '?');
            if (qm) { snprintf(q->query, sizeof q->query, "%s", qm + 1); *qm = 0; }
            /* undo %20-style escapes */
            int j = 0;
            for (int i = 0; target[i] && j < (int)sizeof q->path - 1; i++) {
                if (target[i] == '%' && isxdigit((unsigned char)target[i + 1]) && isxdigit((unsigned char)target[i + 2])) {
                    q->path[j++] = (char)(HexVal(target[i + 1]) * 16 + HexVal(target[i + 2])); i += 2;
                } else q->path[j++] = target[i];
            }
            q->path[j] = 0;
        } else {
            const char *v;
            int vn;
            if (HeaderIs(l, n, "Content-Length", &v, &vn)) q->contentLength = strtoll(v, NULL, 10);
            else if (HeaderIs(l, n, "Transfer-Encoding", &v, &vn)) q->chunked = ValueHas(v, vn, "chunked");
            else if (HeaderIs(l, n, "Content-Encoding", &v, &vn)) q->gzip = ValueHas(v, vn, "gzip");
            else if (HeaderIs(l, n, "Expect", &v, &vn)) q->expect = ValueHas(v, vn, "100-continue");
            else if (HeaderIs(l, n, "Authorization", &v, &vn)) snprintf(q->auth, sizeof q->auth, "%.*s", vn, v);
            else if (HeaderIs(l, n, "Host", &v, &vn)) snprintf(q->host, sizeof q->host, "%.*s", vn, v);
        }
        p = e + 2;
    }
    return line > 0;
}

/* "chunked": the body arrives in pieces, each with its size in hex first.
   Returns 1 when it's all there (and puts it together), 0 if not yet, -1 if damaged. */
static int Unchunk(const uint8_t *d, size_t len, Req *q)
{
    size_t p = 0;
    Buf b = {0};
    for (;;) {
        size_t e = p;
        while (e + 1 < len && !(d[e] == '\r' && d[e + 1] == '\n')) e++;
        if (e + 1 >= len) { BufFree(&b); return 0; }
        unsigned long long n = 0;
        size_t i = p;
        if (i == e || !isxdigit(d[i])) { BufFree(&b); return -1; }
        while (i < e && isxdigit(d[i])) { n = n * 16 + (unsigned long long)HexVal((char)d[i]); i++; if (n > MAX_PUSH) { BufFree(&b); return -1; } }
        p = e + 2;
        if (n == 0) {                                        /* the end (skip any trailer lines) */
            for (;;) {
                size_t e2 = p;
                while (e2 + 1 < len && !(d[e2] == '\r' && d[e2 + 1] == '\n')) e2++;
                if (e2 + 1 >= len) { BufFree(&b); return 0; }
                if (e2 == p) break;
                p = e2 + 2;
            }
            BufByte(&b, 0);
            if (b.oom) { BufFree(&b); return -1; }
            q->bodyOwned = b.d;
            q->body = b.d;
            q->bodyLen = b.n - 1;
            return 1;
        }
        if (p + n + 2 > len) { BufFree(&b); return 0; }
        BufAdd(&b, d + p, (size_t)n);
        p += (size_t)n + 2;
    }
}

/* ---------- who's asking ----------
   git sends "Authorization: Basic <base64 of name:password>". Checking a
   password takes a moment (that's what makes guessing slow), so a correct
   one is remembered for a few minutes. */
static int B64Val(int c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+' || c == '-') return 62;
    if (c == '/' || c == '_') return 63;
    return -1;
}
static struct { uint8_t key[32]; int uid; double until; } authMemo[8];
static double authBlockedUntil;
static int authFails;

/* The user id if the request carries a right name + password, else -1. */
static int Authenticate(Req *q)
{
    if (strncmp(q->auth, "Basic ", 6) != 0) return -1;
    uint8_t key[32];
    Sha256 h;
    ShaInit(&h); ShaUpdate(&h, q->auth, strlen(q->auth)); ShaFinal(&h, key);
    double now = GetTime();
    for (int i = 0; i < 8; i++)
        if (authMemo[i].until > now && memcmp(authMemo[i].key, key, 32) == 0 && FindUserById(authMemo[i].uid)) return authMemo[i].uid;
    if (now < authBlockedUntil) return -1;                  /* too many wrong passwords: wait a little */

    char dec[400];
    int n = 0;
    unsigned acc = 0;
    int bits = 0;
    for (const char *s = q->auth + 6; *s && n < (int)sizeof dec - 1; s++) {
        int v = B64Val((unsigned char)*s);
        if (v < 0) continue;
        acc = acc << 6 | (unsigned)v; bits += 6;
        if (bits >= 8) { bits -= 8; dec[n++] = (char)((acc >> bits) & 0xFF); }
    }
    dec[n] = 0;
    char *colon = strchr(dec, ':');
    int uid = -1;
    if (colon) {
        *colon = 0;
        User *u = FindUserByName(dec);
        if (u && CheckPassword(u, colon + 1)) uid = u->id;
    }
    memset(dec, 0, sizeof dec);
    if (uid < 0) {
        if (++authFails >= 5) { authBlockedUntil = now + 10; authFails = 0; }
        return -1;
    }
    authFails = 0;
    int slot = 0;
    for (int i = 1; i < 8; i++) if (authMemo[i].until < authMemo[slot].until) slot = i;
    memcpy(authMemo[slot].key, key, 32);
    authMemo[slot].uid = uid;
    authMemo[slot].until = now + 300;
    return uid;
}
static void AskForPassword(Conn *c)
{
    ReplyText(c, 401, "Mini GitHub: sign in with your Mini GitHub username and password.\n",
              "WWW-Authenticate: Basic realm=\"Mini GitHub\"\r\n");
}

/* ---------- git's requests ---------- */
#define GIT_AGENT "agent=mini-github/1.0"

/* "GET /owner/repo.git/info/refs?service=git-upload-pack": the list of branches. */
static void InfoRefs(Conn *c, Req *q, Repo *r, bool push)
{
    Buf b = {0};
    char hx[41], line[512];
    PktStr(&b, push ? "# service=git-receive-pack\n" : "# service=git-upload-pack\n");
    PktFlush(&b);
    char caps[256];
    if (push) snprintf(caps, sizeof caps, "report-status delete-refs no-thin ofs-delta %s", GIT_AGENT);
    else      snprintf(caps, sizeof caps, "multi_ack_detailed symref=HEAD:refs/heads/%s %s", DefBranchName(r), GIT_AGENT);
    if (!r->branchCount) {
        int n = snprintf(line, sizeof line, "%040d capabilities^{}%c%s\n", 0, 0, caps);
        Pkt(&b, line, (size_t)n);
    } else {
        /* the default branch as HEAD (for clone), then every branch, in name order */
        int order[MAX_BRANCHES];
        for (int i = 0; i < r->branchCount; i++) order[i] = i;
        for (int i = 0; i < r->branchCount; i++)
            for (int j = i + 1; j < r->branchCount; j++)
                if (strcmp(r->branches[order[j]].name, r->branches[order[i]].name) < 0) { int t = order[i]; order[i] = order[j]; order[j] = t; }
        bool first = true;
        if (!push) {
            ShaHex(r->commits[DefaultTip(r)].sha, hx);
            int n = snprintf(line, sizeof line, "%s HEAD%c%s\n", hx, 0, caps);
            Pkt(&b, line, (size_t)n);
            first = false;
        }
        for (int i = 0; i < r->branchCount; i++) {
            Branch *br = &r->branches[order[i]];
            ShaHex(r->commits[br->tip].sha, hx);
            int n = first ? snprintf(line, sizeof line, "%s refs/heads/%s%c%s\n", hx, br->name, 0, caps)
                          : snprintf(line, sizeof line, "%s refs/heads/%s\n", hx, br->name);
            Pkt(&b, line, (size_t)n);
            first = false;
        }
    }
    PktFlush(&b);
    (void)q;
    Reply(c, 200, push ? "application/x-git-receive-pack-advertisement" : "application/x-git-upload-pack-advertisement", b.d, b.n, NULL);
    BufFree(&b);
}

/* "POST /owner/repo.git/git-upload-pack": clone, fetch and pull.
   git says which commits it wants, then (in rounds) which it already has;
   Mini GitHub says "ACK" to each one it knows too, and once git says
   "done", answers with a pack of everything missing. This is git's
   "multi_ack_detailed" way of talking, which is what git expects over HTTP. */
static void UploadPack(Conn *c, Req *q, Repo *r)
{
    const uint8_t *d = q->body;
    size_t len = q->bodyLen, at = 0, ul = 0;
    uint8_t *unz = NULL;
    if (q->gzip) {
        unz = InflateGzip(d, len, &ul, MAX_PUSH);
        if (!unz) { ReplyText(c, 400, "couldn't unpack the request\n", NULL); return; }
        d = unz; len = ul;
    }
    int *wants = NULL, nw = 0, wcap = 0, *haves = NULL, nh = 0, hcap = 0;
    const char *l;
    int n;
    bool done = false, bad = false;
    Buf b = {0};
    /* the wants, up to a flush */
    while ((n = PktRead(d, len, &at, &l)) >= 0) {
        uint8_t sha[20];
        if (n >= 45 && strncmp(l, "want ", 5) == 0 && ParseHex40(l + 5, sha)) {
            int k = CommitBySha(r, sha);
            if (k < 0) { bad = true; break; }
            wants = GrowArr(wants, &wcap, nw + 1, sizeof(int));
            if (nw < wcap) wants[nw++] = k;
        }
    }
    if (bad) { PktStr(&b, TextFormat("ERR upload-pack: not our ref\n")); }
    else if (n == -1 && nw) {
        /* then what they have */
        char last[41] = "";
        while ((n = PktRead(d, len, &at, &l)) != -2) {
            if (n == -1) { PktStr(&b, "NAK\n"); break; }                   /* the end of a round, without "done" */
            uint8_t sha[20];
            if (n >= 45 && strncmp(l, "have ", 5) == 0 && ParseHex40(l + 5, sha)) {
                int k = CommitBySha(r, sha);
                if (k < 0) continue;
                haves = GrowArr(haves, &hcap, nh + 1, sizeof(int));
                if (nh < hcap) haves[nh++] = k;
                ShaHex(sha, last);
                PktStr(&b, TextFormat("ACK %s common\n", last));
            } else if (n == 4 && strncmp(l, "done", 4) == 0) { done = true; break; }
        }
        if (done) {
            if (nh) PktStr(&b, TextFormat("ACK %s\n", last));
            else    PktStr(&b, "NAK\n");
            if (!BuildPack(r, wants, nw, haves, nh, &b)) { BufFree(&b); ReplyText(c, 500, "out of memory\n", NULL); goto out; }
            gitClones++;
        }
    }
    Reply(c, 200, "application/x-git-upload-pack-result", b.d ? b.d : (const uint8_t *)"", b.n, NULL);
    BufFree(&b);
out:
    free(wants); free(haves); free(unz);
}

/* "POST /owner/repo.git/git-receive-pack": git push.
   First the branch updates ("old new refs/heads/main"), then a pack. */
static void ReceivePack(Conn *c, Req *q, Repo *r, int uid)
{
    const uint8_t *d = q->body;
    size_t len = q->bodyLen, at = 0;
    typedef struct { uint8_t old[20], nw[20]; char ref[256]; } Cmd;
    Cmd cmds[16];
    int nc = 0;
    const char *l;
    int n;
    while ((n = PktRead(d, len, &at, &l)) >= 0) {
        if (nc >= 16) continue;
        int k = 0;
        while (k < n && l[k]) k++;                         /* the first line has extra settings after a zero byte */
        if (k < 83 || l[40] != ' ' || l[81] != ' ') continue;
        if (!ParseHex40(l, cmds[nc].old) || !ParseHex40(l + 41, cmds[nc].nw)) continue;
        snprintf(cmds[nc].ref, sizeof cmds[nc].ref, "%.*s", k - 82, l + 82);
        nc++;
    }
    if (!nc) { Reply(c, 200, "application/x-git-receive-pack-result", (const uint8_t *)"", 0, NULL); return; }

    Buf b = {0};
    PackSet ps;
    memset(&ps, 0, sizeof ps);
    const char *perr = NULL;
    if (at < len) perr = ReadPack(d + at, len - at, &ps, r);
    if (perr) {
        PktStr(&b, TextFormat("unpack %s\n", perr));
        for (int i = 0; i < nc; i++) PktStr(&b, TextFormat("ng %s unpacker error\n", cmds[i].ref));
    } else {
        PktStr(&b, "unpack ok\n");
        int total = 0;
        bool changed = false;
        for (int i = 0; i < nc; i++) {
            const char *why = NULL;
            int added = 0;
            if (!CanEditBy(r, uid))
                why = TextFormat("%s can't push here - only %s and invited collaborators can", UserName(uid), UserName(r->owner));
            if (!why) why = ApplyPush(r, uid, cmds[i].old, cmds[i].nw, cmds[i].ref, &ps, &added);
            if (why) PktStr(&b, TextFormat("ng %s %s\n", cmds[i].ref, why));
            else     PktStr(&b, TextFormat("ok %s\n", cmds[i].ref));
            if (added) changed = true;
            if (added > 0) total += added;
        }
        if (changed) {
            MarkDirty();
            ForgetScratch();
            gitPushes++;
            if (total) Toast(TextFormat("%s pushed %d commit%s to %s", UserName(uid), total, total == 1 ? "" : "s", r->name));
            else       Toast(TextFormat("%s updated the branches of %s", UserName(uid), r->name));
        }
    }
    PktFlush(&b);
    PackFree(&ps);
    Reply(c, 200, "application/x-git-receive-pack-result", b.d, b.n, NULL);
    BufFree(&b);
}

/* Works out what a complete request is for, and answers it. */
static void HandleRequest(Conn *c, Req *q)
{
    /* Only requests addressed to this computer: a web page can't trick your browser into reaching it */
    char hostOnly[128];
    snprintf(hostOnly, sizeof hostOnly, "%s", q->host);
    char *colon = hostOnly[0] == '[' ? strchr(hostOnly, ']') : strchr(hostOnly, ':');
    if (colon) colon[hostOnly[0] == '['] = 0;
    if (q->host[0] && !EqualsCI(hostOnly, "localhost") && strcmp(hostOnly, "127.0.0.1") != 0 && strcmp(hostOnly, "[::1]") != 0) {
        ReplyText(c, 403, "Mini GitHub only answers requests to localhost\n", NULL);
        return;
    }
    if (strcmp(q->path, "/") == 0) {
        ReplyText(c, 200, "This is Mini GitHub's git server.\n"
                          "Use it with git, for example:  git clone http://localhost:7979/<user>/<repository>.git\n", NULL);
        return;
    }
    /* /owner/repo(.git)/rest */
    char owner[NAME_LEN], name[NAME_LEN + 8], rest[128];
    const char *p = q->path;
    if (*p++ != '/') { ReplyText(c, 404, "Not found\n", NULL); return; }
    const char *s1 = strchr(p, '/');
    if (!s1 || s1 - p >= NAME_LEN) { ReplyText(c, 404, "Not found\n", NULL); return; }
    snprintf(owner, sizeof owner, "%.*s", (int)(s1 - p), p);
    p = s1 + 1;
    const char *s2 = strchr(p, '/');
    if (!s2 || s2 - p >= NAME_LEN + 4) { ReplyText(c, 404, "Not found\n", NULL); return; }
    snprintf(name, sizeof name, "%.*s", (int)(s2 - p), p);
    snprintf(rest, sizeof rest, "%s", s2 + 1);
    size_t nl = strlen(name);
    if (nl > 4 && strcmp(name + nl - 4, ".git") == 0) name[nl - 4] = 0;

    User *ou = FindUserByName(owner);
    int ri = ou ? FindRepoOf(ou->id, name) : -1;
    if (ri < 0) {
        ReplyText(c, 404, TextFormat("Mini GitHub has no repository called %s/%s\n", owner, name), NULL);
        return;
    }
    Repo *r = &repos[ri];
    bool wantPush = strcmp(rest, "git-receive-pack") == 0 || (strcmp(rest, "info/refs") == 0 && strstr(q->query, "service=git-receive-pack"));
    bool wantPull = strcmp(rest, "git-upload-pack") == 0 || (strcmp(rest, "info/refs") == 0 && strstr(q->query, "service=git-upload-pack"));
    if (!wantPush && !wantPull) {
        ReplyText(c, 404, "Mini GitHub only speaks git's \"smart\" protocol - please use a recent version of git.\n", NULL);
        return;
    }
    int uid = -1;
    if (wantPush) {
        uid = Authenticate(q);
        if (uid < 0) { AskForPassword(c); return; }
    }
    if (strcmp(rest, "info/refs") == 0) {
        if (strcmp(q->method, "GET") != 0) { ReplyText(c, 405, "Use GET\n", NULL); return; }
        InfoRefs(c, q, r, wantPush);
        return;
    }
    if (strcmp(q->method, "POST") != 0) { ReplyText(c, 405, "Use POST\n", NULL); return; }
    if (wantPull) UploadPack(c, q, r);
    else          ReceivePack(c, q, r, uid);
}

static void DropConn(Conn *c)
{
    SockClose(c->s);
    BufFree(&c->in); BufFree(&c->out);
    memset(c, 0, sizeof *c);
}

/* Called once per frame: new connections, incoming data, replies going out. */
static void GitServerPoll(void)
{
    if (gitListen == BAD_SOCK) return;
    double now = GetTime();
    for (;;) {
        Sock s = SockAccept(gitListen);
        if (s == BAD_SOCK) break;
        int k = -1;
        for (int i = 0; i < MAX_CONNS; i++) if (!conns[i].used) { k = i; break; }
        if (k < 0) { SockClose(s); continue; }
        memset(&conns[k], 0, sizeof conns[k]);
        conns[k].used = true; conns[k].s = s; conns[k].last = now;
    }
    static uint8_t chunk[256 * 1024];
    for (int i = 0; i < MAX_CONNS; i++) {
        Conn *c = &conns[i];
        if (!c->used) continue;
        if (!c->replied) {
            size_t got = 0;
            bool closed = false;
            while (got < 64u * 1024 * 1024) {               /* take up to 64 MB a frame */
                int r = SockRecv(c->s, chunk, (int)sizeof chunk);
                if (r == -1) break;
                if (r <= 0) { closed = true; break; }
                BufAdd(&c->in, chunk, (size_t)r);
                got += (size_t)r;
                c->last = now;
            }
            if (c->in.oom || c->in.n > MAX_PUSH + 65536) { ReplyText(c, 413, "That's too big for Mini GitHub\n", NULL); }
            else if (c->in.n) {
                /* has the whole request arrived? */
                if (!c->headLen) {
                    for (size_t k = 3; k < c->in.n; k++)
                        if (c->in.d[k - 3] == '\r' && c->in.d[k - 2] == '\n' && c->in.d[k - 1] == '\r' && c->in.d[k] == '\n') { c->headLen = (int)k + 1; break; }
                    if (!c->headLen && c->in.n > 16384) ReplyText(c, 400, "Bad request\n", NULL);
                }
                if (c->headLen && !c->replied) {
                    Req q;
                    if (!ParseHead(c->in.d, c->headLen, &q)) ReplyText(c, 400, "Bad request\n", NULL);
                    else {
                        const uint8_t *body = c->in.d + c->headLen;
                        size_t have = c->in.n - (size_t)c->headLen;
                        int ready = 0;
                        if (q.chunked) ready = Unchunk(body, have, &q);
                        else if (q.contentLength > (long long)MAX_PUSH) ready = -2;
                        else if (q.contentLength <= 0 || have >= (size_t)q.contentLength) {
                            ready = 1;
                            q.body = body;
                            q.bodyLen = q.contentLength > 0 ? (size_t)q.contentLength : 0;
                        }
                        if (ready == 0 && q.expect && !c->continued) {          /* "go ahead and send the rest" */
                            const char *go = "HTTP/1.1 100 Continue\r\n\r\n";
                            SockSend(c->s, (const uint8_t *)go, (int)strlen(go));
                            c->continued = true;
                        }
                        if (ready == 1) HandleRequest(c, &q);
                        else if (ready == -1) ReplyText(c, 400, "Bad request\n", NULL);
                        else if (ready == -2) ReplyText(c, 413, "That's too big for Mini GitHub\n", NULL);
                        free(q.bodyOwned);
                    }
                }
            }
            if (closed && !c->replied) { DropConn(c); continue; }
        }
        if (c->replied) {
            while (c->sent < c->out.n) {
                size_t left = c->out.n - c->sent;
                int r = SockSend(c->s, c->out.d + c->sent, (int)(left > (1u << 20) ? (1u << 20) : left));
                if (r == -1) break;
                if (r < 0) { c->sent = c->out.n; break; }
                c->sent += (size_t)r;
                c->last = now;
            }
            if (c->sent >= c->out.n) { DropConn(c); continue; }
        }
        if (now - c->last > 120) DropConn(c);               /* given up on */
    }
}

/* ---------- when the save file can't be read ----------
   Nothing is deleted or saved over: the file stays exactly as it is until
   you choose. Starting empty first moves it aside under a new name (never
   replacing another file), so it can always be brought back. */
static bool quitNow;

/* "minigithub.dat.unreadable-2026-10-21-1103", or "...-2" etc. if that's taken */
static const char *SetAsideName(void)
{
    static char nm[128];
    time_t t = time(NULL);
    char stamp[32];
    strftime(stamp, sizeof stamp, "%Y-%m-%d-%H%M%S", localtime(&t));
    snprintf(nm, sizeof nm, "%s.unreadable-%s", SAVE_FILE, stamp);
    for (int i = 2; FileExists(nm) && i < 1000; i++) snprintf(nm, sizeof nm, "%s.unreadable-%s-%d", SAVE_FILE, stamp, i);
    return nm;
}

static void AfterLoad(void)
{
    loadFailed = false;
    GitServerStart();
    if (userCount == 0) {
        GoRegister();
        if (OrphanCount() > 0) Toast("Create an account to take over your existing repositories");
    } else GoLogin();
}

static void ScreenLoadFail(void)
{
    float w = W - 120 > 900 ? 900 : (float)W - 120;
    Rectangle p = {((float)W - w) / 2, 100, w, (float)H - 160};
    DrawRectangleRec(p, UI_PANEL);
    DrawRectangleLinesEx(p, 1, UI_ORANGE);
    int x = (int)p.x + 32, y = (int)p.y + 28, tw = (int)w - 64;
    DrawBold("Mini GitHub couldn't read its save file", x, y, 30, UI_ORANGE);
    y += 56;
    y += WrapText("Nothing has been deleted, and nothing will be saved over it. The file is still there, exactly as it was:", x, y, tw, FS, UI_TEXT, true) + 8;
    y += WrapText(TextFormat("%s   (in %s)", SAVE_FILE, GetWorkingDirectory()), x + 20, y, tw - 20, FS, UI_LINK, true) + 16;
    y += WrapText(TextFormat("What went wrong: %s%s%s.", loadWhy, loadWhere[0] ? ", in " : "", loadWhere), x, y, tw, FS, UI_MUTED, true) + 16;
    bool lastGood = FileExists(SAVE_FILE ".last-good");
    if (lastGood)
        y += WrapText("There's also a copy of your data from the last time Mini GitHub opened without problems (minigithub.dat.last-good).",
                      x, y, tw, FS, UI_MUTED, true) + 16;
    y += WrapText("If this file came from a newer version of Mini GitHub, quit and open it with that version instead.", x, y, tw, FS, UI_MUTED, true);

    float by = p.y + p.height - 70, bx = p.x + 32;
    if (Button((Rectangle){bx, by, 140, 44}, "Quit", UI_BTN)) quitNow = true;
    bx += 156;
    if (lastGood) {
        if (ConfirmButton((Rectangle){bx, by, 330, 44}, "Use the last good copy", "Click again to use it", 70)) {
            const char *aside = SetAsideName();
            if (rename(SAVE_FILE, aside) != 0) { Toast("Couldn't move the unreadable file aside"); return; }
            int sz = 0;
            unsigned char *d = LoadFileData(SAVE_FILE ".last-good", &sz);
            bool ok = d && SaveFileData(SAVE_FILE, d, sz);
            if (d) UnloadFileData(d);
            loadFailed = false;
            if (ok && Load()) { AfterLoad(); Toast(TextFormat("Restored the last good copy - the unreadable file is kept as %s", GetFileName(aside))); }
            else { rename(aside, SAVE_FILE); loadFailed = true; Toast("That copy couldn't be read either - nothing was changed"); }
            return;
        }
        bx += 346;
    }
    if (ConfirmButton((Rectangle){bx, by, 300, 44}, "Start empty", "Click again to start empty", 71)) {
        const char *aside = SetAsideName();
        if (rename(SAVE_FILE, aside) != 0) { Toast("Couldn't move the unreadable file aside - nothing was changed"); return; }
        AfterLoad();
        Toast(TextFormat("Started empty - your old file is kept as %s", GetFileName(aside)));
    }
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
    if (loadFailed) screen = SCR_LOADFAIL;     /* say so, and wait: nothing is saved until you choose */
    else AfterLoad();

    float saveTimer = 0;
    int   lastCursor = -1;

    while (!WindowShouldClose() && !quitNow) {
        W = GetScreenWidth();
        H = GetScreenHeight();
        clickUsed  = false;
        hoverBlocked = false;
        wantCursor = MOUSE_CURSOR_DEFAULT;
        float dt = GetFrameTime();
        GitServerPoll();
        if (toastT > 0) toastT -= dt;
        if (lockT > 0)  lockT  -= dt;
        if (armedId && (armedT -= dt) <= 0) armedId = 0;

        /* the signed-in user's theme: its colour, and see-through panels if it draws a background */
        int bgTheme = curUser < 0 ? THEME_CLASSIC
                    : screen == SCR_PROFILE && FindUserById(viewUser) ? UserTheme(viewUser)   /* their page, their theme */
                    : UserTheme(curUser);
        uiAccent = THEMES[bgTheme].accent;
        unsigned char see = bgTheme == THEME_CLASSIC ? 255 : 215;
        UI_PANEL.a = see;
        UI_HOVER.a = see;
        UI_BG.a    = bgTheme == THEME_CLASSIC ? 255 : 230;

        /* a picture dropped onto the window */
        if (IsFileDropped()) {
            FilePathList dropped = LoadDroppedFiles();
            User *me = FindUserById(curUser);
            bool inRepo = me && curRepo >= 0 && curRepo < repoCount &&
                          (screen == SCR_REPO || screen == SCR_COMMIT || screen == SCR_UPLOAD);
            if (me && screen == SCR_ACCOUNT && dropped.count > 0) {
                settingsTab = 0;
                UseAvatarPath(me, dropped.paths[0]);
            } else if (inRepo && CanEdit(&repos[curRepo])) {
                if (screen != SCR_UPLOAD) OpenUpload();
                StagePaths(dropped.paths, (int)dropped.count);
            } else if (inRepo) {
                Toast("Only the owner and collaborators can upload files to this repository");
            } else if (me) {
                Toast("To upload files, open one of your repositories and drop them there");
            }
            UnloadDroppedFiles(dropped);
        }

        /* every screen except sign-in / sign-up needs someone signed in */
        if (curUser < 0 && screen != SCR_LOGIN && screen != SCR_REGISTER && screen != SCR_FORGOT && screen != SCR_LOADFAIL) GoLogin();
        /* files waiting to be uploaded are let go once you leave the upload page */
        if (screen != SCR_UPLOAD && stagedCount) ClearStaged();
        /* the recovery code only stays in memory while its screen is open */
        if (screen != SCR_RECOVERY && shownCode[0]) memset(shownCode, 0, sizeof shownCode);

        if (IsKeyPressed(KEY_ESCAPE)) {
            bool anyFocus = ed.focused || tfSearch.active || tfName.active || tfDesc.active ||
                            tfNewFile.active || tfCommit.active || tfIssue.active || tfUser.active ||
                            tfPass.active || tfPass2.active || tfOldPass.active || tfNewPass.active ||
                            tfNewPass2.active || tfDelPass.active || tfBio.active || tfAvatar.active || tfCode.active ||
                            tfUpPath.active || tfUpMsg.active || tfInvite.active || tfEmail.active || tfBranch.active;
            if (codeMenu || branchMenu) codeMenu = branchMenu = false;
            else if (anyFocus) Unfocus();
            else if (screen == SCR_FORGOT) GoLogin();
            else if (screen == SCR_RECOVERY) GoHome();
            else if (screen == SCR_NEW || screen == SCR_REPO || screen == SCR_ACCOUNT || screen == SCR_PEOPLE) GoHome();
            else if (screen == SCR_PROFILE && viewUser != curUser) GoHome();
            else if (screen == SCR_DAY) { if (dayUser == curUser) GoHome(); else OpenProfile(dayUser); }
            else if (screen == SCR_UPLOAD) BackToCode();
            else if (screen == SCR_COMMIT && fromDay) screen = SCR_DAY;
            else if (screen == SCR_COMMIT) { screen = SCR_REPO; tab = TAB_COMMITS; }
        }

        BeginDrawing();
        ClearBackground(UI_BASE);
        if (bgTheme != THEME_CLASSIC) DrawTheme(bgTheme, (Rectangle){0, 0, (float)W, (float)H}, 0.55f);
        DrawHeader();

        switch (screen) {
        case SCR_LOADFAIL: ScreenLoadFail(); break;
        case SCR_LOGIN:    ScreenLogin();    break;
        case SCR_REGISTER: ScreenRegister(); break;
        case SCR_FORGOT:   ScreenForgot();   break;
        case SCR_RECOVERY: ScreenRecovery(); break;
        case SCR_ACCOUNT:  ScreenAccount();  break;
        case SCR_DAY:      ScreenDay();      break;
        case SCR_PROFILE:  ScreenProfile();  break;
        case SCR_PEOPLE:   ScreenPeople();   break;
        case SCR_NEW:      ScreenNew();      break;
        case SCR_REPO: {
            Repo *r = &repos[curRepo];
            if (DrawRepoHeader(r, tab)) break;
            if      (tab == TAB_CODE)    TabCode(r);
            else if (tab == TAB_COMMITS) TabCommits(r);
            else if (tab == TAB_ISSUES)  TabIssues(r);
            else                         TabPeople(r);
        } break;
        case SCR_COMMIT: {
            Repo *r = &repos[curRepo];
            if (DrawRepoHeader(r, TAB_COMMITS)) break;
            if (screen == SCR_COMMIT) ScreenCommit(r);
        } break;
        case SCR_UPLOAD: {
            Repo *r = &repos[curRepo];
            if (DrawRepoHeader(r, TAB_CODE)) break;
            if (screen == SCR_UPLOAD) ScreenUpload(r);
        } break;
        }

        if (screen == SCR_REPO || screen == SCR_COMMIT || screen == SCR_UPLOAD) { CodeMenu(&repos[curRepo], true); BranchMenu(&repos[curRepo], true); }
        else codeMenu = branchMenu = false;
        DrawTip();
        DrawToast();
        EndDrawing();

        if (wantCursor != lastCursor) { SetMouseCursor(wantCursor); lastCursor = wantCursor; }

        saveTimer += dt;
        if (dirty && saveTimer > 0.75f) { Save(); dirty = false; saveTimer = 0; }
    }

    GitServerStop();
    if (dirty) Save();
    ClearSecrets();
    for (int i = 0; i < repoCount; i++) FreeRepo(&repos[i]);
    for (int i = 0; i < userCount; i++) { FreeAvatarTex(&users[i]); free(users[i].avatar); }
    ClearStaged();
    for (int i = 0; i < PREVIEW_SLOTS; i++) if (prevs[i].state == 1) UnloadTexture(prevs[i].tex);
    CloseWindow();
    return 0;
}
