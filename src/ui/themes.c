/*
 * themes.c - animated backgrounds
 *
 * The themes people can choose (code rain, flower garden, night sky...),
 * drawn with simple shapes every frame.
 */

#include "themes.h"

/* ------------------------------------------------------------------ */
/* Themes                                                             */
/*                                                                    */
/* Each theme is drawn with simple shapes every frame, so it can move. */
/* "Random" positions come from a hash of the item's number, so they   */
/* are the same every frame (things don't jump around).               */
/* ------------------------------------------------------------------ */

const ThemeInfo THEMES[THEME_COUNT] = {
    {"Classic",       "Plain and dark",       {247, 129, 102, 255}},
    {"Code rain",     "Falling green code",                 { 63, 185,  80, 255}},
    {"Flower garden", "Flowers and petals", {255, 143, 171, 255}},
    {"Night sky",     "Stars and a moon", {200, 170, 255, 255}},
    {"Circuit board", "Glowing circuits",           { 45, 212, 191, 255}},
    {"Ocean",         "Waves and bubbles",          { 88, 166, 255, 255}},
    {"Autumn",        "Falling leaves",                     {230, 126,  34, 255}},
    {"Snowfall",      "Snow drifting over pines",           {160, 205, 255, 255}},
    {"Aurora",        "Northern lights",                    {110, 240, 190, 255}},
    {"Synthwave",     "A neon sun and grid",                {255,  95, 200, 255}},
    {"Fireflies",     "A warm summer night",                {250, 215,  90, 255}},
    {"Rainy city",    "Rain over the rooftops",             {125, 170, 225, 255}},
};

/* A repeatable "random" number from 0 to 1 for one item and one of its properties. */
static float HashRandom(int item, int property)
{
    uint32_t hash = (uint32_t)item * 374761393u + (uint32_t)property * 668265263u + 0x9e3779b9u;
    hash = (hash ^ (hash >> 13)) * 1274126177u;
    hash ^= hash >> 16;
    return (float)(hash & 0xFFFFFF) / 16777216.0f;
}

/* The same colour, with its transparency multiplied by alpha (0..1). */
Color WithAlpha(Color color, float alpha)
{
    if (alpha < 0) alpha = 0;
    if (alpha > 1) alpha = 1;
    color.a = (unsigned char)(color.a * alpha);
    return color;
}

/* A colour part way from one to another (amount 0 = from, 1 = to). */
Color MixColors(Color from, Color to, float amount)
{
    return (Color){(unsigned char)(from.r + (to.r - from.r) * amount), (unsigned char)(from.g + (to.g - from.g) * amount),
                   (unsigned char)(from.b + (to.b - from.b) * amount), (unsigned char)(from.a + (to.a - from.a) * amount)};
}

/* Colour fades drawn as thin strips. (raylib has functions for this, but their
   arguments have changed between raylib versions, so these work with any of them.) */
static void DrawGradientVertical(int x, int y, int width, int height, Color top, Color bottom)
{
    for (int offset = 0; offset < height; offset += 4) DrawRectangle(x, y + offset, width, height - offset < 4 ? height - offset : 4, MixColors(top, bottom, height > 1 ? (float)offset / (height - 1) : 0));
}
static void DrawGradientHorizontal(int x, int y, int width, int height, Color left, Color right)
{
    for (int offset = 0; offset < width; offset += 4) DrawRectangle(x + offset, y, width - offset < 4 ? width - offset : 4, height, MixColors(left, right, width > 1 ? (float)offset / (width - 1) : 0));
}

/* raylib only fills triangles whose corners go anticlockwise on screen, so fix the order. */
static void DrawTriangleAnyOrder(Vector2 a, Vector2 b, Vector2 c, Color color)
{
    float cross = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    if (cross > 0) DrawTriangle(a, c, b, color);
    else           DrawTriangle(a, b, c, color);
}

static int ItemCount(Rectangle area, float areaPer, int maxItems)
{
    int count = (int)(area.width * area.height / areaPer);
    return count < 6 ? 6 : count > maxItems ? maxItems : count;
}

static void DrawThemeCode(Rectangle area, float seconds, float strength)
{
    static const char glyphs[] = "01{}[]()<>=+-*/;:#&|!?$%^~ifelsfnvarint";
    int glyphCount = (int)sizeof glyphs - 1, columnCount = (int)(area.width / 20) + 1;
    DrawRectangleRec(area, WithAlpha((Color){0, 30, 10, 255}, 0.35f * strength));
    for (int column = 0; column < columnCount; column++) {
        float speed = 50 + HashRandom(column, 1) * 110, length = 6 + HashRandom(column, 2) * 16, period = area.height + length * 20 + 200;
        float head = fmodf(seconds * speed + HashRandom(column, 3) * period, period) - 100;
        for (int i = 0; i < (int)length; i++) {
            float y = head - i * 20;
            if (y < -20 || y > area.height) continue;
            int cell = (int)floorf((y + 10000) / 20);
            int glyph = (int)(HashRandom(column * 977 + cell, (int)(seconds * 1.5f + HashRandom(column, cell) * 20)) * glyphCount) % glyphCount;
            char glyphText[2] = {glyphs[glyph], 0};
            float alpha = (1.0f - (float)i / length) * 0.75f * strength;
            Color color = i == 0 ? (Color){200, 255, 210, 255} : (Color){40, 200, 90, 255};
            DrawText(glyphText, (int)(area.x + column * 20 + 4), (int)(area.y + y), 18, WithAlpha(color, alpha));
        }
    }
}

static void DrawFlower(Vector2 center, float size, float rotation, Color petalColor, float alpha)
{
    for (int i = 0; i < 5; i++) {
        float angle = rotation + i * 72 * DEG2RAD;
        Vector2 petalCenter = {center.x + cosf(angle) * size * 0.85f, center.y + sinf(angle) * size * 0.85f};
        DrawCircleV(petalCenter, size * 0.6f, WithAlpha(petalColor, alpha));
    }
    DrawCircleV(center, size * 0.42f, WithAlpha((Color){255, 214, 102, 255}, alpha * 1.2f));
    DrawCircleV(center, size * 0.2f, WithAlpha((Color){180, 110, 40, 255}, alpha));
}

static void DrawThemeGarden(Rectangle area, float seconds, float strength)
{
    static const Color palette[5] = {{255, 143, 171, 255}, {196, 160, 255, 255}, {255, 190, 140, 255},
                                 {240, 240, 250, 255}, {255, 214, 102, 255}};
    DrawGradientVertical((int)area.x, (int)area.y, (int)area.width, (int)area.height,
                           WithAlpha((Color){40, 20, 50, 255}, 0.3f * strength), WithAlpha((Color){20, 60, 35, 255}, 0.45f * strength));
    int count = ItemCount(area, 9000, 220);
    for (int i = 0; i < count; i++) {
        Vector2 pos = {area.x + HashRandom(i, 1) * area.width, area.y + HashRandom(i, 2) * area.height};
        float size = 5 + HashRandom(i, 3) * 9;
        float sway = sinf(seconds * 0.9f + HashRandom(i, 4) * 6.28f) * 10 * DEG2RAD;
        DrawEllipse((int)(pos.x - size * 1.3f), (int)(pos.y + size * 1.1f), size * 0.9f, size * 0.35f, WithAlpha((Color){70, 160, 90, 255}, 0.5f * strength));
        DrawFlower(pos, size, HashRandom(i, 5) * 6.28f + sway, palette[(int)(HashRandom(i, 6) * 5) % 5], 0.85f * strength);
    }
    for (int i = 0; i < 30; i++) {                     /* petals drifting in the breeze */
        float x = area.x + fmodf(HashRandom(i, 11) * area.width + seconds * (20 + HashRandom(i, 12) * 25), area.width);
        float y = area.y + fmodf(HashRandom(i, 13) * area.height + seconds * (25 + HashRandom(i, 14) * 20), area.height) + sinf(seconds + i) * 6;
        DrawEllipse((int)x, (int)y, 4, 2.5f, WithAlpha(palette[i % 3], 0.6f * strength));
    }
}

static void DrawThemeNight(Rectangle area, float seconds, float strength)
{
    DrawGradientVertical((int)area.x, (int)area.y, (int)area.width, (int)area.height,
                           WithAlpha((Color){10, 20, 60, 255}, 0.75f * strength), WithAlpha((Color){50, 25, 80, 255}, 0.5f * strength));
    int count = ItemCount(area, 2200, 650);
    for (int i = 0; i < count; i++) {
        Vector2 pos = {area.x + HashRandom(i, 1) * area.width, area.y + HashRandom(i, 2) * area.height};
        float twinkle = 0.35f + 0.65f * (0.5f + 0.5f * sinf(seconds * (0.8f + HashRandom(i, 3) * 2.5f) + HashRandom(i, 4) * 6.28f));
        float size = 0.8f + HashRandom(i, 5) * 1.6f;
        Color color = HashRandom(i, 6) < 0.7f ? (Color){255, 255, 255, 255} : HashRandom(i, 6) < 0.85f ? (Color){180, 200, 255, 255}
                                                                                      : (Color){255, 230, 170, 255};
        DrawCircleV(pos, size, WithAlpha(color, twinkle * strength));
        if (size > 2.1f) DrawCircleV(pos, size * 3, WithAlpha(color, twinkle * 0.12f * strength));
    }
    /* crescent moon */
    float moonRadius = area.height < 300 ? 26 : 34;
    Vector2 moonCenter = {area.x + area.width * 0.84f, area.y + (area.height < 300 ? area.height * 0.4f : 90)};
    for (int glowStep = 8; glowStep >= 1; glowStep--)  /* soft glow: faint circles, smaller ones on top */
        DrawCircleV(moonCenter, moonRadius * (1.0f + 0.19f * glowStep), WithAlpha((Color){255, 245, 200, 255}, 0.03f * strength));
    /* the crescent: the moon's disc minus a slightly smaller disc shifted up and right,
       filled in one thin horizontal line at a time */
    {
        Color moonColor = WithAlpha((Color){250, 240, 205, 255}, 0.95f * strength);
        float biteX = moonRadius * 0.45f, biteY = -moonRadius * 0.25f, biteRadius = moonRadius * 0.9f;
        for (float y = -moonRadius; y < moonRadius; y += 1) {
            float rowCenter = y + 0.5f, halfWidth = sqrtf(moonRadius * moonRadius - rowCenter * rowCenter);
            float dy = rowCenter - biteY;
            if (fabsf(dy) >= biteRadius) { DrawRectangleRec((Rectangle){moonCenter.x - halfWidth, moonCenter.y + y, halfWidth * 2, 1}, moonColor); continue; }
            float biteHalfWidth = sqrtf(biteRadius * biteRadius - dy * dy), biteLeft = biteX - biteHalfWidth, biteRight = biteX + biteHalfWidth;
            if (biteLeft > -halfWidth) DrawRectangleRec((Rectangle){moonCenter.x - halfWidth, moonCenter.y + y, (biteLeft < halfWidth ? biteLeft : halfWidth) + halfWidth, 1}, moonColor);
            if (biteRight < halfWidth)  DrawRectangleRec((Rectangle){moonCenter.x + (biteRight > -halfWidth ? biteRight : -halfWidth), moonCenter.y + y, halfWidth - (biteRight > -halfWidth ? biteRight : -halfWidth), 1}, moonColor);
        }
    }
    /* a shooting star every 7 seconds */
    int cycle = (int)(seconds / 7);
    float phase = fmodf(seconds, 7);
    if (phase < 0.9f) {
        Vector2 start = {area.x + (0.3f + HashRandom(cycle, 1) * 0.6f) * area.width, area.y + HashRandom(cycle, 2) * area.height * 0.5f};
        Vector2 direction = {-0.9f, 0.43f};
        Vector2 head = {start.x + direction.x * phase * 520, start.y + direction.y * phase * 520};
        Vector2 tail = {head.x - direction.x * 90, head.y - direction.y * 90};
        DrawLineEx(tail, head, 2, WithAlpha(WHITE, (1 - phase / 0.9f) * strength));
    }
}

static void DrawThemeCircuit(Rectangle area, float seconds, float strength)
{
    Color traceColor = {45, 212, 191, 255};
    DrawRectangleRec(area, WithAlpha((Color){0, 40, 40, 255}, 0.3f * strength));
    int count = ItemCount(area, 11000, 120);
    for (int i = 0; i < count; i++) {
        /* each trace: across, then diagonally, then across again, like on a real board */
        float gridX = area.x + floorf(HashRandom(i, 1) * area.width / 24) * 24, gridY = area.y + floorf(HashRandom(i, 2) * area.height / 24) * 24;
        float dx = HashRandom(i, 3) < 0.5f ? 1 : -1, dy = HashRandom(i, 4) < 0.5f ? 1 : -1;
        float leg1 = 24 + floorf(HashRandom(i, 5) * 5) * 24, leg2 = 12 + floorf(HashRandom(i, 6) * 3) * 12, leg3 = 24 + floorf(HashRandom(i, 7) * 4) * 24;
        Vector2 p0 = {gridX, gridY}, p1 = {gridX + dx * leg1, gridY}, p2 = {p1.x + dx * leg2, p1.y + dy * leg2}, p3 = {p2.x + dx * leg3, p2.y};
        DrawLineEx(p0, p1, 2, WithAlpha(traceColor, 0.35f * strength));
        DrawLineEx(p1, p2, 2, WithAlpha(traceColor, 0.35f * strength));
        DrawLineEx(p2, p3, 2, WithAlpha(traceColor, 0.35f * strength));
        DrawCircleV(p0, 4, WithAlpha(traceColor, 0.45f * strength));
        DrawCircleV(p0, 2, WithAlpha((Color){13, 17, 23, 255}, strength));
        DrawCircleV(p3, 4, WithAlpha(traceColor, 0.45f * strength));
        /* a pulse of light running along the trace */
        float diagonalLength = leg2 * 1.41421f, traceLength = leg1 + diagonalLength + leg3;
        float along = fmodf(seconds * (60 + HashRandom(i, 8) * 60) + HashRandom(i, 9) * 600, traceLength + 260);
        if (along < traceLength) {
            Vector2 pulse;
            if (along < leg1)            pulse = (Vector2){p0.x + dx * along, p0.y};
            else if (along < leg1 + diagonalLength) { float diagonalStep = (along - leg1) / 1.41421f; pulse = (Vector2){p1.x + dx * diagonalStep, p1.y + dy * diagonalStep}; }
            else                   pulse = (Vector2){p2.x + dx * (along - leg1 - diagonalLength), p2.y};
            DrawCircleV(pulse, 6, WithAlpha(traceColor, 0.25f * strength));
            DrawCircleV(pulse, 2.5f, WithAlpha((Color){200, 255, 245, 255}, 0.9f * strength));
        }
    }
    int chipCount = count / 12 + 1;
    for (int i = 0; i < chipCount; i++) {
        Rectangle chip = {area.x + HashRandom(i, 21) * (area.width - 60), area.y + HashRandom(i, 22) * (area.height - 40), 44 + HashRandom(i, 23) * 30, 26 + HashRandom(i, 24) * 14};
        DrawRectangleRec(chip, WithAlpha((Color){10, 30, 32, 255}, 0.9f * strength));
        DrawRectangleLinesEx(chip, 1.5f, WithAlpha(traceColor, 0.5f * strength));
        for (float pinX = chip.x + 6; pinX < chip.x + chip.width - 4; pinX += 8) {
            DrawLineEx((Vector2){pinX, chip.y}, (Vector2){pinX, chip.y - 5}, 2, WithAlpha(traceColor, 0.4f * strength));
            DrawLineEx((Vector2){pinX, chip.y + chip.height}, (Vector2){pinX, chip.y + chip.height + 5}, 2, WithAlpha(traceColor, 0.4f * strength));
        }
    }
}

static void DrawThemeOcean(Rectangle area, float seconds, float strength)
{
    DrawGradientVertical((int)area.x, (int)area.y, (int)area.width, (int)area.height,
                           WithAlpha((Color){10, 40, 80, 255}, 0.45f * strength), WithAlpha((Color){5, 25, 60, 255}, 0.6f * strength));
    static const Color waveColors[4] = {{90, 180, 235, 255}, {60, 140, 215, 255}, {35, 100, 185, 255}, {20, 65, 150, 255}};
    float bottom = area.y + area.height;
    for (int layer = 0; layer < 4; layer++) {
        float base = area.y + area.height * (0.5f + layer * 0.12f), amplitude = 7 + layer * 3, wavelength = 190 - layer * 25, speed = 0.6f + layer * 0.25f;
        for (float x = area.x; x < area.x + area.width; x += 4) {
            float y = base + sinf((x / wavelength) * 6.28f + seconds * speed + layer * 1.7f) * amplitude + sinf((x / (wavelength * 0.37f)) + seconds * speed * 1.6f) * amplitude * 0.3f;
            DrawRectangle((int)x, (int)y, 4, (int)(bottom - y) + 1, WithAlpha(waveColors[layer], 0.32f * strength));
        }
    }
    for (int i = 0; i < 40; i++) {
        float x = area.x + HashRandom(i, 1) * area.width + sinf(seconds * 1.3f + i) * 6;
        float y = bottom - fmodf(seconds * (18 + HashRandom(i, 2) * 30) + HashRandom(i, 3) * area.height, area.height * 0.6f);
        DrawCircleLines((int)x, (int)y, 2 + HashRandom(i, 4) * 4, WithAlpha((Color){190, 230, 255, 255}, 0.45f * strength));
    }
}

static void DrawThemeAutumn(Rectangle area, float seconds, float strength)
{
    static const Color palette[4] = {{230, 126, 34, 255}, {192, 57, 43, 255}, {241, 196, 15, 255}, {160, 82, 45, 255}};
    DrawGradientVertical((int)area.x, (int)area.y, (int)area.width, (int)area.height,
                           WithAlpha((Color){60, 30, 10, 255}, 0.25f * strength), WithAlpha((Color){70, 35, 15, 255}, 0.45f * strength));
    int count = ItemCount(area, 13000, 150);
    for (int i = 0; i < count; i++) {
        float fallSpeed = 22 + HashRandom(i, 1) * 35;
        float y = area.y + fmodf(HashRandom(i, 2) * (area.height + 40) + seconds * fallSpeed, area.height + 40) - 20;
        float x = area.x + HashRandom(i, 3) * area.width + sinf(seconds * (0.5f + HashRandom(i, 4)) + HashRandom(i, 5) * 6.28f) * 30;
        float size = 7 + HashRandom(i, 6) * 8, angle = seconds * (HashRandom(i, 7) * 2 - 1) * 1.4f + HashRandom(i, 8) * 6.28f;
        Vector2 direction = {cosf(angle), sinf(angle)}, across = {-direction.y, direction.x}, center = {x, y};
        Vector2 tip  = {center.x + direction.x * size,  center.y + direction.y * size},  back = {center.x - direction.x * size,  center.y - direction.y * size};
        Vector2 left = {center.x + across.x * size * 0.5f, center.y + across.y * size * 0.5f}, right = {center.x - across.x * size * 0.5f, center.y - across.y * size * 0.5f};
        Color color = WithAlpha(palette[(int)(HashRandom(i, 9) * 4) % 4], 0.7f * strength);
        DrawTriangleAnyOrder(tip, left, back, color);
        DrawTriangleAnyOrder(tip, back, right, color);
        DrawLineEx((Vector2){back.x - direction.x * 4, back.y - direction.y * 4}, tip, 1, WithAlpha((Color){90, 40, 10, 255}, 0.6f * strength));
    }
}

static void DrawThemeSnow(Rectangle area, float seconds, float strength)
{
    DrawGradientVertical((int)area.x, (int)area.y, (int)area.width, (int)area.height,
                           WithAlpha((Color){15, 25, 50, 255}, 0.55f * strength), WithAlpha((Color){45, 60, 90, 255}, 0.5f * strength));
    /* a row of pine trees along the bottom, with snow on the ground in front */
    bool small = area.height < 300;
    float ground = area.y + area.height - (small ? 10 : 26);
    int treeCount = (int)(area.width / 46) + 2;
    Color pine = WithAlpha((Color){18, 40, 45, 255}, 0.9f * strength), snowOnPine = WithAlpha((Color){220, 235, 255, 255}, 0.3f * strength);
    for (int i = 0; i < treeCount; i++) {
        float x = area.x + i * 46 - 20 + HashRandom(i, 31) * 20, height = (small ? 30 : 60) + HashRandom(i, 32) * (small ? 25 : 70);
        for (int tier = 0; tier < 3; tier++) {               /* three stacked triangles, smaller toward the top */
            float tierBottom = ground - height * tier * 0.28f, halfWidth = height * (0.32f - tier * 0.07f), tip = tierBottom - height * 0.45f;
            DrawTriangleAnyOrder((Vector2){x, tip}, (Vector2){x - halfWidth, tierBottom}, (Vector2){x + halfWidth, tierBottom}, pine);
            DrawTriangleAnyOrder((Vector2){x, tip}, (Vector2){x - halfWidth * 0.45f, tip + height * 0.2f}, (Vector2){x + halfWidth * 0.45f, tip + height * 0.2f}, snowOnPine);
        }
    }
    DrawRectangleRec((Rectangle){area.x, ground, area.width, area.y + area.height - ground}, WithAlpha((Color){215, 228, 245, 255}, 0.45f * strength));
    /* snowflakes: small soft dots falling and swaying, and now and then a bigger six-armed one */
    int count = ItemCount(area, 3200, 420);
    for (int i = 0; i < count; i++) {
        float fallSpeed = 18 + HashRandom(i, 1) * 40, size = 1 + HashRandom(i, 2) * 2.4f;
        float y = area.y + fmodf(HashRandom(i, 3) * (area.height + 20) + seconds * fallSpeed, area.height + 20) - 10;
        float x = area.x + fmodf(HashRandom(i, 4) * area.width + sinf(seconds * (0.4f + HashRandom(i, 5)) + HashRandom(i, 6) * 6.28f) * 18 + area.width, area.width);
        if (i % 23 == 0) {
            float spin = seconds * (HashRandom(i, 7) - 0.5f) + HashRandom(i, 8) * 6.28f, arm = 4 + HashRandom(i, 9) * 3;
            for (int k = 0; k < 3; k++) {
                float angle = spin + k * 60 * DEG2RAD;
                DrawLineEx((Vector2){x - cosf(angle) * arm, y - sinf(angle) * arm}, (Vector2){x + cosf(angle) * arm, y + sinf(angle) * arm}, 1.2f, WithAlpha(WHITE, 0.75f * strength));
            }
        } else DrawCircleV((Vector2){x, y}, size, WithAlpha(WHITE, (0.45f + 0.4f * HashRandom(i, 10)) * strength));
    }
}

static void DrawThemeAurora(Rectangle area, float seconds, float strength)
{
    DrawGradientVertical((int)area.x, (int)area.y, (int)area.width, (int)area.height,
                           WithAlpha((Color){4, 12, 28, 255}, 0.75f * strength), WithAlpha((Color){8, 30, 40, 255}, 0.55f * strength));
    int starCount = ItemCount(area, 9000, 160);
    for (int i = 0; i < starCount; i++) {
        float twinkle = 0.4f + 0.6f * (0.5f + 0.5f * sinf(seconds * (0.7f + HashRandom(i, 3) * 2) + HashRandom(i, 4) * 6.28f));
        DrawCircleV((Vector2){area.x + HashRandom(i, 1) * area.width, area.y + HashRandom(i, 2) * area.height * 0.7f}, 0.8f + HashRandom(i, 5),
                    WithAlpha(WHITE, 0.7f * twinkle * strength));
    }
    /* the lights: three curtains, each a row of thin strips whose height and brightness ripple */
    static const Color curtainColors[3] = {{70, 255, 170, 255}, {60, 190, 255, 255}, {190, 110, 255, 255}};
    for (int curtain = 0; curtain < 3; curtain++) {
        float baseY = area.y + area.height * (0.2f + curtain * 0.09f);
        for (float x = area.x; x < area.x + area.width; x += 5) {
            float wave = sinf(x / 230 + seconds * 0.35f + curtain * 1.9f) * 0.6f + sinf(x / 95 - seconds * 0.6f + curtain) * 0.4f;
            float top = baseY + wave * area.height * 0.07f;
            float height = area.height * (0.12f + 0.08f * (0.5f + 0.5f * sinf(x / 140 + seconds * 0.5f + curtain * 2.3f)));
            float glow = 0.5f + 0.5f * sinf(x / 60 + seconds * 1.3f + curtain * 4.1f);
            Color color = WithAlpha(curtainColors[curtain], (0.10f + 0.16f * glow) * strength);
            int stripTop = (int)top, stripHeight = (int)height;   /* whole pixels, so the two halves meet without a seam */
            DrawRectangleGradientV((int)x, stripTop, 5, stripHeight, WithAlpha(color, 0.1f), color);                     /* faint at the top...       */
            DrawRectangleGradientV((int)x, stripTop + stripHeight, 5, (int)(height * 0.6f), color, WithAlpha(color, 0));  /* ...brightest, then fading */
        }
    }
    /* dark hills along the bottom */
    float bottom = area.y + area.height;
    for (float x = area.x; x < area.x + area.width; x += 4) {
        float hill = area.y + area.height * (0.86f + 0.05f * sinf(x / 170) + 0.03f * sinf(x / 61 + 1.3f));
        DrawRectangle((int)x, (int)hill, 4, (int)(bottom - hill) + 1, WithAlpha((Color){5, 14, 20, 255}, 0.95f * strength));
    }
}

static void DrawThemeSynthwave(Rectangle area, float seconds, float strength)
{
    float horizon = area.y + area.height * 0.58f, bottom = area.y + area.height, centerX = area.x + area.width / 2;
    DrawGradientVertical((int)area.x, (int)area.y, (int)area.width, (int)(horizon - area.y),
                           WithAlpha((Color){25, 8, 50, 255}, 0.7f * strength), WithAlpha((Color){110, 25, 95, 255}, 0.6f * strength));
    DrawRectangleRec((Rectangle){area.x, horizon, area.width, bottom - horizon}, WithAlpha((Color){18, 4, 32, 255}, 0.8f * strength));
    int starCount = ItemCount(area, 12000, 90);
    for (int i = 0; i < starCount; i++)
        DrawCircleV((Vector2){area.x + HashRandom(i, 1) * area.width, area.y + HashRandom(i, 2) * (horizon - area.y) * 0.6f}, 1,
                    WithAlpha(WHITE, (0.3f + 0.4f * HashRandom(i, 3)) * strength));
    /* the sun: yellow fading to pink, sliced by gaps that drift down and widen toward the horizon */
    float sunRadius = area.height * 0.24f < 150 ? area.height * 0.24f : 150, sunY = horizon - sunRadius * 0.35f;
    float gapSpacing = sunRadius * 0.13f, gapsFrom = -sunRadius * 0.3f;
    DrawCircleV((Vector2){centerX, sunY}, sunRadius * 1.25f, WithAlpha((Color){255, 80, 160, 255}, 0.08f * strength));
    for (float row = -sunRadius; row < sunRadius && sunY + row < horizon; row += 2) {
        float gapShare = (row - gapsFrom) / (sunRadius * 0.35f - gapsFrom) * 0.6f;    /* 0 where the gaps start, 0.6 at the horizon */
        if (row > gapsFrom && fmodf(row - seconds * 6 + gapSpacing * 100, gapSpacing) < gapSpacing * gapShare) continue;   /* a gap */
        float halfWidth = sqrtf(sunRadius * sunRadius - row * row), below = (row + sunRadius) / (2 * sunRadius);
        DrawRectangleRec((Rectangle){centerX - halfWidth, sunY + row, halfWidth * 2, 2},
                         WithAlpha(MixColors((Color){255, 220, 90, 255}, (Color){255, 60, 150, 255}, below), 0.9f * strength));
    }
    /* the grid: lines running away to the horizon, and cross lines rolling toward you */
    Color gridColor = {255, 70, 200, 255};
    for (int line = -14; line <= 14; line++) {
        float spread = area.width * 0.11f * line;
        DrawLineEx((Vector2){centerX + spread * 0.08f, horizon}, (Vector2){centerX + spread * 1.6f, bottom}, 1.5f, WithAlpha(gridColor, 0.55f * strength));
    }
    float roll = fmodf(seconds * 0.9f, 1);
    for (int line = 0; line < 14; line++) {
        float depth = (line + 1 - roll) / 14;                /* 0 = at the horizon, 1 = at your feet */
        float y = horizon + (bottom - horizon) * depth * depth;
        DrawLineEx((Vector2){area.x, y}, (Vector2){area.x + area.width, y}, 1 + depth, WithAlpha(gridColor, (0.15f + 0.5f * depth) * strength));
    }
    DrawLineEx((Vector2){area.x, horizon}, (Vector2){area.x + area.width, horizon}, 2, WithAlpha((Color){255, 150, 230, 255}, 0.8f * strength));
}

static void DrawThemeFireflies(Rectangle area, float seconds, float strength)
{
    DrawGradientVertical((int)area.x, (int)area.y, (int)area.width, (int)area.height,
                           WithAlpha((Color){6, 14, 22, 255}, 0.65f * strength), WithAlpha((Color){12, 32, 20, 255}, 0.6f * strength));
    /* tall grass along the bottom, swaying */
    float bottom = area.y + area.height, grassHeight = area.height < 300 ? 26 : 70;
    for (float x = area.x; x < area.x + area.width; x += 5) {
        int blade = (int)((x - area.x) / 5);
        float height = grassHeight * (0.5f + HashRandom(blade, 41) * 0.8f);
        float sway = sinf(seconds * 1.1f + x / 70 + HashRandom(blade, 42) * 2) * height * 0.18f;
        DrawLineEx((Vector2){x, bottom}, (Vector2){x + sway, bottom - height}, 2, WithAlpha((Color){14, 45, 25, 255}, 0.95f * strength));
    }
    /* fireflies: wandering slowly, each glowing and fading on its own rhythm */
    int count = ItemCount(area, 14000, 110);
    for (int i = 0; i < count; i++) {
        float x = area.x + (HashRandom(i, 1) + 0.06f * sinf(seconds * (0.15f + HashRandom(i, 3) * 0.2f) + HashRandom(i, 4) * 6.28f)) * area.width;
        float y = area.y + (0.15f + HashRandom(i, 2) * 0.8f + 0.05f * cosf(seconds * (0.12f + HashRandom(i, 5) * 0.2f) + HashRandom(i, 6) * 6.28f)) * area.height;
        float pulse = 0.5f + 0.5f * sinf(seconds * (0.8f + HashRandom(i, 7) * 1.4f) + HashRandom(i, 8) * 6.28f);
        pulse = pulse * pulse * pulse;                       /* mostly dim, now and then bright */
        if (pulse < 0.02f) continue;
        DrawCircleV((Vector2){x, y}, 12, WithAlpha((Color){250, 215, 90, 255}, 0.08f * pulse * strength));
        DrawCircleV((Vector2){x, y}, 5, WithAlpha((Color){250, 225, 110, 255}, 0.25f * pulse * strength));
        DrawCircleV((Vector2){x, y}, 2, WithAlpha((Color){255, 250, 200, 255}, 0.95f * pulse * strength));
    }
}

static void DrawThemeRain(Rectangle area, float seconds, float strength)
{
    DrawGradientVertical((int)area.x, (int)area.y, (int)area.width, (int)area.height,
                           WithAlpha((Color){14, 20, 32, 255}, 0.7f * strength), WithAlpha((Color){30, 36, 52, 255}, 0.6f * strength));
    /* now and then, far-off lightning brightens the sky for a moment */
    float stormPhase = fmodf(seconds, 13);
    if (stormPhase < 0.35f) {
        float flash = stormPhase < 0.08f ? stormPhase / 0.08f : stormPhase < 0.14f ? 0.4f : (0.35f - stormPhase) / 0.21f;
        DrawRectangleRec(area, WithAlpha((Color){180, 200, 255, 255}, 0.12f * flash * strength));
    }
    /* the skyline: buildings with a few lit windows (now and then one goes on or off) */
    float bottom = area.y + area.height, x = area.x - 10;
    for (int building = 0; x < area.x + area.width; building++) {
        float width = 40 + HashRandom(building, 51) * 70, height = area.height * (0.15f + HashRandom(building, 52) * 0.3f);
        Rectangle block = {x, bottom - height, width, height};
        DrawRectangleRec(block, WithAlpha((Color){12, 16, 26, 255}, 0.95f * strength));
        for (float windowY = block.y + 8; windowY < bottom - 10; windowY += 14)
            for (float windowX = block.x + 6; windowX < block.x + width - 10; windowX += 12) {
                int window = building * 1000 + (int)((windowY - block.y) / 14) * 37 + (int)((windowX - block.x) / 12);
                bool lit = HashRandom(window, 53) < 0.28f;
                if (HashRandom(window, 54) < 0.03f) lit = fmodf(seconds + HashRandom(window, 55) * 30, 30) < 15;   /* someone comes home, then goes to bed */
                if (lit) DrawRectangleRec((Rectangle){windowX, windowY, 5, 7}, WithAlpha((Color){255, 210, 120, 255}, 0.55f * strength));
            }
        x += width + 4;
    }
    /* the rain: thin slanted streaks falling fast */
    int count = ItemCount(area, 2400, 520);
    for (int i = 0; i < count; i++) {
        float speed = 520 + HashRandom(i, 1) * 360, length = 10 + HashRandom(i, 2) * 14;
        float y = area.y + fmodf(HashRandom(i, 3) * (area.height + 40) + seconds * speed, area.height + 40) - 20;
        float dropX = area.x + fmodf(HashRandom(i, 4) * area.width - (y - area.y) * 0.18f + area.width * 2, area.width);
        DrawLineEx((Vector2){dropX, y}, (Vector2){dropX - length * 0.18f, y - length}, 1, WithAlpha((Color){170, 195, 235, 255}, (0.25f + 0.25f * HashRandom(i, 5)) * strength));
    }
}

/* Draws a theme filling the rectangle area, but only the part inside clip.
   strength 0..1: low for behind the app, 1 for banners and previews. */
void DrawThemeClip(int theme, Rectangle area, Rectangle clip, float strength)
{
    float seconds = (float)GetTime();
    if (clip.width <= 0 || clip.height <= 0) return;
    BeginScissorMode((int)clip.x, (int)clip.y, (int)clip.width, (int)clip.height);
    switch (theme) {
    case THEME_CODE:    DrawThemeCode(area, seconds, strength);    break;
    case THEME_GARDEN:  DrawThemeGarden(area, seconds, strength);  break;
    case THEME_NIGHT:   DrawThemeNight(area, seconds, strength);   break;
    case THEME_CIRCUIT: DrawThemeCircuit(area, seconds, strength); break;
    case THEME_OCEAN:   DrawThemeOcean(area, seconds, strength);   break;
    case THEME_AUTUMN:  DrawThemeAutumn(area, seconds, strength);  break;
    case THEME_SNOW:      DrawThemeSnow(area, seconds, strength);      break;
    case THEME_AURORA:    DrawThemeAurora(area, seconds, strength);    break;
    case THEME_SYNTHWAVE: DrawThemeSynthwave(area, seconds, strength); break;
    case THEME_FIREFLIES: DrawThemeFireflies(area, seconds, strength); break;
    case THEME_RAIN:      DrawThemeRain(area, seconds, strength);      break;
    default:            /* Classic: a quiet banner, nothing behind the app */
        if (strength >= 0.99f) {
            DrawGradientHorizontal((int)area.x, (int)area.y, (int)area.width, (int)area.height, (Color){22, 27, 34, 255}, (Color){33, 38, 45, 255});
            for (float x = area.x + 12; x < area.x + area.width; x += 24)
                for (float y = area.y + 12; y < area.y + area.height; y += 24) DrawCircleV((Vector2){x, y}, 1.2f, (Color){48, 54, 61, 255});
        }
        break;
    }
    EndScissorMode();
}

/* Draws a theme filling a rectangle (strength 0..1: faint behind the app, full for previews). */
void DrawTheme(int theme, Rectangle area, float strength) { DrawThemeClip(theme, area, area, strength); }
