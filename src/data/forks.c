/*
 * forks.c - forks
 *
 * Copying someone else's repository with its whole history, how far a fork
 * is ahead of or behind its original, and bringing in the original's new
 * commits ("Sync fork").
 */

#include "forks.h"
#include "branches.h"
#include "git_commit.h"
#include "model.h"
#include "navigation.h"
#include "repo.h"
#include "sha1.h"
#include "sha_map.h"
#include "snapshot.h"
#include "ui_core.h"
#include "users.h"
#include "util.h"

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
/* The index of the repository owner has called name (ignoring capitals), or -1. */
int FindRepoByOwner(int owner, const char *name)
{
    for (int i = 0; i < repoCount; i++)
        if (repos[i].owner == owner && EqualsIgnoreCase(repos[i].name, name)) return i;
    return -1;
}

static bool IsForkOf(Repo *fork, Repo *original)
{
    return fork->isFork && fork->forkOwner == original->owner && EqualsIgnoreCase(fork->forkName, original->name);
}

/* The original a fork came from (its index), or -1 if it isn't a fork or the original is gone. */
int UpstreamIndex(Repo *repo) { return repo->isFork ? FindRepoByOwner(repo->forkOwner, repo->forkName) : -1; }

/* How many repositories are forks of this one. */
int ForkCount(Repo *repo)
{
    int count = 0;
    for (int i = 0; i < repoCount; i++) if (IsForkOf(&repos[i], repo)) count++;
    return count;
}

/* userId's fork of repos[originalIndex], or -1 */
int FindForkOf(int userId, int originalIndex)
{
    for (int i = 0; i < repoCount; i++)
        if (repos[i].owner == userId && IsForkOf(&repos[i], &repos[originalIndex])) return i;
    return -1;
}

/* How a fork compares with its original: whether it still exists and shares
   history, and how many commits each has that the other doesn't.
   Commits are matched by their git fingerprint, so it doesn't matter how
   they got here - Sync fork, or pushed with git. Worked out again only
   when either history changes. */
ForkState GetForkState(Repo *repo)
{
    static struct { const Repo *fork, *original; unsigned forkGeneration, originalGeneration; int forkCommitCount, originalCommitCount; ForkState state; } memo[8];
    ForkState state = {-1, false, -1, 0, 0};
    state.upstream = UpstreamIndex(repo);
    if (state.upstream < 0) return state;
    Repo *original = &repos[state.upstream];
    int slot = (int)(repo - repos) % 8;
    if (memo[slot].fork == repo && memo[slot].original == original && memo[slot].forkGeneration == repo->generation && memo[slot].originalGeneration == original->generation &&
        memo[slot].forkCommitCount == repo->commitCount && memo[slot].originalCommitCount == original->commitCount && memo[slot].state.upstream == state.upstream) return memo[slot].state;
    /* compares the two default branches: what's in one's history and not the other's */
    uint8_t *inOriginal = calloc((size_t)original->commitCount + 1, 1), *inFork = calloc((size_t)repo->commitCount + 1, 1);
    if (inOriginal && inFork) {
        MarkHistory(original, DefaultTip(original), inOriginal, 1);
        MarkHistory(repo, DefaultTip(repo), inFork, 1);
        for (int k = 0; k < original->commitCount; k++) {
            if (!inOriginal[k]) continue;
            int indexThere = CommitBySha(repo, original->commits[k].sha);
            if (indexThere >= 0 && inFork[indexThere]) state.baseCommit = k;
            else state.behind++;
        }
        for (int k = 0; k < repo->commitCount; k++) {
            if (!inFork[k]) continue;
            int indexThere = CommitBySha(original, repo->commits[k].sha);
            if (indexThere < 0 || !inOriginal[indexThere]) state.ahead++;
        }
    }
    free(inOriginal); free(inFork);
    state.related = state.baseCommit >= 0 || !repo->forkBase[0] || !original->commitCount || !repo->commitCount;
    memo[slot].fork = repo; memo[slot].original = original; memo[slot].forkGeneration = repo->generation; memo[slot].originalGeneration = original->generation;
    memo[slot].forkCommitCount = repo->commitCount; memo[slot].originalCommitCount = original->commitCount; memo[slot].state = state;
    return state;
}

/* Copies commit commitIndex of src onto the end of dst - the very same git commit,
   so it keeps its fingerprint. Blob numbers differ between repositories,
   so each file's contents are stored again in dst (StoreBlob reuses a blob
   when dst already has it). The commits it was built on must already be in dst. */
static bool CopyCommit(Repo *dst, Repo *src, int commitIndex)
{
    Commit *source = &src->commits[commitIndex];
    if (CommitBySha(dst, source->sha) >= 0) return true;          /* already there */
    Commit copy = *source;
    copy.langLines = NULL;                           /* worked out again in the new repository */
    int fileCount = source->fileCount;
    copy.names = malloc((size_t)(fileCount ? fileCount : 1) * sizeof *copy.names);
    copy.blobs = malloc((size_t)(fileCount ? fileCount : 1) * sizeof *copy.blobs);
    copy.modes = malloc((size_t)(fileCount ? fileCount : 1));
    copy.raw   = malloc((size_t)source->rawLen + 1);
    if (!copy.names || !copy.blobs || !copy.modes || !copy.raw) { FreeCommit(&copy); return false; }
    memcpy(copy.names, source->names, (size_t)fileCount * sizeof *copy.names);
    memcpy(copy.modes, source->modes, (size_t)fileCount);
    memcpy(copy.raw, source->raw, (size_t)source->rawLen + 1);
    for (int j = 0; j < fileCount; j++) {
        Blob *blob = &src->blobs[source->blobs[j]];
        copy.blobs[j] = StoreBlobWithSha(dst, blob->data, blob->length, blob->sha);
        if (copy.blobs[j] < 0) { FreeCommit(&copy); return false; }
    }
    for (int parentNo = 0; parentNo < source->parentCount; parentNo++) {
        copy.parents[parentNo] = CommitBySha(dst, src->commits[source->parents[parentNo]].sha);
        if (copy.parents[parentNo] < 0) { FreeCommit(&copy); return false; }
    }
    copy.copied = true;
    if (!AppendCommit(dst, &copy)) { FreeCommit(&copy); return false; }
    return true;
}

/* Copies every commit of src that dst doesn't have, oldest first: all of
   them, or only the history of commit "tip" (if it isn't -1).
   If it fails part way, dst goes back to how it was. */
static bool CopyMissing(Repo *dst, Repo *src, int tip)
{
    int countBefore = dst->commitCount;
    uint8_t *wanted = NULL;
    if (tip >= 0) { wanted = calloc((size_t)src->commitCount + 1, 1); if (!wanted) return false; MarkHistory(src, tip, wanted, 1); }
    for (int k = 0; k < src->commitCount; k++)
        if ((!wanted || wanted[k]) && !CopyCommit(dst, src, k)) {
            free(wanted);
            while (dst->commitCount > countBefore) FreeCommit(&dst->commits[--dst->commitCount]);
            ShaMapFree(&dst->commitMap);
            for (int j = 0; j < dst->commitCount; j++) ShaMapPut(&dst->commitMap, dst->commits[j].sha, j);
            dst->generation++;
            return false;
        }
    free(wanted);
    return true;
}

/* Makes the signed-in user's fork of a repository and opens it (or opens the fork they already
   have). */
void TryForkRepo(int originalIndex)
{
    Repo *original = &repos[originalIndex];
    if (!CanSee(original, currentUserId)) { Toast("That repository is private"); return; }
    int existingFork = FindForkOf(currentUserId, originalIndex);
    if (existingFork >= 0) { OpenRepo(existingFork); Toast("You already have a fork of this repository - here it is"); return; }
    if (repoCount >= MAX_REPOS) { Toast("This computer has reached the maximum number of repositories"); return; }

    /* same name if it's free, otherwise name-fork, name-fork-2, ... */
    char name[NAME_LEN];
    snprintf(name, NAME_LEN, "%s", original->name);
    for (int attempt = 1; FindRepoByOwner(currentUserId, name) >= 0; attempt++)
        snprintf(name, NAME_LEN, attempt == 1 ? "%.50s-fork" : "%.50s-fork-%d", original->name, attempt);

    Repo *repo = NewRepo(currentUserId, name, original->description);  /* original is still valid: repos[] never moves */
    repo->isFork    = true;
    repo->isPrivate = original->isPrivate;         /* a copy of private work stays private */
    repo->forkOwner = original->owner;
    snprintf(repo->forkName, NAME_LEN, "%s", original->name);
    snprintf(repo->initialBranch, NAME_LEN, "%s", original->initialBranch);
    if (original->branchCount) ShaToHex(original->commits[DefaultTip(original)].sha, repo->forkBase); else repo->forkBase[0] = 0;
    if (!CopyMissing(repo, original, -1)) {
        DeleteRepo(repoCount - 1);
        Toast("Couldn't copy that repository - it's too big");
        return;
    }
    for (int branch = 0; branch < original->branchCount; branch++)  /* every branch comes too */
        AddBranch(repo, original->branches[branch].name, CommitBySha(repo, original->commits[original->branches[branch].tip].sha));
    repo->defaultBranch = repo->currentBranch = original->defaultBranch;
    MarkDirty();
    OpenRepo(repoCount - 1);
    Toast(TextFormat("Forked %s / %s - this copy is yours to change", UserName(original->owner), original->name));
}

/* A file's contents and mode in a commit (commit may be NULL). */
FileVersion FileVersionIn(Repo *repo, Commit *commit, const char *name)
{
    FileVersion version = {false, NULL, 0, 0};
    int index = commit ? FindInCommit(commit, name) : -1;
    if (index >= 0) { version.exists = true; version.data = repo->blobs[commit->blobs[index]].data; version.length = repo->blobs[commit->blobs[index]].length; version.mode = commit->modes[index]; }
    return version;
}
/* Are two versions of a file the same (both missing counts as the same)? */
bool SameVersion(FileVersion a, FileVersion b)
{
    if (a.exists != b.exists) return false;
    return !a.exists || (a.length == b.length && a.mode == b.mode && memcmp(a.data, b.data, (size_t)a.length) == 0);
}

/* Brings the original's new commits into the fork repo.
   Returns NULL on success (and a message for the user in *doneMessage), or an error. */
const char *SyncFork(Repo *repo, char *doneMessage, int doneCapacity)
{
    ForkState state = GetForkState(repo);
    if (state.upstream < 0)     return "The original repository has been deleted";
    if (!CanSee(&repos[state.upstream], currentUserId)) return "The original repository is private now - you can't sync from it";
    if (!state.related)   return "The original repository's history no longer matches this fork";
    if (!state.behind)    return "Already up to date";
    Repo *original = &repos[state.upstream];
    char originalName[96];
    snprintf(originalName, sizeof originalName, "%.40s / %.40s", UserName(original->owner), original->name);
    int behind = state.behind;

    /* 1) fast-forward: you haven't committed anything of your own, so just copy their new commits */
    int theirTip = DefaultTip(original);
    if (state.ahead == 0) {
        if (repo->commitCount + behind > MAX_COMMITS) return "This fork has reached its commit limit";
        if (!CopyMissing(repo, original, theirTip)) return "This fork is out of storage space";
        int newTip = CommitBySha(repo, original->commits[theirTip].sha);
        if (repo->branchCount) repo->branches[repo->defaultBranch].tip = newTip;
        else { AddBranch(repo, DefaultBranchName(original), newTip); repo->defaultBranch = repo->currentBranch = 0; }
        repo->generation++;
        ShaToHex(original->commits[theirTip].sha, repo->forkBase);
        snprintf(doneMessage, (size_t)doneCapacity, "Synced: copied %d commit%s from %s", behind, behind == 1 ? "" : "s", originalName);
        return NULL;
    }

    /* 2) three-way merge. For every file, compare
            base   = the original's newest commit that this fork also has
            theirs = the original now
            ours   = this fork now
          If only one side changed the file, take that side. If both made the
          same change, fine. If both changed it differently, that's a conflict. */
    Commit *baseCommit = state.baseCommit >= 0 ? &original->commits[state.baseCommit] : NULL, *theirCommit = &original->commits[theirTip], *ourCommit = &repo->commits[DefaultTip(repo)];
    static const char *allNames[MAX_FILES * 3];
    static char pick[MAX_FILES * 3];      /* 'o' ours, 't' theirs */
    int nameCount = 0;
    Commit *sides[3] = {ourCommit, theirCommit, baseCommit};
    Snapshot allPaths = {0};
    for (int side = 0; side < 3; side++)                           /* every path on any side, once */
        for (int j = 0; sides[side] && j < sides[side]->fileCount; j++)
            if (SnapshotFind(&allPaths, sides[side]->names[j]) < 0 && !SnapshotSet(&allPaths, sides[side]->names[j], 0, 0)) { SnapshotFree(&allPaths); return "Out of memory"; }
    for (int q = 0; q < allPaths.count; q++) allNames[nameCount++] = allPaths.names[q];
    SnapshotFree(&allPaths);

    /* first pass: decide every file, and stop before changing anything if there's a conflict */
    int  conflicts = 0, keptCount = 0;
    const char *firstConflict = NULL;
    bool takesTheirChanges = false;
    for (int q = 0; q < nameCount; q++) {
        FileVersion baseVersion = FileVersionIn(original, baseCommit, allNames[q]), theirVersion = FileVersionIn(original, theirCommit, allNames[q]), ourVersion = FileVersionIn(repo, ourCommit, allNames[q]);
        if      (SameVersion(ourVersion, theirVersion) || SameVersion(baseVersion, theirVersion)) pick[q] = 'o';  /* nothing new from them   */
        else if (SameVersion(baseVersion, ourVersion))                      pick[q] = 't';  /* only they changed it    */
        else { pick[q] = '!'; if (!conflicts++) firstConflict = allNames[q]; continue; }
        if (pick[q] == 't') takesTheirChanges = true;
        if ((pick[q] == 'o' ? ourVersion : theirVersion).exists) keptCount++;
    }
    if (conflicts)
        return TextFormat("Can't merge: you and %s both changed %s%s", UserName(original->owner), firstConflict,
                          conflicts > 1 ? TextFormat(" (and %d more file%s)", conflicts - 1, conflicts == 2 ? "" : "s") : "");
    if (keptCount > MAX_FILES) return TextFormat("The merged repository would have more than %d files", MAX_FILES);
    if (repo->commitCount + behind + 1 > MAX_COMMITS) return "This fork has reached its commit limit";

    /* bring their commits in too (a merge commit points at both sides, so git needs both) */
    int ourHead = DefaultTip(repo);
    if (!CopyMissing(repo, original, theirTip)) return "This fork is out of storage space";
    int theirHead = CommitBySha(repo, theirCommit->sha);
    ourCommit = &repo->commits[ourHead];                               /* the list may have moved */

    /* even if nothing of theirs changes your files, the merge commit records that you have their history */
    Snapshot merged;
    memset(&merged, 0, sizeof merged);
    bool ok = SnapshotReserve(&merged, keptCount > 0 ? keptCount : 1);
    for (int q = 0; q < nameCount && ok; q++) {
        FileVersion version = pick[q] == 'o' ? FileVersionIn(repo, ourCommit, allNames[q]) : FileVersionIn(original, theirCommit, allNames[q]);
        if (!version.exists) continue;                                  /* deleted on the winning side */
        int blob = StoreBlob(repo, version.data, version.length);
        ok = blob >= 0 && SnapshotSet(&merged, allNames[q], blob, version.mode);
    }
    if (!ok) { SnapshotFree(&merged); return "This fork is out of storage space"; }
    /* the merge sits on top of your latest commit (git's "first parent") and theirs.
       FinishCommit builds on the last commit in the list - theirs now - so it's put together here */
    Commit commit;
    memset(&commit, 0, sizeof commit);
    commit.fileCount = merged.count; commit.names = merged.names; commit.blobs = merged.blobs; commit.modes = merged.modes;
    commit.author = currentUserId;
    commit.time = (long long)time(NULL);
    snprintf(commit.message, TEXT_LEN, "Merge %d commit%s from %s", behind, behind == 1 ? "" : "s", originalName);
    commit.parents[0] = ourHead; commit.parents[1] = theirHead; commit.parentCount = 2;
    const uint8_t *parentShas[2] = {repo->commits[ourHead].sha, repo->commits[theirHead].sha};
    uint8_t treeSha[20];
    if (CommitTree(repo, &commit, treeSha, NULL, NULL))
        commit.raw = MakeRawCommit(treeSha, parentShas, 2, UserName(currentUserId), UserEmail(currentUserId), commit.time, commit.message, &commit.rawLen);
    if (!commit.raw || !AppendCommit(repo, &commit)) { FreeCommit(&commit); return "Couldn't save the merge commit"; }
    repo->branches[repo->defaultBranch].tip = repo->commitCount - 1;
    ShaToHex(theirCommit->sha, repo->forkBase);
    if (!takesTheirChanges) snprintf(doneMessage, (size_t)doneCapacity, "Up to date: you already had everything from %s", originalName);
    else snprintf(doneMessage, (size_t)doneCapacity, "Merged %d commit%s from %s into a new commit", behind, behind == 1 ? "" : "s", originalName);
    return NULL;
}
