/*
 * tech_stack.h - language bars and the tech stack pie
 *
 * The language bar on a repository, and the "Tech stack" card on a
 * profile: lines changed per language, counted a little each frame so the
 * window stays responsive.
 */

#ifndef MINIGITHUB_TECH_STACK_H
#define MINIGITHUB_TECH_STACK_H

#include "common.h"

/* The colour a language has in the breakdown just built (grey if it's in "Other" or not code). */
Color LangColor(int lang);

/* A thin bar split into each language's share, with a key underneath. Hovering part of the bar
   shows its numbers. */
void DrawLanguageBar(Rectangle area);

/* One slice of a pie, pushed out from the centre by lift (0..1: how far it has risen on hover)
   and dimmed by fade (0..1) while another slice is the one hovered. separated: there are
   other slices, so leave a thin gap between them. */
void DrawPieSlice(Vector2 center, float radius, float startAngle, float sweep, Color color, float lift, float fade, bool separated);

/* The "Tech stack" card on a profile: a pie and its key (or, in a narrow window, a bar). */
void DrawTechStack(Rectangle area, int userId, bool asBar);

#endif
