/*
 * header_bar.h - the bar across the top of every screen
 *
 * The logo, the Back button, your name and picture, and Sign out.
 */

#ifndef MINIGITHUB_HEADER_BAR_H
#define MINIGITHUB_HEADER_BAR_H

#include "common.h"

/* The logo - a circle with git's branch mark cut out of it - centred on (centerX, centerY), at
   scale times its normal size (a radius of 18), in the colours given. */
void DrawLogoScaled(float centerX, float centerY, float scale, Color body, Color cutout);

/* Draws the bar across the top of the window and handles its buttons. */
void DrawHeader(void);

#endif
