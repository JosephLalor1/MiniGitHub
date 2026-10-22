/*
 * git_receive.h - taking in a push
 *
 * Finds the new commits in a pack, checks they build on the branch,
 * unfolds their folders and stores everything.
 */

#ifndef MINIGITHUB_GIT_RECEIVE_H
#define MINIGITHUB_GIT_RECEIVE_H

#include "common.h"
#include "git_pack_read.h"
#include "model.h"

/* Something the repository already has, for a delta built on it ("thin" packs). */
bool FindRepoObject(Repo *repo, const uint8_t *sha, const uint8_t **data, size_t *length, int *type);

/* Updates repo's branch from oldSha to newSha using the objects in pack. */
const char *ApplyPush(Repo *repo, int pusherId, const uint8_t oldSha[20], const uint8_t newSha[20], const char *refName, PackContents *pack, int *addedCount);

#endif
