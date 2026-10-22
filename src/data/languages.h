/*
 * languages.h - what language a file is written in
 *
 * Guesses from clues in the content and the file's name, remembered per
 * file, and the breakdown of a repository's code by language.
 */

#ifndef MINIGITHUB_LANGUAGES_H
#define MINIGITHUB_LANGUAGES_H

#include "common.h"
#include "model.h"
#include "repo_view.h"

enum {
    LANG_C, LANG_CPP, LANG_CSHARP, LANG_JAVA, LANG_PYTHON, LANG_JS, LANG_TS, LANG_RUST, LANG_GO,
    LANG_HTML, LANG_CSS, LANG_SQL, LANG_SHELL, LANG_PHP, LANG_RUBY, LANG_KOTLIN, LANG_SWIFT,
    LANG_ASM, LANG_LUA, LANG_MAKE,
    LANG_FIRST_NONCODE,                       /* everything below isn't counted as code */
    LANG_MARKDOWN = LANG_FIRST_NONCODE, LANG_JSON, LANG_YAML, LANG_TEXT,
    LANG_COUNT
};

extern const char *LANG_NAMES[LANG_COUNT];
#define LANG_BINARY (-2)                      /* not text at all (pictures and so on) */

/* Remembers a file's points so they're only worked out once. */
struct LangCache {
    unsigned short scores[LANG_COUNT];   /* points for each language, from the content */
    int  lines;
    bool done;
    bool binary;
    const char *pickedFor;               /* the (shared, interned) path pickedLang was worked out for */
    int  pickedLang;                     /* the language for that path: scores plus its file ending */
};

/* Works out a file's language clues and line count into *cache. */
void FillLangCache(LangCache *cache, const char *data, int length, bool binary);

/* The language of a file from its clues and its name (LANG_BINARY for non-text files). */
int LangFromCache(LangCache *cache, const char *name);

/* A blob's language clues, worked out the first time they're needed. */
LangCache *GetBlobLangCache(Repo *repo, int blobIndex);

/* The language of a file as you see it, and how many lines it has. */
int ViewFileLang(Repo *repo, ViewFile *viewFile, int *lineCount);

/* The name to show for a language ("Binary" for non-text files). */
const char *LangLabel(int lang);
extern const Color LANG_PALETTE[8];
extern const Color LANG_OTHER;
typedef struct { int lang; int lines; int files; Color color; } LangPart;   /* lang -1 = "Other" */
extern LangPart langParts[6];
extern int langPartCount, langTotal;
extern char langSkipped[TEXT_LEN];  /* the kinds of file that aren't counted, e.g. "Markdown, JSON" */
void BuildLanguages(Repo *repo, int userId);
extern bool langPartsAreCommits;

/* Turns lines and files per language into the parts drawn by the language bar and the tech stack
   pie (into langParts). */
void BuildLangParts(const int *lines, const int *files, const bool *skipped, bool binary, bool countsAreCommits);

#endif
