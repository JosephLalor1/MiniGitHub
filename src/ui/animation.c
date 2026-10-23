/*
 * animation.c - values that ease toward where they should be
 *
 * A small table of animated numbers, looked up by key. Each call moves the
 * number toward its target by however much time has passed since it was
 * last asked for, so a value asked for twice in one frame doesn't move
 * twice as fast. Entries nobody has asked for in a while are reused.
 */

#include "animation.h"

#define ANIMATION_SLOTS 1024         /* how many animated values can be kept at once (a power of 2) */
#define ANIMATION_PROBES 32          /* how many slots to try before giving up on one */
#define STALE_SECONDS 0.25           /* not asked for in this long: it was off screen, start again */

typedef struct { unsigned key; float value; double lastTime; bool used; } AnimationSlot;
static AnimationSlot animationSlots[ANIMATION_SLOTS];

/* Mixes a number into a hash (FNV-1a over its bytes). */
static unsigned HashFloat(unsigned hash, float number)
{
    unsigned char bytes[sizeof number];
    memcpy(bytes, &number, sizeof number);
    for (size_t i = 0; i < sizeof number; i++) hash = (hash ^ bytes[i]) * 16777619u;
    return hash;
}

unsigned AnimationKey(const char *tag, float first, float second)
{
    unsigned hash = 2166136261u;
    for (const char *ch = tag; *ch; ch++) hash = (hash ^ (unsigned char)*ch) * 16777619u;
    return HashFloat(HashFloat(hash, first), second);
}

unsigned RectKey(Rectangle rect)
{
    return HashFloat(HashFloat(AnimationKey("rect", rect.x, rect.y), rect.width), rect.height);
}

float Animate(unsigned key, float target, float speed)
{
    double now = GetTime();
    AnimationSlot *slot = NULL, *reusable = NULL;
    for (int probe = 0; probe < ANIMATION_PROBES; probe++) {
        AnimationSlot *candidate = &animationSlots[(key + (unsigned)probe) & (ANIMATION_SLOTS - 1)];
        if (candidate->used && candidate->key == key) { slot = candidate; break; }
        if (!reusable && (!candidate->used || now - candidate->lastTime > 1.0)) reusable = candidate;
    }
    if (!slot) {
        if (!reusable) return target;                    /* table full: just don't animate */
        slot = reusable;
        *slot = (AnimationSlot){key, 0, now, true};
    }
    float elapsed = (float)(now - slot->lastTime);
    if (now - slot->lastTime > STALE_SECONDS) { slot->value = 0; elapsed = 0; }   /* it was gone: start again */
    if (elapsed > 0.1f) elapsed = 0.1f;
    slot->lastTime = now;
    slot->value += (target - slot->value) * (1 - expf(-speed * elapsed));
    if (fabsf(target - slot->value) < 0.001f) slot->value = target;
    return slot->value;
}

float HoverAmount(unsigned key, bool hovered) { return Animate(key, hovered ? 1.0f : 0.0f, 14.0f); }

float EaseInOut(float amount)
{
    if (amount <= 0) return 0;
    if (amount >= 1) return 1;
    return amount * amount * (3 - 2 * amount);
}
