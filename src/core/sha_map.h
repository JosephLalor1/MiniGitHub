/*
 * sha_map.h - a hash table from a SHA-1 fingerprint to a number
 *
 * Finds a blob or commit by its fingerprint. Open addressing, kept at most
 * half full.
 */

#ifndef MINIGITHUB_SHA_MAP_H
#define MINIGITHUB_SHA_MAP_H

#include "common.h"

typedef struct { uint8_t sha[20]; int index; } ShaMapSlot;
typedef struct { ShaMapSlot *slots; int capacity, used; } ShaMap;

/* The number stored for this fingerprint, or -1. */
int ShaMapGet(const ShaMap *map, const uint8_t *sha);

/* Stores a number for a fingerprint (replacing any number it had), growing the table as needed. */
void ShaMapPut(ShaMap *map, const uint8_t *sha, int index);

/* Frees the table and empties it. */
void ShaMapFree(ShaMap *map);

#endif
