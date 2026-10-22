/*
 * util.c - small helpers used everywhere
 *
 * Hashing short strings (FNV-1a), growing arrays, case-insensitive
 * comparisons, tidying names and paths that people type, and "3 hours ago"
 * times.
 */

#include "util.h"

/* FNV-1a hash of length bytes, continuing from hash (start with FNV_OFFSET_BASIS). */
uint32_t HashFnv1a(const char *text, int length, uint32_t hash)
{
    for (int i = 0; i < length; i++) { hash ^= (unsigned char)text[i]; hash *= 16777619u; }
    return hash;
}

/* An array that grows as needed (doubling). Returns the new pointer. */
void *GrowArray(void *array, int *capacity, int needed, size_t itemSize)
{
    if (needed <= *capacity) return array;
    int newCapacity = *capacity ? *capacity : 16;
    while (newCapacity < needed) newCapacity *= 2;
    void *grown = realloc(array, (size_t)newCapacity * itemSize);
    if (!grown) return array;
    memset((char *)grown + (size_t)*capacity * itemSize, 0, (size_t)(newCapacity - *capacity) * itemSize);
    *capacity = newCapacity;
    return grown;
}

/* Is needle somewhere in haystack, ignoring capitals? An empty needle is in everything. */
bool ContainsIgnoreCase(const char *haystack, const char *needle)
{
    if (!needle[0]) return true;
    for (; *haystack; haystack++) {
        int matched = 0;
        while (needle[matched] && haystack[matched] &&
               tolower((unsigned char)haystack[matched]) == tolower((unsigned char)needle[matched])) matched++;
        if (!needle[matched]) return true;
    }
    return false;
}

/* A file path someone typed: like SanitizeName, but "/" makes folders
   ("docs/notes.txt"). Doubled or stray slashes are dropped. */
void SanitizePath(const char *input, char *output)
{
    int length = 0;
    for (int i = 0; input[i] && length < PATH_LEN - 1; i++) {
        char ch = input[i] == '\\' ? '/' : input[i];
        if (isalnum((unsigned char)ch) || ch == '-' || ch == '_' || ch == '.') output[length++] = ch;
        else if (ch == ' ') output[length++] = '-';
        else if (ch == '/' && length > 0 && output[length - 1] != '/') output[length++] = '/';
    }
    while (length > 0 && output[length - 1] == '/') length--;
    output[length] = 0;
}

/* Keep letters, digits, - _ . ; turn spaces into dashes. */
void SanitizeName(const char *input, char *output)
{
    int length = 0;
    for (int i = 0; input[i] && length < NAME_LEN - 1; i++) {
        char ch = input[i];
        if (isalnum((unsigned char)ch) || ch == '-' || ch == '_' || ch == '.') output[length++] = ch;
        else if (ch == ' ') output[length++] = '-';
    }
    output[length] = 0;
}

/* "just now", "5 minutes ago", "3 days ago" or "on 13 Oct 2026" for a moment in the past. */
const char *TimeAgo(long long when)
{
    long long secondsAgo = (long long)time(NULL) - when;
    if (secondsAgo < 0) secondsAgo = 0;
    if (secondsAgo < 60)         return "just now";
    if (secondsAgo < 3600)       return TextFormat("%d minute%s ago", (int)(secondsAgo / 60),    secondsAgo / 60    == 1 ? "" : "s");
    if (secondsAgo < 86400)      return TextFormat("%d hour%s ago",   (int)(secondsAgo / 3600),  secondsAgo / 3600  == 1 ? "" : "s");
    if (secondsAgo < 86400 * 30) return TextFormat("%d day%s ago",    (int)(secondsAgo / 86400), secondsAgo / 86400 == 1 ? "" : "s");
    static char formatted[32];
    time_t whenTime = (time_t)when;
    strftime(formatted, sizeof formatted, "on %d %b %Y", localtime(&whenTime));
    return formatted;
}

/* Are the first length characters of a and b the same, ignoring capitals? */
bool SameTextIgnoreCase(const char *a, const char *b, int length)
{
    for (int i = 0; i < length; i++) if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) return false;
    return true;
}

/* ------------------------------------------------------------------ */
/* Users                                                              */
/* ------------------------------------------------------------------ */
/* Are two strings the same, ignoring capitals? */
bool EqualsIgnoreCase(const char *a, const char *b)
{
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
        a++; b++;
    }
    return *a == *b;
}
