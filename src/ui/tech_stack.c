/*
 * tech_stack.c - language bars and the tech stack pie
 *
 * The language bar on a repository, and the "Tech stack" card on a
 * profile: lines changed per language, counted a little each frame so the
 * window stays responsive.
 */

#include "tech_stack.h"
#include "animation.h"
#include "app_state.h"
#include "diff.h"
#include "languages.h"
#include "model.h"
#include "repo.h"
#include "save_file.h"
#include "themes.h"
#include "ui_core.h"
#include "users.h"
#include "widgets.h"

/* The colour a language has in the breakdown just built (grey if it's in "Other" or not code). */
Color LangColor(int lang)
{
    for (int index = 0; index < langPartCount; index++) if (langParts[index].lang == lang) return langParts[index].color;
    return LANG_OTHER;
}
static const char *LangPartName(LangPart *part) { return part->lang >= 0 ? LANG_NAMES[part->lang] : "Other"; }

/* A thin bar split into each language's share, with a key underneath.
   Hovering part of the bar shows its numbers. */
void DrawLanguageBar(Rectangle area)
{
    if (!langTotal) {
        DrawTextFit(langSkipped[0] ? TextFormat("Languages: no code yet (%s isn't counted)", langSkipped) : "Languages: no code yet",
                (int)area.x, (int)area.y + 6, FONT_SIZE, (int)area.width, UI_MUTED);
        return;
    }
    Rectangle bar = {area.x, area.y + 2, area.width, 10};
    int hovered = -1;
    for (int pass = 0; pass < 2; pass++) {               /* first find the part under the mouse, then draw */
        float x = bar.x;
        for (int i = 0; i < langPartCount; i++) {
            LangPart *part = &langParts[i];
            float width = bar.width * part->lines / langTotal;
            if (i == langPartCount - 1) width = bar.x + bar.width - x;    /* no rounding gap at the end */
            if (pass == 0) {
                if (IsHovered((Rectangle){x, bar.y - 6, width, bar.height + 12})) hovered = i;
            } else {
                float drawWidth = width - (i < langPartCount - 1 ? 2 : 0);    /* a 2-pixel gap between parts */
                if (drawWidth < 2) drawWidth = 2;
                float grow = HoverAmount(AnimationKey("language bar", bar.x + bar.y * 4096, (float)part->lang), i == hovered) * 3;
                float fade = HoverAmount(AnimationKey("language bar fade", bar.x + bar.y * 4096, (float)part->lang), hovered >= 0 && i != hovered);
                Color shown = fade > 0 ? MixColors(part->color, ColorBrightness(part->color, -0.3f), fade * 0.6f) : part->color;
                DrawRectangleRounded((Rectangle){x, bar.y - grow, drawWidth, bar.height + grow * 2}, 0.6f, 4, shown);
            }
            x += width;
        }
    }
    /* the key: name and share for each, as many as fit */
    float keyX = area.x, keyY = area.y + 18;
    int shown = 0;
    for (int i = 0; i < langPartCount; i++) {
        LangPart *part = &langParts[i];
        const char *percent = TextFormat(" %.1f%%", 100.0 * part->lines / langTotal);
        int width = 14 + MeasureText(LangPartName(part), FONT_SIZE) + MeasureText(percent, FONT_SIZE) + 22;
        int moreWidth = i < langPartCount - 1 ? MeasureText("+9 more", FONT_SIZE) : 0;  /* leave room to say what's left out */
        if (keyX + width + moreWidth > area.x + area.width) break;
        shown++;
        DrawCircle((int)keyX + 5, (int)keyY + 10, 5, part->color);
        DrawText(LangPartName(part), (int)keyX + 16, (int)keyY, FONT_SIZE, UI_TEXT);
        DrawText(percent, (int)keyX + 16 + MeasureText(LangPartName(part), FONT_SIZE), (int)keyY, FONT_SIZE, UI_MUTED);
        keyX += width;
    }
    if (shown < langPartCount) {
        DrawText(TextFormat("+%d more", langPartCount - shown), (int)keyX, (int)keyY, FONT_SIZE, UI_MUTED);
        keyX += MeasureText("+9 more", FONT_SIZE) + 22;
    } else if (langSkipped[0]) {
        const char *note = TextFormat("|   not counted: %s", langSkipped);
        if (keyX + MeasureText(note, FONT_SIZE) <= area.x + area.width) DrawText(note, (int)keyX, (int)keyY, FONT_SIZE, UI_MUTED);
    }
    if (hovered >= 0) {
        LangPart *part = &langParts[hovered];
        SetTooltip(LangPartName(part), TextFormat("%d line%s  (%.1f%%)", part->lines, part->lines == 1 ? "" : "s", 100.0 * part->lines / langTotal),
               TextFormat("in %d %s%s", part->files, langPartsAreCommits ? "commit" : "file", part->files == 1 ? "" : "s"));
    }
}

/* ---------- a person's tech stack ----------
   Every commit they made (in any repository; not ones merely copied into a
   fork) is compared with the commit before it, and the lines changed - added
   plus removed - are counted under each file's language. Each commit's
   counts are worked out once and kept, so only new commits cost anything. */
typedef struct { Repo *r; int *linesPerLang; } TechCounter;
static void CountTechChange(void *context, const char *name, int oldBlob, int newBlob)
{
    TechCounter *counter = context;
    if (oldBlob == newBlob) return;                      /* only the mode changed */
    int blob = newBlob >= 0 ? newBlob : oldBlob;         /* a deleted file counts in the language it was */
    /* a file's language hardly ever changes from one version to the next, so it's
       worked out once per path (from the first version seen) and remembered */
    static PathIndexMap pathLang;
    int lang = PathIndexGet(&pathLang, name) - 100;
    if (lang < -50) {
        LangCache *cache = GetBlobLangCache(counter->r, blob);
        lang = cache ? LangFromCache(cache, name) : LANG_BINARY;
        PathIndexPut(&pathLang, name, lang + 100);
    }
    if (lang < 0 || lang >= LANG_FIRST_NONCODE) return;  /* not code (Markdown, text...) or binary */
    int added = 0, removed = 0;
    DiffCounts(BlobText(counter->r, oldBlob), BlobText(counter->r, newBlob), &added, &removed);
    counter->linesPerLang[lang] += added + removed;
}
static const int *CommitLangLines(Repo *repo, int commitIndex)
{
    Commit *commit = &repo->commits[commitIndex];
    if (!commit->langLines) {
        int *lines = calloc(LANG_COUNT, sizeof(int));
        if (!lines) return NULL;
        TechCounter counter = {repo, lines};
        WalkChanges(ParentOf(repo, commitIndex), commit, CountTechChange, &counter);
        commit->langLines = lines;
    }
    return commit->langLines;
}

/* The totals for one person, kept until any repository changes. Counting a
   big history takes a while the first time, so it's done a little each frame
   (never more than ~25 ms) - the window keeps working, and the card says
   how far it's got. */
static struct {
    int userId; unsigned stamp; bool done; int repoIndex, commitIndex, commitsToCount, commitsCounted;
    int lines[LANG_COUNT], commits[LANG_COUNT]; int totalLines;
} techTotals = {-999, 0, false, 0, 0, 0, 0, {0}, {0}, 0};
static unsigned StateStamp(void)
{
    unsigned stamp = (unsigned)repoCount * 2654435761u + (unsigned)outsiderCount * 40503u + (unsigned)userCount + (unsigned)currentUserId * 977u;
    for (int i = 0; i < repoCount; i++)            /* (what you can see counts too: private repositories) */
        stamp = stamp * 31u + repos[i].generation * 7u + (unsigned)repos[i].commitCount + (CanSee(&repos[i], currentUserId) ? 1u : 0u);
    for (int i = 0; i < userCount; i++) stamp = stamp * 31u + (unsigned)users[i].claimCount;
    return stamp;
}
static void CountTechStack(int userId)
{
    unsigned stamp = StateStamp();
    if (techTotals.userId != userId || techTotals.stamp != stamp) {  /* start again */
        memset(&techTotals, 0, sizeof techTotals);
        techTotals.userId = userId; techTotals.stamp = stamp;
        for (int i = 0; i < repoCount; i++)
            for (int k = 0; k < repos[i].commitCount && CanSee(&repos[i], currentUserId); k++)
                if (repos[i].commits[k].author == userId && !repos[i].commits[k].copied && !repos[i].commits[k].langLines) techTotals.commitsToCount++;
    }
    if (techTotals.done) return;
    double until = GetTime() + 0.025;
    for (; techTotals.repoIndex < repoCount; techTotals.repoIndex++, techTotals.commitIndex = 0) {
        Repo *repo = &repos[techTotals.repoIndex];
        if (!CanSee(repo, currentUserId)) continue;         /* private, and you're not in it */
        for (; techTotals.commitIndex < repo->commitCount; techTotals.commitIndex++) {
            Commit *commit = &repo->commits[techTotals.commitIndex];
            if (commit->author != userId || commit->copied) continue;
            if (!commit->langLines) {
                if (GetTime() > until) return;                /* carry on next frame */
                techTotals.commitsCounted++;
            }
            const int *lines = CommitLangLines(repo, techTotals.commitIndex);
            for (int lang = 0; lines && lang < LANG_FIRST_NONCODE; lang++)
                if (lines[lang]) { techTotals.lines[lang] += lines[lang]; techTotals.commits[lang]++; techTotals.totalLines += lines[lang]; }
        }
    }
    techTotals.done = true;
}

/* One slice of a pie, pushed out from the centre by lift (0..1: how far it has risen
   on hover) and dimmed by fade (0..1) while another slice is the one hovered.
   separated: there are other slices, so leave a thin gap between them. */
void DrawPieSlice(Vector2 center, float radius, float startAngle, float sweep, Color color, float lift, float fade, bool separated)
{
    float rise = radius * 0.08f < 5 ? radius * 0.08f : 5;  /* how far a hovered slice moves out: less on a small pie */
    float middle = (startAngle + sweep / 2) * DEG2RAD, push = (separated ? 1.2f : 0) + rise * lift;
    Vector2 sliceCenter = {center.x + cosf(middle) * push, center.y + sinf(middle) * push};
    Color shown = fade > 0 ? MixColors(color, ColorBrightness(color, -0.35f), fade) : color;
    DrawCircleSector(sliceCenter, radius + rise * 0.6f * lift, startAngle, startAngle + sweep, (int)(sweep / 3) + 4, shown);
}

/* A pie of the parts BuildLangParts made. Each slice eases outward while it's hovered (or
   while it's highlighted - its row in the key is hovered, -1 for none) and back after.
   Returns the slice under the mouse, or -1. */
static int DrawLangPie(Vector2 center, float radius, int highlighted)
{
    if (!langTotal) { DrawRing(center, radius - 3, radius, 0, 360, 48, UI_BORDER); return -1; }
    Vector2 mouse = GetMousePosition();
    float dx = mouse.x - center.x, dy = mouse.y - center.y;
    bool inside = dx * dx + dy * dy <= (radius + 4) * (radius + 4) && IsHovered((Rectangle){center.x - radius - 4, center.y - radius - 4, radius * 2 + 8, radius * 2 + 8});
    float mouseAngle = atan2f(dy, dx) * RAD2DEG;
    if (mouseAngle < -90) mouseAngle += 360;
    int hovered = -1;
    float angle = -90;
    for (int i = 0; i < langPartCount; i++) {
        float sweep = 360.0f * langParts[i].lines / langTotal;
        if (inside && mouseAngle >= angle && mouseAngle < angle + sweep) hovered = i;
        angle += sweep;
    }
    int lit = hovered >= 0 ? hovered : highlighted;      /* the slice under the mouse, or the key row under it */
    angle = -90;
    for (int i = 0; i < langPartCount; i++) {
        float sweep = 360.0f * langParts[i].lines / langTotal;
        float lift = HoverAmount(AnimationKey("tech pie", center.x + center.y * 4096, (float)langParts[i].lang), i == lit);
        float fade = HoverAmount(AnimationKey("tech pie fade", center.x + center.y * 4096, (float)langParts[i].lang), lit >= 0 && i != lit);
        DrawPieSlice(center, radius, angle, sweep, langParts[i].color, langPartCount > 1 ? lift : lift * 0.4f, fade * 0.6f, langPartCount > 1);
        angle += sweep;
    }
    return hovered;
}

/* The "Tech stack" card on a profile: a pie and its key (or, in a narrow window, a bar). */
void DrawTechStack(Rectangle area, int userId, bool asBar)
{
    CountTechStack(userId);
    bool notCounted[LANG_COUNT] = {0};
    BuildLangParts(techTotals.lines, techTotals.commits, notCounted, false, true);
    DrawRectangleRec(area, UI_PANEL);
    DrawRectangleLinesEx(area, 1, UI_BORDER);
    DrawText("Tech stack", (int)area.x + 16, (int)area.y + 12, FONT_SIZE, UI_TEXT);
    if (!techTotals.done) {                              /* still counting (a big history, the first time) */
        int percent = techTotals.commitsToCount ? techTotals.commitsCounted * 100 / techTotals.commitsToCount : 0;
        DrawTextFit(TextFormat("Counting the languages in %d commits...  %d%%", techTotals.commitsToCount, percent), (int)area.x + 16, (int)area.y + 46, FONT_SIZE, (int)area.width - 32, UI_MUTED);
        DrawRectangle((int)area.x + 16, (int)area.y + 78, (int)((area.width - 32) * percent / 100), 6, accentColor);
        return;
    }
    const char *subtitle = "lines changed, all repositories";
    if (area.width - 32 - MeasureText("Tech stack", FONT_SIZE) - 16 > MeasureText(subtitle, FONT_SIZE))
        DrawText(subtitle, (int)(area.x + area.width) - 16 - MeasureText(subtitle, FONT_SIZE), (int)area.y + 12, FONT_SIZE, UI_MUTED);
    if (!techTotals.totalLines) {
        DrawTextFit(userId == currentUserId ? "No code committed yet - your languages will show up here." : "No code committed yet.",
                (int)area.x + 16, (int)area.y + 46, FONT_SIZE, (int)area.width - 32, UI_MUTED);
        return;
    }
    if (asBar) {                                         /* narrow window: the same bar the repositories have */
        langSkipped[0] = 0;
        DrawLanguageBar((Rectangle){area.x + 16, area.y + 42, area.width - 32, 40});
        return;
    }
    float radius = (area.height - 56) / 2;
    if (radius > 62) radius = 62;
    Vector2 center = {area.x + 20 + radius, area.y + 44 + radius};
    float keyX = center.x + radius + 24, keyTop = area.y + 44, keyWidth = area.x + area.width - 16 - keyX;
    int rows = (int)((area.height - 52) / 22);
    int keyHovered = -1;                                 /* a row of the key under the mouse lights its slice too */
    for (int i = 0; i < langPartCount && i < rows; i++)
        if (IsHovered((Rectangle){keyX - 4, keyTop + i * 22 - 2, keyWidth + 8, 22})) keyHovered = i;
    int hovered = DrawLangPie(center, radius, keyHovered);
    if (hovered < 0) hovered = keyHovered;
    float keyY = keyTop;
    for (int i = 0; i < langPartCount && i < rows; i++) {
        LangPart *part = &langParts[i];
        float glow = HoverAmount(AnimationKey("tech key", keyX + keyTop * 4096, (float)part->lang), i == hovered);
        if (glow > 0) DrawRectangleRounded((Rectangle){keyX - 6, keyY - 2, keyWidth + 12, 23}, 0.4f, 6, WithAlpha(UI_HOVER, glow));
        DrawCircleV((Vector2){keyX + 5, keyY + 10}, 5 + 1.5f * glow, part->color);
        const char *percent = TextFormat("%.1f%%", 100.0 * part->lines / langTotal);
        int percentWidth = MeasureText(percent, FONT_SIZE);
        DrawTextFit(LangPartName(part), (int)keyX + 16, (int)keyY, FONT_SIZE, (int)keyWidth - 16 - percentWidth - 10, UI_TEXT);
        DrawText(percent, (int)(keyX + keyWidth) - percentWidth, (int)keyY, FONT_SIZE, MixColors(UI_MUTED, UI_TEXT, glow));
        keyY += 22;
    }
    if (hovered >= 0) {
        LangPart *part = &langParts[hovered];
        SetTooltip(LangPartName(part), TextFormat("%d line%s changed  (%.1f%%)", part->lines, part->lines == 1 ? "" : "s", 100.0 * part->lines / langTotal),
               TextFormat("in %d commit%s", part->files, part->files == 1 ? "" : "s"));
    }
}
