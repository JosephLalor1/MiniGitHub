/*
 * util.h - small helpers used everywhere
 *
 * Hashing short strings (FNV-1a), growing arrays, case-insensitive
 * comparisons, tidying names and paths that people type, and "3 hours ago"
 * times.
 */

#ifndef MINIGITHUB_UTIL_H
#define MINIGITHUB_UTIL_H

#include "common.h"

#define FNV_OFFSET_BASIS 2166136261u
/* FNV-1a hash of length bytes, continuing from hash (start with FNV_OFFSET_BASIS). */
uint32_t HashFnv1a(const char *text, int length, uint32_t hash);

/* An array that grows as needed (doubling). Returns the new pointer. */
void *GrowArray(void *array, int *capacity, int needed, size_t itemSize);

/* Is needle somewhere in haystack, ignoring capitals? An empty needle is in everything. */
bool ContainsIgnoreCase(const char *haystack, const char *needle);

/* A file path someone typed: like SanitizeName, but "/" makes folders ("docs/notes.txt"). Doubled
   or stray slashes are dropped. */
void SanitizePath(const char *input, char *output);

/* Keep letters, digits, - _ . ; turn spaces into dashes. */
void SanitizeName(const char *input, char *output);

/* "just now", "5 minutes ago", "3 days ago" or "on 13 Oct 2026" for a moment in the past. */
const char *TimeAgo(long long when);

/* Are the first length characters of a and b the same, ignoring capitals? */
bool SameTextIgnoreCase(const char *a, const char *b, int length);

/* Are two strings the same, ignoring capitals? */
bool EqualsIgnoreCase(const char *a, const char *b);

#endif
