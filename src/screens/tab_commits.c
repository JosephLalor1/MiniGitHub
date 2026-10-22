/*
 * tab_commits.c - the Commits tab
 *
 * The history as a list, or as a graph of branches and merges.
 */

#include "tab_commits.h"
#include "app_state.h"
#include "avatars.h"
#include "branches.h"
#include "contributions.h"
#include "git_commit.h"
#include "languages.h"
#include "model.h"
#include "navigation.h"
#include "repo.h"
#include "repo_header.h"
#include "themes.h"
#include "ui_core.h"
#include "users.h"
#include "util.h"
#include "widgets.h"

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
typedef struct { int lane, firstLane, laneCount; } GraphRow;  /* the dot's lane; the lanes below it: [at, at + n) */
static struct {
    const Repo *repo; unsigned generation; int commitCount;
    GraphRow *rows; int rowCapacity;
    int *waitingFor, *fromLane; int capacity, usedLanes;  /* per lane below a row: the commit it waits for (-1 = none), and the lane its line starts from */
    int maxLanes;
    int *order, orderCapacity;                    /* which commit is on each row */
} graph;

/* The order of the rows: newest first, but a commit always comes after
   (below) every commit built on it - the same order as git log --date-order.
   Each step takes the newest commit whose children are all placed already. */
static long long *committerTimes;                     /* when each commit was committed (git's "committer" time) */
static bool IsOlderInHeap(Repo *repo, int a, int b)
{
    (void)repo;
    long long timeA = committerTimes[a], timeB = committerTimes[b];
    return timeA != timeB ? timeA < timeB : a < b;
}
/* The committer time: when the commit was actually made (the author time is
   when the change was first written - they differ for re-applied work). */
static long long CommitterTime(Commit *commit)
{
    const char *line = commit->raw ? strstr(commit->raw, "\ncommitter ") : NULL;
    const char *emailEnd = line ? strchr(line, '>') : NULL;
    const char *lineEnd = line ? strchr(line + 1, '\n') : NULL;
    if (!emailEnd || (lineEnd && emailEnd > lineEnd)) return commit->time;
    return strtoll(emailEnd + 1, NULL, 10);
}
static void GraphOrder(Repo *repo)
{
    int count = repo->commitCount;
    graph.order = GrowArray(graph.order, &graph.orderCapacity, count + 1, sizeof(int));
    int *childrenLeft = calloc((size_t)count + 1, sizeof(int)), *heap = malloc(((size_t)count + 1) * sizeof(int));
    committerTimes = malloc(((size_t)count + 1) * sizeof(long long));
    if (!childrenLeft || !heap || !committerTimes || graph.orderCapacity < count) {  /* out of memory: plain newest-first */
        for (int i = 0; i < count && graph.orderCapacity >= count; i++) graph.order[i] = count - 1 - i;
        free(childrenLeft); free(heap); free(committerTimes); committerTimes = NULL; return;
    }
    for (int k = 0; k < count; k++) committerTimes[k] = CommitterTime(&repo->commits[k]);
    for (int k = 0; k < count; k++)
        for (int parentNo = 0; parentNo < repo->commits[k].parentCount; parentNo++) childrenLeft[repo->commits[k].parents[parentNo]]++;
    int heapSize = 0, placed = 0;
    #define HEAP_PUSH(item) do { int i_ = heapSize++; heap[i_] = (item); \
        while (i_ > 0 && IsOlderInHeap(repo, heap[(i_ - 1) / 2], heap[i_])) { int t_ = heap[i_]; heap[i_] = heap[(i_ - 1) / 2]; heap[(i_ - 1) / 2] = t_; i_ = (i_ - 1) / 2; } } while (0)
    for (int k = 0; k < count; k++) if (!childrenLeft[k]) HEAP_PUSH(k);
    while (heapSize) {
        int newest = heap[0];
        heap[0] = heap[--heapSize];
        for (int i = 0;;) {                                  /* sift down */
            int leftChild = 2 * i + 1, rightChild = leftChild + 1, older = i;
            if (leftChild < heapSize && IsOlderInHeap(repo, heap[older], heap[leftChild])) older = leftChild;
            if (rightChild < heapSize && IsOlderInHeap(repo, heap[older], heap[rightChild])) older = rightChild;
            if (older == i) break;
            int swap = heap[i]; heap[i] = heap[older]; heap[older] = swap; i = older;
        }
        graph.order[placed++] = newest;
        Commit *commit = &repo->commits[newest];
        for (int parentNo = 0; parentNo < commit->parentCount; parentNo++) if (--childrenLeft[commit->parents[parentNo]] == 0) HEAP_PUSH(commit->parents[parentNo]);
    }
    #undef HEAP_PUSH
    for (int k = 0; placed < count && k < count; k++) if (childrenLeft[k] > 0) graph.order[placed++] = k;  /* can't happen, but never leave a gap */
    free(childrenLeft); free(heap); free(committerTimes); committerTimes = NULL;
}
static int commitsTabView = 1;                    /* 0 = list, 1 = graph */

static void BuildGraph(Repo *repo)
{
    if (graph.repo == repo && graph.generation == repo->generation && graph.commitCount == repo->commitCount) return;
    int count = repo->commitCount;
    graph.repo = repo; graph.generation = repo->generation; graph.commitCount = count; graph.usedLanes = 0; graph.maxLanes = 0;
    graph.rows = GrowArray(graph.rows, &graph.rowCapacity, count + 1, sizeof(GraphRow));
    GraphOrder(repo);
    if (graph.orderCapacity < count) { graph.commitCount = -1; return; }
    int *lanes = NULL, laneCount = 0, laneCapacity = 0, *laneFrom = NULL, laneFromCapacity = 0;
    for (int i = 0; i < count && graph.rowCapacity > count; i++) {
        int commitIndex = graph.order[i];          /* row 0 is the newest commit */
        Commit *commit = &repo->commits[commitIndex];
        int lane = -1;
        for (int k = 0; k < laneCount; k++)
            if (lanes[k] == commitIndex) { if (lane < 0) lane = k; else lanes[k] = -1; }  /* lines joining here end here */
        if (lane < 0) {                            /* nothing above leads here: the newest commit of a line */
            for (int k = 0; k < laneCount && lane < 0; k++) if (lanes[k] < 0) lane = k;
            if (lane < 0) { lanes = GrowArray(lanes, &laneCapacity, laneCount + 1, sizeof(int)); lane = laneCount++; }
        }
        laneFrom = GrowArray(laneFrom, &laneFromCapacity, laneCount + 2, sizeof(int));
        for (int k = 0; k < laneCount; k++) laneFrom[k] = k;
        lanes[lane] = commit->parentCount > 0 ? commit->parents[0] : -1;
        laneFrom[lane] = lane;
        if (commit->parentCount > 1) {             /* a merge: its other parent gets a lane, curving out of this dot */
            int newLane = -1;
            for (int k = 0; k < laneCount && newLane < 0; k++) if (lanes[k] < 0) newLane = k;
            if (newLane < 0) { lanes = GrowArray(lanes, &laneCapacity, laneCount + 1, sizeof(int)); laneFrom = GrowArray(laneFrom, &laneFromCapacity, laneCount + 2, sizeof(int)); newLane = laneCount++; }
            lanes[newLane] = commit->parents[1];
            laneFrom[newLane] = lane;
        }
        while (laneCount > 0 && lanes[laneCount - 1] < 0) laneCount--;  /* drop empty lanes on the right */
        graph.rows[i] = (GraphRow){lane, graph.usedLanes, laneCount};
        if (lane + 1 > graph.maxLanes) graph.maxLanes = lane + 1;
        if (laneCount > graph.maxLanes) graph.maxLanes = laneCount;
        if (graph.usedLanes + laneCount > graph.capacity) {
            int newCapacity = graph.capacity ? graph.capacity : 1024;
            while (newCapacity < graph.usedLanes + laneCount) newCapacity *= 2;
            int *grownWait = realloc(graph.waitingFor, (size_t)newCapacity * sizeof(int)), *grownFrom = grownWait ? realloc(graph.fromLane, (size_t)newCapacity * sizeof(int)) : NULL;
            if (grownWait) graph.waitingFor = grownWait;
            if (grownFrom) graph.fromLane = grownFrom;
            if (!grownWait || !grownFrom) { graph.rows[i].laneCount = 0; continue; }
            graph.capacity = newCapacity;
        }
        memcpy(graph.waitingFor + graph.usedLanes, lanes, (size_t)laneCount * sizeof(int));
        memcpy(graph.fromLane + graph.usedLanes, laneFrom, (size_t)laneCount * sizeof(int));
        graph.usedLanes += laneCount;
    }
    free(lanes); free(laneFrom);
}

/* The same colour-blind-checked colours as the languages and the pie, one per lane. */
static Color LaneColor(int lane) { return LANG_PALETTE[(lane + 0) % 8]; }

static const char *FormatLongTime(long long when)
{
    static char formatted[40];
    time_t whenTime = (time_t)when;
    strftime(formatted, sizeof formatted, "%d %b %Y, %H:%M", localtime(&whenTime));
    return formatted;
}

/* The details of a commit, next to the mouse (like the contribution calendar's). */
static void DrawCommitTip(Repo *repo, int commitIndex)
{
    Commit *commit = &repo->commits[commitIndex];
    CommitStats(repo, commitIndex);
    static char        kinds[MAX_FILES * 2];
    static const char *names[MAX_FILES * 2];
    int fileCount = CommitFileList(repo, commitIndex, kinds, names, MAX_FILES * 2);

    enum { MAX_LINES = 16 };
    char  text[MAX_LINES][200];
    Color colors[MAX_LINES];
    bool  bold[MAX_LINES] = {0}, rule[MAX_LINES] = {0}, showsAddsDels[MAX_LINES] = {0};
    char  kind[MAX_LINES] = {0};
    int   lineCount = 0;
    snprintf(text[lineCount], 200, "%.70s", commit->message); bold[lineCount] = true; colors[lineCount++] = UI_TEXT;
    snprintf(text[lineCount], 200, "%s   |   %s", CommitAuthorName(commit), FormatLongTime(commit->time)); colors[lineCount++] = UI_MUTED;
    if (commit->parentCount > 1)
        snprintf(text[lineCount], 200, "merge %s   |   joins %s and %s", commit->id, repo->commits[commit->parents[0]].id, repo->commits[commit->parents[1]].id);
    else if (commit->parentCount)
        snprintf(text[lineCount], 200, "commit %s   |   built on %s", commit->id, repo->commits[commit->parents[0]].id);
    else
        snprintf(text[lineCount], 200, "commit %s   |   the first commit", commit->id);
    colors[lineCount++] = UI_MUTED;
    rule[lineCount] = true;
    snprintf(text[lineCount], 200, "%d file%s changed   ", commit->statFiles, commit->statFiles == 1 ? "" : "s"); showsAddsDels[lineCount] = true; colors[lineCount++] = UI_TEXT;
    int shown = fileCount < 6 ? fileCount : 6;
    for (int i = 0; i < shown; i++) {
        snprintf(text[lineCount], 200, "%.80s", names[i]);
        kind[lineCount] = kinds[i];
        colors[lineCount++] = UI_TEXT;
    }
    if (fileCount > shown) { snprintf(text[lineCount], 200, "...and %d more", fileCount - shown); colors[lineCount++] = UI_MUTED; }
    if (commit->copied) { snprintf(text[lineCount], 200, "(brought in from the original repository)"); colors[lineCount++] = UI_MUTED; }
    rule[lineCount] = true;
    snprintf(text[lineCount], 200, "Click to see what changed"); colors[lineCount++] = UI_MUTED;

    int width = 0, ruleCount = 0;
    for (int i = 0; i < lineCount; i++) {
        int lineWidth = MeasureText(text[i], FONT_SIZE) + (kind[i] ? 30 : 0);
        if (showsAddsDels[i]) lineWidth += MeasureText(TextFormat("+%d -%d", commit->statAdds, commit->statDels), FONT_SIZE) + 10;
        if (lineWidth > width) width = lineWidth;
        if (rule[i]) ruleCount++;
    }
    int boxWidth = width + 32, boxHeight = lineCount * 26 + ruleCount * 10 + 22;
    Vector2 mouse = GetMousePosition();
    int x = (int)mouse.x + 18, y = (int)mouse.y + 22;
    if (x + boxWidth > screenWidth - 8) x = (int)mouse.x - 18 - boxWidth;
    if (x < 8) x = 8;
    if (y + boxHeight > screenHeight - 8) y = (int)mouse.y - 12 - boxHeight;
    if (y < 8) y = 8;
    Rectangle box = {(float)x, (float)y, (float)boxWidth, (float)boxHeight};
    DrawRectangleRounded((Rectangle){box.x + 3, box.y + 4, box.width, box.height}, 0.06f, 6, (Color){0, 0, 0, 120});
    DrawRectangleRounded(box, 0.06f, 6, (Color){36, 41, 49, 255});
    DrawRectangleRoundedLinesEx(box, 0.06f, 6, 1, UI_BORDER);
    int textY = y + 12;
    for (int i = 0; i < lineCount; i++) {
        if (rule[i]) { DrawLine(x + 12, textY + 3, x + boxWidth - 12, textY + 3, UI_BORDER); textY += 10; }
        int textX = x + 16;
        if (kind[i]) {
            Color kindColor = kind[i] == 'A' ? UI_GREEN_TEXT : kind[i] == 'D' ? UI_RED_TEXT : UI_YELLOW;
            char kindText[2] = {kind[i], 0};
            DrawTextBold(kindText, textX, textY, FONT_SIZE, kindColor);
            textX += 30;
        }
        if (bold[i]) DrawTextBold(text[i], textX, textY, FONT_SIZE, colors[i]);
        else         DrawText(text[i], textX, textY, FONT_SIZE, colors[i]);
        if (showsAddsDels[i]) DrawAddsDels(commit->statAdds, commit->statDels, textX + MeasureText(text[i], FONT_SIZE), textY);
        textY += 26;
    }
}

static void CommitGraph(Repo *repo, Rectangle area)
{
    BuildGraph(repo);
    int count = repo->commitCount;
    float rowHeight = 36, laneWidth = 18;
    float laneX = area.x + 22;                               /* the middle of lane 0 */
    float graphWidth = graph.maxLanes * laneWidth;
    float maxGraphWidth = area.width * 0.4f;
    if (graphWidth > maxGraphWidth) graphWidth = maxGraphWidth;
    float textX = area.x + 22 + graphWidth + 8;
    ScrollArea(area, &graphScroll, count * rowHeight + 8);
    DrawRectangleRec(area, UI_PANEL);
    DrawRectangleLinesEx(area, 1, UI_BORDER);
    int firstRow = (int)(graphScroll / rowHeight) - 1, lastRow = (int)((graphScroll + area.height) / rowHeight) + 1;
    if (firstRow < 0) firstRow = 0;
    if (lastRow > count - 1) lastRow = count - 1;
    int hoveredRow = -1;
    bool pillHover = false;
    Vector2 mouse = GetMousePosition();

    BeginScissorMode((int)area.x + 1, (int)area.y + 1, (int)area.width - 14, (int)area.height - 2);
    /* row backgrounds first, so the lines go over them */
    for (int i = firstRow; i <= lastRow; i++) {
        Rectangle row = {area.x + 1, area.y + 4 + i * rowHeight - graphScroll, area.width - 14, rowHeight};
        if (IsHovered(area) && CheckCollisionPointRec(mouse, row)) { hoveredRow = i; DrawRectangleRec(row, UI_HOVER); }
    }
    /* the lines: from each row's dot level down to the next row's */
    BeginScissorMode((int)area.x + 1, (int)area.y + 1, (int)(graphWidth + 30), (int)area.height - 2);
    for (int i = firstRow - 1; i <= lastRow; i++) {
        if (i < 0 || i >= count - 1) continue;
        GraphRow *rowInfo = &graph.rows[i], *nextRowInfo = &graph.rows[i + 1];
        float y1 = area.y + 4 + i * rowHeight + rowHeight / 2 - graphScroll, y2 = y1 + rowHeight;
        int belowCommit = graph.order[i + 1];                /* the commit on the next row */
        for (int k = 0; k < rowInfo->laneCount; k++) {
            int waiting = graph.waitingFor[rowInfo->firstLane + k];
            if (waiting < 0) continue;
            int fromLane = graph.fromLane[rowInfo->firstLane + k];
            int toLane = waiting == belowCommit ? nextRowInfo->lane : k;
            Vector2 lineStart = {laneX + fromLane * laneWidth, y1}, lineEnd = {laneX + toLane * laneWidth, y2};
            Color laneColor = LaneColor(k);
            if (lineStart.x == lineEnd.x) DrawLineEx(lineStart, lineEnd, 2.5f, laneColor);
            else DrawLineBezier(lineStart, lineEnd, 2.5f, laneColor);
        }
    }
    /* the dots */
    for (int i = firstRow; i <= lastRow; i++) {
        int commitIndex = graph.order[i];
        Commit *commit = &repo->commits[commitIndex];
        float cy = area.y + 4 + i * rowHeight + rowHeight / 2 - graphScroll, cx = laneX + graph.rows[i].lane * laneWidth;
        Color laneColor = LaneColor(graph.rows[i].lane);
        if (commit->parentCount > 1) { DrawCircle((int)cx, (int)cy, 7, laneColor); DrawCircle((int)cx, (int)cy, 4, (Color){22, 27, 34, 255}); }  /* merges: a ring */
        else DrawCircle((int)cx, (int)cy, 6, laneColor);
        for (int branchIndex = 0; branchIndex < repo->branchCount; branchIndex++)                            /* a branch's newest commit */
            if (repo->branches[branchIndex].tip == commitIndex) {
                DrawCircleLines((int)cx, (int)cy, 10, laneColor);
                if (branchIndex == repo->currentBranch) DrawCircleLines((int)cx, (int)cy, 11, laneColor);
            }
    }
    EndScissorMode();
    BeginScissorMode((int)area.x + 1, (int)area.y + 1, (int)area.width - 14, (int)area.height - 2);
    /* the words */
    for (int i = firstRow; i <= lastRow; i++) {
        int commitIndex = graph.order[i];
        Commit *commit = &repo->commits[commitIndex];
        float y = area.y + 4 + i * rowHeight - graphScroll;
        float tagX = textX;
        /* a name tag for each branch whose newest commit this is: the one you're on is filled in */
        for (int branchIndex = 0; branchIndex < repo->branchCount; branchIndex++) {
            if (repo->branches[branchIndex].tip != commitIndex) continue;
            const char *branchName = repo->branches[branchIndex].name;
            bool isCurrent = branchIndex == repo->currentBranch, isDefault = branchIndex == repo->defaultBranch;
            int pillWidth = MeasureText(branchName, FONT_SIZE) + 16 + (isCurrent ? 18 : 0);
            if (tagX + pillWidth > area.x + area.width * 0.75f) break;
            Rectangle pill = {tagX, y + 6, (float)pillWidth, 24};
            Color pillColor = isDefault ? UI_GREEN_TEXT : UI_LINK;
            bool pillHovered = CheckCollisionPointRec(mouse, pill) && IsHovered(area);
            DrawRectangleRounded(pill, 0.5f, 6, isCurrent ? WithAlpha(pillColor, pillHovered ? 0.55f : 0.40f) : WithAlpha(pillColor, pillHovered ? 0.30f : 0.16f));
            DrawRectangleRoundedLinesEx(pill, 0.5f, 6, 1, WithAlpha(pillColor, 0.8f));
            if (isCurrent) DrawCircle((int)tagX + 12, (int)y + 18, 4, UI_TEXT);      /* you are here */
            DrawText(branchName, (int)tagX + 8 + (isCurrent ? 18 : 0), (int)y + 8, FONT_SIZE, isCurrent ? UI_TEXT : pillColor);
            if (pillHovered) {
                pillHover = true;
                wantedCursor = MOUSE_CURSOR_POINTING_HAND;
                UpdateBranchStats(repo);
                SetTooltip(TextFormat("%s%s", branchName, isDefault ? "  (default branch)" : ""),
                       isDefault ? TextFormat("%s", isCurrent ? "You're on this branch" : "The branch git clone gives you")
                           : TextFormat("%d commit%s ahead of %s, %d behind", branchStats.ahead[branchIndex], branchStats.ahead[branchIndex] == 1 ? "" : "s", DefaultBranchName(repo), branchStats.behind[branchIndex]),
                       isCurrent ? "You're working on this branch" : "Click to switch to this branch");
                if (WasClicked(pill)) SwitchBranch(repo, branchIndex);
            }
            tagX += pillWidth + 8;
        }
        float right = area.x + area.width - 24;
        const char *when = TimeAgo(commit->time);
        const char *who = CommitAuthorName(commit);
        int idWidth = MeasureText(commit->id, FONT_SIZE), whenWidth = MeasureText(when, FONT_SIZE);
        float idX = right - idWidth, whenX = idX - 24 - whenWidth;
        float whoWidth = (float)MeasureText(who, FONT_SIZE);
        if (whoWidth > 220) whoWidth = 220;
        float whoX = whenX - 24 - whoWidth;
        bool roomy = whoX - 16 - tagX > 160;                  /* narrow window: just the message and id */
        DrawTextFit(commit->message, (int)tagX, (int)y + 8, FONT_SIZE, (int)((roomy ? whoX - 40 : idX - 20) - tagX), commit->copied ? UI_MUTED : UI_TEXT);
        if (roomy) {
            DrawAvatar(commit->author, (int)whoX - 16, (int)y + 18, 10);
            DrawTextFit(who, (int)whoX, (int)y + 8, FONT_SIZE, (int)whoWidth, UI_MUTED);
            DrawText(when, (int)whenX, (int)y + 8, FONT_SIZE, UI_MUTED);
        }
        DrawText(commit->id, (int)idX, (int)y + 8, FONT_SIZE, UI_LINK);
    }
    EndScissorMode();
    DrawScrollbar(area, graphScroll, count * rowHeight + 8);

    if (hoveredRow >= 0 && !pillHover) {
        wantedCursor = MOUSE_CURSOR_POINTING_HAND;
        int commitIndex = graph.order[hoveredRow];
        if (WasClicked((Rectangle){area.x, area.y, area.width - 14, area.height})) { OpenCommit(commitIndex); return; }
        DrawCommitTip(repo, commitIndex);
    }
}

/* The commits in the current branch's history, newest first (kept until the commits or the branch change). */
static struct { const Repo *repo; unsigned generation; int tip, commitCount, count, capacity; int *commits; } branchHistory;
static void BuildHistory(Repo *repo)
{
    int tip = HeadIndex(repo);
    if (branchHistory.repo == repo && branchHistory.generation == repo->generation && branchHistory.tip == tip && branchHistory.commitCount == repo->commitCount) return;
    branchHistory.repo = repo; branchHistory.generation = repo->generation; branchHistory.tip = tip; branchHistory.commitCount = repo->commitCount; branchHistory.count = 0;
    uint8_t *marks = calloc((size_t)repo->commitCount + 1, 1);
    branchHistory.commits = GrowArray(branchHistory.commits, &branchHistory.capacity, repo->commitCount + 1, sizeof(int));
    if (!marks || branchHistory.capacity <= repo->commitCount) { free(marks); return; }
    MarkHistory(repo, tip, marks, 1);
    for (int k = repo->commitCount - 1; k >= 0; k--) if (marks[k]) branchHistory.commits[branchHistory.count++] = k;
    free(marks);
}

/* Draws a repository's Commits tab. */
void TabCommits(Repo *repo)
{
    int total = repo->commitCount;
    BuildHistory(repo);
    int count = branchHistory.count;
    /* Graph or List, and which branch */
    if (total) {
        const char *labels[2] = {"List", "Graph"};
        float buttonX = 20;
        for (int option = 0; option < 2; option++) {
            Rectangle button = {buttonX, 184, 110, 34};
            bool selected = commitsTabView == option;
            if (ButtonEnabled(button, labels[option], selected ? UI_BLUE : UI_BUTTON, true) && !selected) commitsTabView = option;
            buttonX += 118;
        }
        BranchButton(repo, (Rectangle){buttonX + 8, 184, 260, 34});
        int merges = 0;
        for (int k = 0; k < total; k++) if (repo->commits[k].parentCount > 1) merges++;
        const char *info = commitsTabView == 1
            ? TextFormat("%d commit%s on %d branch%s%s", total, total == 1 ? "" : "s", repo->branchCount, repo->branchCount == 1 ? "" : "es",
                         merges ? TextFormat(", %d merge%s", merges, merges == 1 ? "" : "s") : "")
            : TextFormat("%d commit%s in %s's history", count, count == 1 ? "" : "s", CurrentBranchName(repo));
        if (screenWidth - 20 - MeasureText(info, FONT_SIZE) > buttonX + 290) DrawText(info, screenWidth - 20 - MeasureText(info, FONT_SIZE), 191, FONT_SIZE, UI_MUTED);
        if (commitsTabView == 1) { CommitGraph(repo, (Rectangle){20, 230, (float)screenWidth - 40, (float)screenHeight - 250}); return; }
    }
    Rectangle listArea = {20, total ? 230.0f : 186.0f, (float)screenWidth - 40, (float)screenHeight - (total ? 250 : 206)};
    float rowHeight = 74;
    ScrollArea(listArea, &commitListScroll, count * rowHeight);
    if (!count) {
        DrawText("No commits yet.", 20, 196, FONT_SIZE, UI_TEXT);
        if (CanEdit(repo)) DrawText("Edit some files in the Code tab, write a message, then press Commit.", 20, 226, FONT_SIZE, UI_MUTED);
        return;
    }
    BeginScissorMode((int)listArea.x, (int)listArea.y, (int)listArea.width, (int)listArea.height);
    for (int k = 0; k < count; k++) {
        int commitIndex = branchHistory.commits[k];
        Commit *commit = &repo->commits[commitIndex];
        Rectangle row = {listArea.x, listArea.y + k * rowHeight - commitListScroll, listArea.width - 12, rowHeight};
        if (row.y > listArea.y + listArea.height || row.y + row.height < listArea.y) continue;
        bool hovered = IsHovered(listArea) && IsHovered(row);
        if (hovered) wantedCursor = MOUSE_CURSOR_POINTING_HAND;
        DrawRectangleRec(row, hovered ? UI_HOVER : UI_PANEL);
        DrawRectangleLinesEx(row, 1, UI_BORDER);
        DrawAvatar(commit->author, (int)row.x + 36, (int)row.y + 37, 18);
        DrawTextFit(commit->message, (int)row.x + 70, (int)row.y + 14, FONT_SIZE, (int)row.width - 310, UI_TEXT);
        DrawTextFit(TextFormat("%s committed %s", CommitAuthorName(commit), TimeAgo(commit->time)), (int)row.x + 70, (int)row.y + 42, FONT_SIZE, (int)row.width - 310, UI_MUTED);

        Rectangle idPill = {row.x + row.width - 120, row.y + 20, 102, 34};
        DrawRectangleRounded(idPill, 0.3f, 6, UI_BUTTON);
        DrawText(commit->id, (int)idPill.x + 12, (int)idPill.y + 7, FONT_SIZE, UI_LINK);
        if (k == 0) {
            Rectangle latest = {row.x + row.width - 220, row.y + 22, 88, 30};
            DrawRectangleRounded(latest, 0.5f, 6, (Color){35, 134, 54, 90});
            DrawText("latest", (int)latest.x + 14, (int)latest.y + 5, FONT_SIZE, UI_GREEN_TEXT);
        }
        if (IsHovered(listArea) && WasClicked(row)) OpenCommit(commitIndex);
    }
    EndScissorMode();
    DrawScrollbar(listArea, commitListScroll, count * rowHeight);
}
