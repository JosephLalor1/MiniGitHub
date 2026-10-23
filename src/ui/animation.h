/*
 * animation.h - values that ease toward where they should be
 *
 * The interface is drawn from scratch every frame, so nothing on screen
 * remembers how it looked a moment ago. These keep a number per "key" (a
 * button's rectangle, a pie slice, a menu...) and move it a little toward
 * its target each frame, so hovers, menus and the like glide instead of
 * snapping.
 */

#ifndef MINIGITHUB_ANIMATION_H
#define MINIGITHUB_ANIMATION_H

#include "common.h"

/* A key for something animated, made from a short tag and two numbers (an index, a position...). */
unsigned AnimationKey(const char *tag, float first, float second);

/* A key for something animated that lives at this rectangle (a button, a card...). */
unsigned RectKey(Rectangle rect);

/* The value kept for key, moved toward target; speed is how quickly (about 1 / seconds
   to get most of the way). Something new, or not drawn for a moment, starts from 0. */
float Animate(unsigned key, float target, float speed);

/* 0..1: how far a hover effect has faded in (toward 1 while hovered, back to 0 after). */
float HoverAmount(unsigned key, bool hovered);

/* Smoothstep easing: 0..1 in, 0..1 out, gentle at both ends. */
float EaseInOut(float amount);

#endif
