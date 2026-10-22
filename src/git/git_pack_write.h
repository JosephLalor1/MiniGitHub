/*
 * git_pack_write.h - making the packs git receives
 *
 * For clone, fetch and pull: every commit, folder and file the other side
 * is missing, with new versions of files sent as small deltas of older
 * ones where that helps.
 */

#ifndef MINIGITHUB_GIT_PACK_WRITE_H
#define MINIGITHUB_GIT_PACK_WRITE_H

#include "common.h"
#include "byte_buffer.h"
#include "model.h"

/* The pack for "I want these commits; I already have those". */
bool BuildPack(Repo *repo, const int *wants, int wantCount, const int *haves, int haveCount, ByteBuffer *out);

#endif
