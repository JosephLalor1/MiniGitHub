/*
 * branches.c - branches
 *
 * Walking a commit's history, finding where two lines of work split,
 * counting commits ahead, and creating and deleting branches.
 */

#include "branches.h"
#include "drafts.h"
#include "model.h"
#include "repo.h"
#include "sha_map.h"

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
void MarkHistory(Repo *repo, int tip, uint8_t *mark, uint8_t bit)
{
    if (tip < 0) return;
    int *stack = malloc(((size_t)repo->commitCount + 1) * sizeof(int)), stackSize = 0;
    if (!stack) return;
    stack[stackSize++] = tip;
    while (stackSize) {
        int commitIndex = stack[--stackSize];
        if (mark[commitIndex] & bit) continue;
        mark[commitIndex] |= bit;
        for (int parentNo = 0; parentNo < repo->commits[commitIndex].parentCount; parentNo++) {
            int parent = repo->commits[commitIndex].parents[parentNo];
            if (!(mark[parent] & bit) && stackSize < repo->commitCount) stack[stackSize++] = parent;
        }
    }
    free(stack);
}
/* Is commit ancestor part of descendant's history (or descendant itself)? */
bool IsAncestor(Repo *repo, int ancestor, int descendant)
{
    if (ancestor < 0 || descendant < 0) return false;
    if (ancestor == descendant) return true;
    if (ancestor > descendant) return false;         /* parents always come earlier in the list */
    uint8_t *marks = calloc((size_t)repo->commitCount, 1);
    if (!marks) return false;
    MarkHistory(repo, descendant, marks, 1);
    bool found = marks[ancestor] != 0;
    free(marks);
    return found;
}
/* The newest commit both a and b are built on: where the two lines of work split. */
int MergeBase(Repo *repo, int a, int b)
{
    uint8_t *marks = calloc((size_t)repo->commitCount + 1, 1);
    if (!marks) return -1;
    MarkHistory(repo, a, marks, 1);
    MarkHistory(repo, b, marks, 2);
    int best = -1;
    for (int k = repo->commitCount - 1; k >= 0 && best < 0; k--) if (marks[k] == 3) best = k;  /* later in the list = newer */
    free(marks);
    return best;
}
/* How many commits a has that b doesn't (b may be -1). */
int CountAhead(Repo *repo, int a, int b)
{
    uint8_t *marks = calloc((size_t)repo->commitCount + 1, 1);
    if (!marks) return 0;
    MarkHistory(repo, a, marks, 1);
    MarkHistory(repo, b, marks, 2);
    int count = 0;
    for (int k = 0; k < repo->commitCount; k++) if (marks[k] == 1) count++;
    free(marks);
    return count;
}

/* After a branch is deleted, the commits no branch leads to any more are let go
   (the list is closed up and every reference to a position is updated). */
static void PruneUnreachable(Repo *repo)
{
    int count = repo->commitCount;
    uint8_t *reachable = calloc((size_t)count + 1, 1);
    int *newIndex = malloc(((size_t)count + 1) * sizeof(int));
    if (!reachable || !newIndex) { free(reachable); free(newIndex); return; }
    for (int branch = 0; branch < repo->branchCount; branch++) MarkHistory(repo, repo->branches[branch].tip, reachable, 1);
    int kept = 0;
    for (int k = 0; k < count; k++) {
        if (!reachable[k]) { FreeCommit(&repo->commits[k]); newIndex[k] = -1; continue; }
        newIndex[k] = kept;
        repo->commits[kept] = repo->commits[k];
        for (int parentNo = 0; parentNo < repo->commits[kept].parentCount; parentNo++) repo->commits[kept].parents[parentNo] = newIndex[repo->commits[kept].parents[parentNo]];
        kept++;
    }
    if (kept != count) {
        repo->commitCount = kept;
        for (int branch = 0; branch < repo->branchCount; branch++) repo->branches[branch].tip = newIndex[repo->branches[branch].tip];
        ShaMapFree(&repo->commitMap);
        for (int k = 0; k < kept; k++) ShaMapPut(&repo->commitMap, repo->commits[k].sha, k);
        repo->generation++;
    }
    free(reachable); free(newIndex);
}

/* Git's rules for a branch name, kept simple: letters, digits and - _ . /
   (so "feature/login" works), no "..", no "//", not starting or ending with / or . */
const char *BranchNameProblem(const char *name)
{
    size_t length = strlen(name);
    if (!length) return "Type a name for the branch";
    if (length >= NAME_LEN) return "That name is too long";
    for (size_t i = 0; i < length; i++) {
        char ch = name[i];
        if (!(isalnum((unsigned char)ch) || ch == '-' || ch == '_' || ch == '.' || ch == '/')) return "Branch names can use letters, numbers and - _ . /";
    }
    if (name[0] == '/' || name[0] == '.' || name[0] == '-' || name[length - 1] == '/' || name[length - 1] == '.' ||
        strstr(name, "..") || strstr(name, "//") || strstr(name, "/.") || (length >= 5 && strcmp(name + length - 5, ".lock") == 0) || strcmp(name, "HEAD") == 0)
        return "That isn't a name git allows for a branch";
    return NULL;
}

/* A new branch starting at the commit you're on; you move onto it. */
const char *CreateBranch(Repo *repo, const char *name)
{
    const char *problem = BranchNameProblem(name);
    if (problem) return problem;
    if (!repo->branchCount) return "Make the first commit before making branches";
    if (FindBranch(repo, name) >= 0) return TextFormat("There's already a branch called %s", name);
    for (int branchIndex = 0; branchIndex < repo->branchCount; branchIndex++) {             /* git can't have "a" and "a/b" both */
        size_t existingLength = strlen(repo->branches[branchIndex].name), nameLength = strlen(name);
        if ((existingLength < nameLength && strncmp(name, repo->branches[branchIndex].name, existingLength) == 0 && name[existingLength] == '/') ||
            (nameLength < existingLength && strncmp(repo->branches[branchIndex].name, name, nameLength) == 0 && repo->branches[branchIndex].name[nameLength] == '/'))
            return TextFormat("That name clashes with the branch %s", repo->branches[branchIndex].name);
    }
    int branchIndex = AddBranch(repo, name, HeadIndex(repo));
    if (branchIndex < 0) return TextFormat("A repository can have up to %d branches", MAX_BRANCHES);
    repo->currentBranch = branchIndex;
    repo->generation++;
    return NULL;
}

/* Deletes a branch (never the default one). Its commits stay if another branch leads to them. */
const char *DeleteBranch(Repo *repo, int branchIndex)
{
    if (branchIndex < 0 || branchIndex >= repo->branchCount) return "No such branch";
    if (branchIndex == repo->defaultBranch) return "The default branch can't be deleted";
    RemoveDrafts(repo, -1, repo->branches[branchIndex].name);
    memmove(&repo->branches[branchIndex], &repo->branches[branchIndex + 1], (size_t)(repo->branchCount - branchIndex - 1) * sizeof(Branch));
    repo->branchCount--;
    if (repo->defaultBranch > branchIndex) repo->defaultBranch--;
    if (repo->currentBranch == branchIndex) repo->currentBranch = repo->defaultBranch;
    else if (repo->currentBranch > branchIndex) repo->currentBranch--;
    PruneUnreachable(repo);
    repo->generation++;
    return NULL;
}
