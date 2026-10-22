/*
 * themes.h - animated backgrounds
 *
 * The themes people can choose (code rain, flower garden, night sky...),
 * drawn with simple shapes every frame.
 */

#ifndef MINIGITHUB_THEMES_H
#define MINIGITHUB_THEMES_H

#include "common.h"

enum { THEME_CLASSIC, THEME_CODE, THEME_GARDEN, THEME_NIGHT, THEME_CIRCUIT, THEME_OCEAN, THEME_AUTUMN, THEME_COUNT };
typedef struct { const char *name, *blurb; Color accent; } ThemeInfo;
extern const ThemeInfo THEMES[THEME_COUNT];

/* The same colour, with its transparency multiplied by alpha (0..1). */
Color WithAlpha(Color color, float alpha);

/* Draws a theme filling the rectangle area, but only the part inside clip. strength 0..1: low for
   behind the app, 1 for banners and previews. */
void DrawThemeClip(int theme, Rectangle area, Rectangle clip, float strength);

/* Draws a theme filling a rectangle (strength 0..1: faint behind the app, full for previews). */
void DrawTheme(int theme, Rectangle area, float strength);

#endif
