/*
 * passwords.c - passwords and recovery codes
 *
 * Salted PBKDF2-SHA256 password hashes, random bytes from the operating
 * system, and one-time recovery codes for forgotten passwords. Passwords
 * themselves are never kept.
 */

#include "passwords.h"
#include "model.h"
#include "sha256.h"

static void ToHex(const uint8_t *bytes, int count, char *hex)
{
    static const char digits[] = "0123456789abcdef";
    for (int i = 0; i < count; i++) { hex[2*i] = digits[bytes[i] >> 4]; hex[2*i+1] = digits[bytes[i] & 15]; }
    hex[2 * count] = 0;
}
/* The value of one hex digit (0 for anything that isn't one). */
int HexDigitValue(char digit)
{
    if (digit >= '0' && digit <= '9') return digit - '0';
    if (digit >= 'a' && digit <= 'f') return digit - 'a' + 10;
    if (digit >= 'A' && digit <= 'F') return digit - 'A' + 10;
    return 0;
}
static void FromHex(const char *hex, uint8_t *bytes, int count)
{
    for (int i = 0; i < count; i++) bytes[i] = (uint8_t)(HexDigitValue(hex[2*i]) << 4 | HexDigitValue(hex[2*i+1]));
}

/* Random bytes for salts: the operating system's random source when there
   is one (Linux / macOS), otherwise a mix of clocks and raylib's generator. */
static void RandomBytes(uint8_t *out, int count)
{
#ifdef _WIN32
    bool allGood = true;
    for (int i = 0; i < count && allGood; i++) {
        unsigned int value;
        if (rand_s(&value) != 0) allGood = false;
        else out[i] = (uint8_t)value;
    }
    if (allGood) return;
#endif
    FILE *urandom = fopen("/dev/urandom", "rb");
    if (urandom) {
        size_t bytesRead = fread(out, 1, (size_t)count, urandom);
        fclose(urandom);
        if ((int)bytesRead == count) return;
    }
    Sha256 sha;
    uint8_t digest[32];
    long long now = (long long)time(NULL);
    clock_t cpuTime = clock();
    double raylibTime = GetTime();
    void *stackAddress = &sha;
    Sha256Init(&sha);
    Sha256Update(&sha, &now, sizeof now); Sha256Update(&sha, &cpuTime, sizeof cpuTime);
    Sha256Update(&sha, &raylibTime, sizeof raylibTime); Sha256Update(&sha, &stackAddress, sizeof stackAddress);
    for (int i = 0; i < 16; i++) { int value = GetRandomValue(0, 0x7fffffff); Sha256Update(&sha, &value, sizeof value); }
    Sha256Final(&sha, digest);
    for (int i = 0; i < count; i++) out[i] = digest[i % 32] ^ (uint8_t)GetRandomValue(0, 255);
}

/* The PBKDF2-SHA256 hash (as 64 hex digits) of a password with a salt given as 32 hex digits. */
void HashPassword(const char *password, const char *saltHex, char outHex[65])
{
    uint8_t salt[16], derivedKey[32];
    FromHex(saltHex, salt, 16);
    Pbkdf2(password, salt, 16, PBKDF2_ITER, derivedKey);
    ToHex(derivedKey, 32, outHex);
}
/* Gives the user a new random salt and stores the hash of password with it. */
void SetPassword(User *user, const char *password)
{
    uint8_t salt[16];
    RandomBytes(salt, 16);
    ToHex(salt, 16, user->salt);
    HashPassword(password, user->salt, user->hash);
}
/* Compares every character (no early exit) so timing gives nothing away. */
bool CheckPassword(User *user, const char *password)
{
    char hashHex[65];
    HashPassword(password, user->salt, hashHex);
    unsigned difference = 0;
    for (int i = 0; i < 64; i++) difference |= (unsigned)(hashHex[i] ^ user->hash[i]);
    return difference == 0;
}

/* ------------------------------------------------------------------ */
/* Recovery codes ("forgot password")                                 */
/*                                                                    */
/* There's no email on a local app, so instead every account can have */
/* a recovery code like  K7QM-2XWD-9PRT-H4NC . It is shown once, and   */
/* only its hash is saved - exactly like the password. Using it to    */
/* reset the password uses it up, and a fresh one is shown.           */
/* ------------------------------------------------------------------ */

/* 32 letters/digits with the easily confused ones (0 O 1 I) left out */
static const char RECOVERY_CODE_ALPHABET[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";

/* Keep only letters and digits, in capitals, so "k7qm 2xwd-..." still works.
   Returns how many characters were kept. */
int NormalizeRecoveryCode(const char *typed, char normalized[RECOVERY_CODE_LENGTH + 1])
{
    int count = 0;
    for (int i = 0; typed[i]; i++) {
        if (!isalnum((unsigned char)typed[i])) continue;
        if (count == RECOVERY_CODE_LENGTH) return RECOVERY_CODE_LENGTH + 1;  /* too long */
        normalized[count++] = (char)toupper((unsigned char)typed[i]);
    }
    normalized[count] = 0;
    return count;
}

/* Has this user made a recovery code? */
bool HasRecoveryCode(User *user) { return user->recoveryHash[0] != 0; }

/* Makes a new code for user (the old one stops working) and writes the
   readable "XXXX-XXXX-XXXX-XXXX" form into shown. */
void NewRecoveryCode(User *user, char shown[24])
{
    uint8_t randomBytes[RECOVERY_CODE_LENGTH], salt[16];
    char plain[RECOVERY_CODE_LENGTH + 1];
    RandomBytes(randomBytes, RECOVERY_CODE_LENGTH);
    for (int i = 0; i < RECOVERY_CODE_LENGTH; i++) plain[i] = RECOVERY_CODE_ALPHABET[randomBytes[i] % 32];  /* 256 % 32 == 0: no bias */
    plain[RECOVERY_CODE_LENGTH] = 0;
    snprintf(shown, 24, "%.4s-%.4s-%.4s-%.4s", plain, plain + 4, plain + 8, plain + 12);

    RandomBytes(salt, 16);
    ToHex(salt, 16, user->recoverySalt);
    HashPassword(plain, user->recoverySalt, user->recoveryHash);
    memset(plain, 0, sizeof plain);
    memset(randomBytes, 0, sizeof randomBytes);
}

/* Is this the user's recovery code? Spaces, dashes and capitals don't matter. */
bool CheckRecoveryCode(User *user, const char *typed)
{
    char normalized[RECOVERY_CODE_LENGTH + 1], hashHex[65];
    if (!HasRecoveryCode(user) || NormalizeRecoveryCode(typed, normalized) != RECOVERY_CODE_LENGTH) return false;
    HashPassword(normalized, user->recoverySalt, hashHex);
    unsigned difference = 0;
    for (int i = 0; i < 64; i++) difference |= (unsigned)(hashHex[i] ^ user->recoveryHash[i]);
    return difference == 0;
}
