/*
 * sha_map.c - a hash table from a SHA-1 fingerprint to a number
 *
 * Finds a blob or commit by its fingerprint. Open addressing, kept at most
 * half full.
 */

#include "sha_map.h"

/* ---------- looking things up by fingerprint ----------
   A hash table from a SHA-1 to a number (which blob, which commit...). */

static uint32_t ShaMapHash(const uint8_t *sha) { return (uint32_t)sha[0] | (uint32_t)sha[1] << 8 | (uint32_t)sha[2] << 16 | (uint32_t)sha[3] << 24; }
/* The number stored for this fingerprint, or -1. */
int ShaMapGet(const ShaMap *map, const uint8_t *sha)
{
    if (!map->capacity) return -1;
    for (uint32_t slot = ShaMapHash(sha) & (uint32_t)(map->capacity - 1);; slot = (slot + 1) & (uint32_t)(map->capacity - 1)) {
        if (map->slots[slot].index < 0) return -1;
        if (memcmp(map->slots[slot].sha, sha, 20) == 0) return map->slots[slot].index;
    }
}
/* Stores a number for a fingerprint (replacing any number it had), growing the table as needed. */
void ShaMapPut(ShaMap *map, const uint8_t *sha, int index)
{
    if ((map->used + 1) * 2 > map->capacity) {             /* keep it at most half full */
        ShaMap bigger = {0};
        bigger.capacity = map->capacity ? map->capacity * 2 : 64;
        bigger.slots = malloc((size_t)bigger.capacity * sizeof(ShaMapSlot));
        if (!bigger.slots) return;
        for (int slot = 0; slot < bigger.capacity; slot++) bigger.slots[slot].index = -1;
        for (int slot = 0; slot < map->capacity; slot++) if (map->slots[slot].index >= 0) ShaMapPut(&bigger, map->slots[slot].sha, map->slots[slot].index);
        free(map->slots);
        *map = bigger;
    }
    for (uint32_t slot = ShaMapHash(sha) & (uint32_t)(map->capacity - 1);; slot = (slot + 1) & (uint32_t)(map->capacity - 1)) {
        if (map->slots[slot].index < 0) { memcpy(map->slots[slot].sha, sha, 20); map->slots[slot].index = index; map->used++; return; }
        if (memcmp(map->slots[slot].sha, sha, 20) == 0) { map->slots[slot].index = index; return; }
    }
}
/* Frees the table and empties it. */
void ShaMapFree(ShaMap *map) { free(map->slots); memset(map, 0, sizeof *map); }
