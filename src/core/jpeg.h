/*
 * jpeg.h - a small JPEG decoder
 *
 * raylib's standard build only reads PNG, so this reads "baseline" JPEG
 * photos - the kind cameras and phones save - including the EXIF note
 * saying which way up they are.
 */

#ifndef MINIGITHUB_JPEG_H
#define MINIGITHUB_JPEG_H

#include "common.h"

/* Decodes a baseline JPG into an RGB image, turned the right way up. On failure, returns an empty
   image and points *error at the reason. */
Image DecodeJpg(const uint8_t *data, int size, const char **error);

#endif
