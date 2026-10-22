/*
 * git_pack_read.c - reading the packs git sends
 *
 * When you push, git sends a pack: every new object squeezed with zlib,
 * some as deltas of others. This unpacks them all and turns every delta
 * back into the object it describes.
 */

#include "git_pack_read.h"
#include "compression.h"
#include "git_receive.h"
#include "model.h"
#include "sha1.h"
#include "sha_map.h"
#include "util.h"

static const char *ObjectKindName(int type) { return type == OBJ_COMMIT ? "commit" : type == OBJ_TREE ? "tree" : type == OBJ_BLOB ? "blob" : "tag"; }

#define MAX_OBJECT (256u * 1024 * 1024)       /* biggest single object in a pack       */

/* ---------- reading a pack ---------- */

/* Frees every object of an unpacked pack. */
void PackFree(PackContents *pack)
{
    for (int index = 0; index < pack->count; index++) free(pack->objects[index].data);
    free(pack->objects);
    ShaMapFree(&pack->map);
    memset(pack, 0, sizeof *pack);
}

/* "take bits of base, add new bytes": builds the new object from a delta. */
static uint8_t *ApplyDelta(const uint8_t *base, size_t baseLength, const uint8_t *delta, size_t deltaLength, size_t *outLength)
{
    size_t pos = 0;
    uint64_t sizes[2] = {0, 0};
    for (int which = 0; which < 2; which++) {
        int shift = 0;
        uint8_t byte;
        do { if (pos >= deltaLength || shift > 56) return NULL; byte = delta[pos++]; sizes[which] |= (uint64_t)(byte & 0x7F) << shift; shift += 7; } while (byte & 0x80);
    }
    if (sizes[0] != baseLength || sizes[1] > MAX_OBJECT) return NULL;
    uint8_t *out = malloc((size_t)sizes[1] + 1);
    if (!out) return NULL;
    size_t written = 0;
    while (pos < deltaLength) {
        uint8_t byte = delta[pos++];
        if (byte & 0x80) {                                /* copy from the base */
            size_t copyOffset = 0, copyLength = 0;
            for (int which = 0; which < 4; which++) if (byte & (1 << which)) { if (pos >= deltaLength) goto bad; copyOffset |= (size_t)delta[pos++] << (8 * which); }
            for (int which = 0; which < 3; which++) if (byte & (16 << which)) { if (pos >= deltaLength) goto bad; copyLength |= (size_t)delta[pos++] << (8 * which); }
            if (!copyLength) copyLength = 0x10000;
            if (copyOffset + copyLength > baseLength || written + copyLength > sizes[1]) goto bad;
            memcpy(out + written, base + copyOffset, copyLength);
            written += copyLength;
        } else if (byte) {                                /* new bytes */
            if (pos + byte > deltaLength || written + byte > sizes[1]) goto bad;
            memcpy(out + written, delta + pos, byte);
            pos += byte; written += byte;
        } else goto bad;
    }
    if (written != sizes[1]) goto bad;
    out[written] = 0;
    *outLength = written;
    return out;
bad:
    free(out);
    return NULL;
}

static int PackIndexAt(PackContents *pack, size_t offset)
{
    int low = 0, high = pack->count - 1;
    while (low <= high) {
        int mid = (low + high) / 2;
        if (pack->objects[mid].offset == offset) return mid;
        if (pack->objects[mid].offset < offset) low = mid + 1; else high = mid - 1;
    }
    return -1;
}

/* Turns a delta into the real object (its base first, if that's a delta too). */
static bool ResolveDelta(PackContents *pack, int index, Repo *repo)
{
    PackEntry *object = &pack->objects[index];
    if (!object->isDelta || object->resolved) return true;
    if (object->resolving) return false;                  /* a loop: damaged pack */
    object->resolving = true;
    const uint8_t *base = NULL;
    size_t baseLength = 0;
    int baseType = 0;
    if (object->type == OBJ_OFS_DELTA) {
        int baseIndex = PackIndexAt(pack, object->baseOffset);
        if (baseIndex < 0 || !ResolveDelta(pack, baseIndex, repo)) return false;
        base = pack->objects[baseIndex].data; baseLength = pack->objects[baseIndex].length; baseType = pack->objects[baseIndex].type;
    } else {
        int baseIndex = ShaMapGet(&pack->map, object->baseSha);
        if (baseIndex >= 0) { if (!ResolveDelta(pack, baseIndex, repo)) return false; base = pack->objects[baseIndex].data; baseLength = pack->objects[baseIndex].length; baseType = pack->objects[baseIndex].type; }
        else if (!FindRepoObject(repo, object->baseSha, &base, &baseLength, &baseType)) return false;
    }
    size_t resolvedLength = 0;
    uint8_t *resolved = ApplyDelta(base, baseLength, object->data, object->length, &resolvedLength);
    if (!resolved) return false;
    free(object->data);
    object->data = resolved; object->length = resolvedLength; object->type = baseType;
    object->resolved = true; object->resolving = false;
    GitObjectSha(ObjectKindName(object->type), object->data, object->length, object->sha);
    ShaMapPut(&pack->map, object->sha, index);
    return true;
}

/* Unpacks a whole pack. Returns NULL if it worked, or what was wrong. */
const char *ReadPack(const uint8_t *data, size_t length, PackContents *pack, Repo *repo)
{
    memset(pack, 0, sizeof *pack);
    if (length < 32 || memcmp(data, "PACK", 4) != 0) return "not a git pack";
    uint32_t version = (uint32_t)data[4] << 24 | (uint32_t)data[5] << 16 | (uint32_t)data[6] << 8 | data[7];
    uint32_t count = (uint32_t)data[8] << 24 | (uint32_t)data[9] << 16 | (uint32_t)data[10] << 8 | data[11];
    if (version != 2 && version != 3) return "unknown pack version";
    if (count > 10000000) return "too many objects";
    uint8_t checksum[20];
    Sha1 sha;
    Sha1Init(&sha); Sha1Update(&sha, data, length - 20); Sha1Final(&sha, checksum);
    if (memcmp(checksum, data + length - 20, 20) != 0) return "the pack is damaged (checksum)";
    size_t pos = 12, end = length - 20;
    for (uint32_t index = 0; index < count; index++) {
        pack->objects = GrowArray(pack->objects, &pack->capacity, pack->count + 1, sizeof(PackEntry));
        if (pack->count >= pack->capacity) return "out of memory";
        PackEntry *object = &pack->objects[pack->count];
        memset(object, 0, sizeof *object);
        object->offset = pos;
        if (pos >= end) return "the pack is cut short";
        uint8_t byte = data[pos++];
        object->type = (byte >> 4) & 7;
        uint64_t size = byte & 15;
        int shift = 4;
        while (byte & 0x80) { if (pos >= end || shift > 56) return "the pack is damaged"; byte = data[pos++]; size |= (uint64_t)(byte & 0x7F) << shift; shift += 7; }
        if (size > MAX_OBJECT) return "an object is too big";
        if (object->type == OBJ_OFS_DELTA) {
            if (pos >= end) return "the pack is damaged";
            byte = data[pos++];
            uint64_t backOffset = byte & 0x7F;
            while (byte & 0x80) { if (pos >= end) return "the pack is damaged"; byte = data[pos++]; backOffset = ((backOffset + 1) << 7) | (byte & 0x7F); }
            if (backOffset == 0 || backOffset > object->offset) return "the pack is damaged (delta)";
            object->baseOffset = object->offset - (size_t)backOffset;
            object->isDelta = true;
        } else if (object->type == OBJ_REF_DELTA) {
            if (pos + 20 > end) return "the pack is damaged";
            memcpy(object->baseSha, data + pos, 20);
            pos += 20;
            object->isDelta = true;
        } else if (object->type < 1 || object->type > 4) return "the pack has an unknown kind of object";
        size_t used = 0, objectLength = 0;
        object->data = InflateZlib(data + pos, end - pos, &used, &objectLength, (size_t)size);
        if (!object->data || objectLength != size) return "the pack is damaged (couldn't unpack an object)";
        object->length = objectLength;
        pos += used;
        if (!object->isDelta) {
            object->resolved = true;
            GitObjectSha(ObjectKindName(object->type), object->data, object->length, object->sha);
            ShaMapPut(&pack->map, object->sha, pack->count);
        }
        pack->count++;
    }
    if (pos != end) return "the pack has extra data at the end";
    for (int i = 0; i < pack->count; i++) if (!ResolveDelta(pack, i, repo)) return "a delta's base object is missing";
    return NULL;
}
