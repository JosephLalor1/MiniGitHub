/*
 * screen_splash.c - the splash screen shown when the app opens
 *
 * The logo pops in over one of the animated themes (picked at random each
 * time), with the app's name and a progress bar in the theme's colour. Its
 * start plays before the save file loads (so the window shows something while
 * it does), the rest after; a click or a key skips to the app once loading is
 * done, and it fades into the app at the end.
 */

#include "screen_splash.h"
#include "app_state.h"
#include "git_server.h"
#include "header_bar.h"
#include "themes.h"
#include "ui_core.h"

#define SPLASH_INTRO_SECONDS 0.7f    /* played before loading: the logo and name appear */
#define SPLASH_SECONDS       2.6f    /* the whole splash, if loading was quick */
#define SPLASH_FILL_SECONDS  0.45f   /* the progress bar filling up once loading is done */
#define SPLASH_FADE_SECONDS  0.35f   /* the fade into the app at the end */

static const Color PANEL_COLOR = {22, 27, 34, 255};

static float loadedAt = -1;          /* when loading finished (seconds since the window opened), -1 = not yet */
static int   splashTheme = -1;       /* the theme behind it this time, -1 = not picked yet */

/* 0 before start, 1 after start + length, easing in between: quick at first, gentle at the end. */
static float EaseOut(float seconds, float start, float length)
{
    float progress = (seconds - start) / length;
    if (progress <= 0) return 0;
    if (progress >= 1) return 1;
    float remaining = 1 - progress;
    return 1 - remaining * remaining * remaining;
}

/* The same, but overshooting a little before settling - for the logo's pop. */
static float EaseOutBack(float seconds, float start, float length)
{
    float progress = (seconds - start) / length;
    if (progress <= 0) return 0;
    if (progress >= 1) return 1;
    const float overshoot = 1.70158f;
    float shifted = progress - 1;
    return 1 + (overshoot + 1) * shifted * shifted * shifted + overshoot * shifted * shifted;
}

/* Text centred on x, faded by alpha. */
static void DrawCenteredText(const char *text, float centerX, float y, int fontSize, Color color, float alpha, bool bold)
{
    int x = (int)centerX - MeasureText(text, fontSize) / 2;
    if (bold) DrawTextBold(text, x, (int)y, fontSize, WithAlpha(color, alpha));
    else      DrawText(text, x, (int)y, fontSize, WithAlpha(color, alpha));
}

/* One frame of the splash, seconds after the window opened. fade (0..1) darkens
   it at the end, on the way into the app. */
static void DrawSplash(float seconds, float fade)
{
    float width = (float)GetScreenWidth(), height = (float)GetScreenHeight();
    ClearBackground(UI_BASE);

    /* the backdrop: one animated theme (any but Classic, which is plain), picked at random */
    if (splashTheme < 0) splashTheme = GetRandomValue(THEME_CLASSIC + 1, THEME_COUNT - 1);
    DrawTheme(splashTheme, (Rectangle){0, 0, width, height}, 0.9f);
    Color accent = THEMES[splashTheme].accent;

    /* the panel in the middle */
    float panelWidth = width - 80 < 560 ? width - 80 : 560, panelHeight = 330;
    Rectangle panel = {(width - panelWidth) / 2, (height - panelHeight) / 2, panelWidth, panelHeight};
    float centerX = panel.x + panel.width / 2;
    float appear = EaseOut(seconds, 0, 0.35f);
    DrawRectangleRounded((Rectangle){panel.x + 4, panel.y + 6, panel.width, panel.height}, 0.08f, 8, WithAlpha(BLACK, 0.35f * appear));
    DrawRectangleRounded(panel, 0.08f, 8, WithAlpha(PANEL_COLOR, 0.94f * appear));
    DrawRectangleRoundedLinesEx(panel, 0.08f, 8, 1.5f, WithAlpha(accent, 0.55f * appear));

    /* the logo pops in, inside a glow and a slowly turning ring in the theme's colour */
    float pop = EaseOutBack(seconds, 0.05f, 0.6f);
    Vector2 logoCenter = {centerX, panel.y + 108};
    if (pop > 0) {
        DrawCircleV(logoCenter, 80 * pop, WithAlpha(accent, 0.14f));
        DrawRing(logoCenter, 66 * pop, 70 * pop, 0, 360, 72, WithAlpha(accent, 0.25f));
        float spin = seconds * 140;
        DrawRing(logoCenter, 65 * pop, 71 * pop, spin, spin + 250, 60, accent);
        DrawLogoScaled(logoCenter.x, logoCenter.y, 3.0f * pop, UI_TEXT, PANEL_COLOR);
    }

    /* the name slides up into place, then the tagline */
    float titleIn = EaseOut(seconds, 0.3f, 0.5f);
    DrawCenteredText("Mini GitHub", centerX, panel.y + 200 + 14 * (1 - titleIn), 46, UI_TEXT, titleIn, true);
    DrawCenteredText("Your own GitHub, right on your computer", centerX, panel.y + 258, FONT_SIZE, UI_MUTED,
                     EaseOut(seconds, 0.55f, 0.5f), false);

    /* the progress bar: a sliding piece while loading, then filling up */
    Rectangle track = {panel.x + 60, panel.y + panel.height - 22, panel.width - 120, 4};
    DrawRectangleRounded(track, 1, 4, WithAlpha(UI_BORDER, appear));
    if (loadedAt < 0) {
        float slide = fmodf(seconds * 0.8f, 1.4f) - 0.4f;              /* -0.4 .. 1.0: a 40% piece crossing the track */
        float start = slide < 0 ? 0 : slide, end = slide + 0.4f > 1 ? 1 : slide + 0.4f;
        if (end > start) DrawRectangleRounded((Rectangle){track.x + track.width * start, track.y, track.width * (end - start), track.height}, 1, 4, accent);
    } else {
        float filled = EaseOut(seconds, loadedAt, SPLASH_FILL_SECONDS);
        DrawRectangleRounded((Rectangle){track.x, track.y, track.width * filled, track.height}, 1, 4, accent);
    }

    /* under the panel: what's happening */
    const char *status = loadedAt < 0 ? "Loading your repositories..." : "Click or press any key to continue";
    DrawCenteredText(status, centerX, panel.y + panel.height + 18, FONT_SIZE, UI_MUTED, EaseOut(seconds, 0.8f, 0.4f), false);

    if (fade > 0) DrawRectangle(0, 0, (int)width, (int)height, WithAlpha(UI_BASE, fade));
}

/* Throws away any typing or key presses made during the splash, so they don't
   land in the first text box afterwards. True if anything was pressed. */
static bool DrainInput(void)
{
    bool pressed = IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || IsMouseButtonPressed(MOUSE_BUTTON_RIGHT);
    while (GetKeyPressed() > 0) pressed = true;
    while (GetCharPressed() > 0) pressed = true;
    return pressed;
}

void SplashIntro(void)
{
    while (!WindowShouldClose() && GetTime() < SPLASH_INTRO_SECONDS) {
        DrainInput();
        BeginDrawing();
        DrawSplash((float)GetTime(), 0);
        EndDrawing();
    }
    /* one more frame: this one stays on screen while the save file loads */
    BeginDrawing();
    DrawSplash((float)GetTime(), 0);
    EndDrawing();
}

void SplashFinish(void)
{
    loadedAt = (float)GetTime();
    /* it ends at SPLASH_SECONDS, but never before the bar has filled (after a slow load) */
    float fadeStart = SPLASH_SECONDS - SPLASH_FADE_SECONDS;
    if (fadeStart < loadedAt + SPLASH_FILL_SECONDS) fadeStart = loadedAt + SPLASH_FILL_SECONDS;
    while (!WindowShouldClose()) {
        float seconds = (float)GetTime();
        GitServerPoll();                                   /* git can already be answered */
        if (DrainInput() && seconds < fadeStart) fadeStart = seconds;   /* skipped: start the fade now */
        float fade = seconds < fadeStart ? 0 : (seconds - fadeStart) / SPLASH_FADE_SECONDS;
        if (fade >= 1) break;
        BeginDrawing();
        DrawSplash(seconds, fade);
        EndDrawing();
    }
}
