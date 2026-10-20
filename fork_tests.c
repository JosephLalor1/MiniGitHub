#define main app_main
#include "main.c"
#undef main
#include <assert.h>
static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } else printf("ok   %s\n", #c); } while (0)

static int MakeUser(const char *n) { User *u = &users[userCount++]; memset(u,0,sizeof *u); u->id = nextUserId++; snprintf(u->name, NAME_LEN, "%s", n); return u->id; }
static void Commit1(Repo *r, int uid, const char *file, const char *text, const char *msg)
{ curUser = uid; DraftSetText(r, uid, file, text); const char *e = DoCommit(r, msg); if (e) printf("commit err: %s\n", e); }
static const char *FileAtHead(Repo *r, const char *name) { int b = HeadBlobOf(r, name); return b >= 0 ? r->blobs[b].data : "(none)"; }

int main(void)
{
    int alice = MakeUser("alice"), bob = MakeUser("bob");
    curUser = alice;
    Repo *a = NewRepo(alice, "proj", "demo");
    Commit1(a, alice, "a.txt", "one\n", "first");
    Commit1(a, alice, "b.txt", "bee\n", "second");

    /* fork */
    curUser = bob; curRepo = 0;
    TryForkRepo(0);
    CHECK(repoCount == 2);
    Repo *f = &repos[1];
    a = &repos[0];
    CHECK(f->owner == bob && f->isFork && strcmp(f->name, "proj") == 0);
    CHECK(f->commitCount == 2 && strcmp(f->commits[1].id, a->commits[1].id) == 0);
    CHECK(f->commits[0].copied && f->commits[1].copied);
    ForkState s = GetForkState(f);
    CHECK(s.up == 0 && s.related && s.ahead == 0 && s.behind == 0);
    CHECK(ForkCount(a) == 1);

    /* fork again -> the same one */
    TryForkRepo(0);
    CHECK(repoCount == 2);

    /* fast-forward */
    Commit1(a, alice, "a.txt", "one\ntwo\n", "alice adds two");
    s = GetForkState(f);
    CHECK(s.behind == 1 && s.ahead == 0);
    char done[TEXT_LEN]; curUser = bob;
    const char *err = SyncFork(f, done, sizeof done);
    CHECK(err == NULL); printf("     -> %s\n", done);
    CHECK(f->commitCount == 3 && strcmp(Head(f)->id, Head(a)->id) == 0);
    CHECK(strcmp(FileAtHead(f, "a.txt"), "one\ntwo\n") == 0);
    s = GetForkState(f); CHECK(s.behind == 0 && s.ahead == 0);

    /* diverged, different files -> merge commit */
    Commit1(f, bob, "b.txt", "bee\nbob was here\n", "bob edits b");
    Commit1(a, alice, "c.txt", "new file\n", "alice adds c");
    Commit1(a, alice, "a.txt", "one\ntwo\nthree\n", "alice adds three");
    s = GetForkState(f); CHECK(s.ahead == 1 && s.behind == 2);
    curUser = bob;
    err = SyncFork(f, done, sizeof done);
    CHECK(err == NULL); if (!err) printf("     -> %s\n", done);
    CHECK(strcmp(FileAtHead(f, "a.txt"), "one\ntwo\nthree\n") == 0);
    CHECK(strcmp(FileAtHead(f, "b.txt"), "bee\nbob was here\n") == 0);
    CHECK(strcmp(FileAtHead(f, "c.txt"), "new file\n") == 0);
    CHECK(Head(f)->author == bob && !Head(f)->copied);
    printf("     merge message: %s\n", Head(f)->message);
    s = GetForkState(f); CHECK(s.behind == 0 && s.ahead == 2);

    /* deletion on their side is merged too */
    curUser = alice; { WorkFile *e = DraftEdit(a, alice, "c.txt"); e->deleted = true; DoCommit(a, "remove c"); }
    curUser = bob; err = SyncFork(f, done, sizeof done);
    CHECK(err == NULL && strcmp(FileAtHead(f, "c.txt"), "(none)") == 0);

    /* conflict: both change a.txt differently -> refused, nothing changes */
    Commit1(f, bob, "a.txt", "bob's a\n", "bob rewrites a");
    Commit1(a, alice, "a.txt", "alice's a\n", "alice rewrites a");
    int before = f->commitCount; char baseBefore[8]; strcpy(baseBefore, f->forkBase);
    curUser = bob; err = SyncFork(f, done, sizeof done);
    CHECK(err != NULL); if (err) printf("     -> %s\n", err);
    CHECK(f->commitCount == before && strcmp(f->forkBase, baseBefore) == 0);
    CHECK(strcmp(FileAtHead(f, "a.txt"), "bob's a\n") == 0);

    /* name clash: carol owns a "proj" already -> fork is called proj-fork */
    int carol = MakeUser("carol"); curUser = carol;
    Repo *own = NewRepo(carol, "proj", ""); (void)own;
    TryForkRepo(0);
    CHECK(strcmp(repos[repoCount - 1].name, "proj-fork") == 0 && repos[repoCount - 1].isFork);

    /* save + load keeps everything */
    char idsBefore[64]; snprintf(idsBefore, sizeof idsBefore, "%s %s %d", repos[1].forkBase, Head(&repos[1])->id, repos[1].commitCount);
    for (int i = 0; i < userCount; i++) { strcpy(users[i].salt, "00000000000000000000000000000000"); memset(users[i].hash, 'a', 64); users[i].hash[64] = 0; }
    Save();
    for (int i = 0; i < repoCount; i++) FreeRepo(&repos[i]);
    memset(repos, 0, sizeof repos); memset(users, 0, sizeof users); repoCount = userCount = 0;
    CHECK(Load());
    char idsAfter[64]; snprintf(idsAfter, sizeof idsAfter, "%s %s %d", repos[1].forkBase, Head(&repos[1])->id, repos[1].commitCount);
    CHECK(strcmp(idsBefore, idsAfter) == 0);
    CHECK(repos[1].isFork && repos[1].forkOwner == alice && strcmp(repos[1].forkName, "proj") == 0);
    CHECK(repos[1].commits[0].copied && !Head(&repos[1])->copied);
    s = GetForkState(&repos[1]); printf("     ahead=%d behind=%d\n", s.ahead, s.behind); CHECK(s.ahead == 4 && s.behind == 1);

    /* original deleted */
    DeleteRepo(0);
    s = GetForkState(&repos[0]); CHECK(s.up == -1);
    curUser = bob; err = SyncFork(&repos[0], done, sizeof done); CHECK(err != NULL); if (err) printf("     -> %s\n", err);

    printf(fails ? "\n%d FAILED\n" : "\nALL PASSED\n", fails);
    return fails != 0;
}
