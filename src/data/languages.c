/*
 * languages.c - what language a file is written in
 *
 * Guesses from clues in the content and the file's name, remembered per
 * file, and the breakdown of a repository's code by language.
 */

#include "languages.h"
#include "model.h"
#include "repo_view.h"
#include "util.h"

/* ------------------------------------------------------------------ */
/* Language detection                                                 */
/*                                                                    */
/* Reads a file and guesses what it's written in. Each language has a */
/* list of clues: words it uses (like "def" or "fn"), bits of text it */
/* uses ("std::", "console.log("), and how its lines start            */
/* ("#include", "import "). Every clue found adds points to its       */
/* language; the language with the most points wins.                  */
/*                                                                    */
/* A clue only scores up to 4 times, so one common word can't win on  */
/* its own - a file has to look like the language in several ways.    */
/* The file's ending (.c, .py...) is used as one extra clue, worth a  */
/* few points, so a tiny file can still be recognised.                */
/* ------------------------------------------------------------------ */

const char *LANG_NAMES[LANG_COUNT] = {
    "C", "C++", "C#", "Java", "Python", "JavaScript", "TypeScript", "Rust", "Go",
    "HTML", "CSS", "SQL", "Shell", "PHP", "Ruby", "Kotlin", "Swift",
    "Assembly", "Lua", "Makefile",
    "Markdown", "JSON", "YAML", "Plain text"
};

/* how a clue is matched */
enum { CLUE_WORD,      /* a whole word, e.g. "def" but not "define"             */
       CLUE_WORD_ANYCASE,        /* a whole word, any capitals (SQL: select = SELECT)     */
       CLUE_TEXT,                /* this text anywhere                                    */
       CLUE_LINE_START,          /* a line starts with this (after any spaces)            */
       CLUE_LINE_START_ANYCASE,  /* the same, any capitals                                */
       CLUE_LINE_END,            /* a line ends with this                                 */
       CLUE_SHEBANG }; /* the first line is "#!..." and contains this           */

typedef struct { unsigned char lang, kind, weight; const char *text; } Clue;

static const Clue CLUES[] = {
    /* ---- C ---- */
    {LANG_C, CLUE_LINE_START, 3, "#include"}, {LANG_C, CLUE_TEXT, 3, "#include <stdio.h>"}, {LANG_C, CLUE_TEXT, 3, "#include <stdlib.h>"},
    {LANG_C, CLUE_TEXT, 2, ".h\""}, {LANG_C, CLUE_TEXT, 2, "printf("}, {LANG_C, CLUE_TEXT, 3, "malloc("}, {LANG_C, CLUE_TEXT, 2, "free("},
    {LANG_C, CLUE_TEXT, 2, "typedef struct"}, {LANG_C, CLUE_LINE_START, 2, "#define"}, {LANG_C, CLUE_LINE_START, 2, "#ifdef"}, {LANG_C, CLUE_LINE_START, 2, "#ifndef"},
    {LANG_C, CLUE_LINE_START, 2, "#endif"}, {LANG_C, CLUE_WORD, 1, "unsigned"}, {LANG_C, CLUE_WORD, 1, "sizeof"}, {LANG_C, CLUE_TEXT, 1, "->"},
    {LANG_C, CLUE_WORD, 1, "static"}, {LANG_C, CLUE_WORD, 1, "void"}, {LANG_C, CLUE_WORD, 1, "int"}, {LANG_C, CLUE_TEXT, 2, "int main("},
    {LANG_C, CLUE_WORD, 1, "struct"}, {LANG_C, CLUE_TEXT, 2, "NULL"}, {LANG_C, CLUE_WORD, 1, "const"}, {LANG_C, CLUE_TEXT, 1, "/*"},
    /* ---- C++ (on top of C: these are things C doesn't have) ---- */
    {LANG_CPP, CLUE_LINE_START, 3, "#include"}, {LANG_CPP, CLUE_TEXT, 4, "std::"}, {LANG_CPP, CLUE_WORD, 4, "namespace"}, {LANG_CPP, CLUE_TEXT, 4, "template <"},
    {LANG_CPP, CLUE_TEXT, 4, "template<"}, {LANG_CPP, CLUE_TEXT, 3, "#include <iostream>"}, {LANG_CPP, CLUE_TEXT, 3, "#include <vector>"},
    {LANG_CPP, CLUE_TEXT, 3, "#include <string>"}, {LANG_CPP, CLUE_WORD, 3, "cout"}, {LANG_CPP, CLUE_WORD, 3, "nullptr"},
    {LANG_CPP, CLUE_LINE_START, 3, "public:"}, {LANG_CPP, CLUE_LINE_START, 3, "private:"}, {LANG_CPP, CLUE_LINE_START, 3, "protected:"},
    {LANG_CPP, CLUE_WORD, 2, "class"}, {LANG_CPP, CLUE_TEXT, 2, "::"}, {LANG_CPP, CLUE_WORD, 2, "virtual"}, {LANG_CPP, CLUE_WORD, 2, "override"},
    {LANG_CPP, CLUE_TEXT, 3, "using namespace"}, {LANG_CPP, CLUE_WORD, 2, "constexpr"}, {LANG_CPP, CLUE_WORD, 2, "typename"},
    {LANG_CPP, CLUE_LINE_START, 2, "#define"}, {LANG_CPP, CLUE_LINE_START, 2, "#ifndef"}, {LANG_CPP, CLUE_LINE_START, 2, "#endif"}, {LANG_CPP, CLUE_WORD, 1, "const"},
    {LANG_CPP, CLUE_WORD, 1, "void"}, {LANG_CPP, CLUE_WORD, 1, "int"}, {LANG_CPP, CLUE_TEXT, 2, "auto "},
    /* ---- C# ---- */
    {LANG_CSHARP, CLUE_LINE_START, 5, "using System"}, {LANG_CSHARP, CLUE_TEXT, 4, "Console.Write"}, {LANG_CSHARP, CLUE_WORD, 3, "namespace"},
    {LANG_CSHARP, CLUE_TEXT, 4, "{ get; set; }"}, {LANG_CSHARP, CLUE_TEXT, 3, "{ get;"}, {LANG_CSHARP, CLUE_WORD, 2, "public"}, {LANG_CSHARP, CLUE_WORD, 2, "class"},
    {LANG_CSHARP, CLUE_WORD, 2, "string"}, {LANG_CSHARP, CLUE_WORD, 2, "var"}, {LANG_CSHARP, CLUE_TEXT, 3, "static void Main"},
    {LANG_CSHARP, CLUE_WORD, 3, "readonly"}, {LANG_CSHARP, CLUE_LINE_START, 3, "[Test"}, {LANG_CSHARP, CLUE_WORD, 2, "async"}, {LANG_CSHARP, CLUE_TEXT, 2, "await "},
    {LANG_CSHARP, CLUE_WORD, 2, "foreach"}, {LANG_CSHARP, CLUE_LINE_START, 2, "///"}, {LANG_CSHARP, CLUE_WORD, 2, "internal"},
    /* ---- Java ---- */
    {LANG_JAVA, CLUE_LINE_START, 5, "import java."}, {LANG_JAVA, CLUE_LINE_START, 4, "package "}, {LANG_JAVA, CLUE_TEXT, 5, "System.out.print"},
    {LANG_JAVA, CLUE_TEXT, 5, "public static void main(String"}, {LANG_JAVA, CLUE_WORD, 2, "public"}, {LANG_JAVA, CLUE_WORD, 2, "class"},
    {LANG_JAVA, CLUE_WORD, 2, "private"}, {LANG_JAVA, CLUE_WORD, 2, "extends"}, {LANG_JAVA, CLUE_WORD, 3, "implements"}, {LANG_JAVA, CLUE_LINE_START, 3, "@Override"},
    {LANG_JAVA, CLUE_WORD, 2, "String"}, {LANG_JAVA, CLUE_WORD, 3, "throws"}, {LANG_JAVA, CLUE_WORD, 2, "new"}, {LANG_JAVA, CLUE_WORD, 2, "final"},
    {LANG_JAVA, CLUE_LINE_START, 2, "import "}, {LANG_JAVA, CLUE_LINE_START, 2, "/**"},
    /* ---- Python ---- */
    {LANG_PYTHON, CLUE_LINE_START, 4, "def "}, {LANG_PYTHON, CLUE_LINE_START, 3, "import "}, {LANG_PYTHON, CLUE_LINE_START, 4, "from "}, {LANG_PYTHON, CLUE_TEXT, 3, " import "},
    {LANG_PYTHON, CLUE_WORD, 4, "self"}, {LANG_PYTHON, CLUE_WORD, 4, "elif"}, {LANG_PYTHON, CLUE_TEXT, 4, "__init__"}, {LANG_PYTHON, CLUE_TEXT, 3, "__name__"},
    {LANG_PYTHON, CLUE_LINE_END, 1, ":"}, {LANG_PYTHON, CLUE_LINE_START, 3, "if __name__"}, {LANG_PYTHON, CLUE_LINE_START, 2, "return "},
    {LANG_PYTHON, CLUE_LINE_START, 3, "raise "}, {LANG_PYTHON, CLUE_LINE_START, 3, "try:"}, {LANG_PYTHON, CLUE_LINE_START, 3, "with "}, {LANG_PYTHON, CLUE_TEXT, 2, "f\""}, {LANG_PYTHON, CLUE_WORD, 2, "None"}, {LANG_PYTHON, CLUE_WORD, 2, "True"}, {LANG_PYTHON, CLUE_WORD, 2, "False"},
    {LANG_PYTHON, CLUE_LINE_START, 3, "class "}, {LANG_PYTHON, CLUE_TEXT, 2, "print("}, {LANG_PYTHON, CLUE_LINE_START, 2, "#"}, {LANG_PYTHON, CLUE_TEXT, 3, "\"\"\""},
    {LANG_PYTHON, CLUE_WORD, 2, "lambda"}, {LANG_PYTHON, CLUE_WORD, 2, "except"}, {LANG_PYTHON, CLUE_LINE_START, 3, "@"}, {LANG_PYTHON, CLUE_SHEBANG, 9, "python"},
    {LANG_PYTHON, CLUE_WORD, 1, "not"}, {LANG_PYTHON, CLUE_WORD, 1, "in"}, {LANG_PYTHON, CLUE_WORD, 2, "pass"},
    /* ---- JavaScript ---- */
    {LANG_JS, CLUE_WORD, 3, "function"}, {LANG_JS, CLUE_LINE_START, 2, "const "}, {LANG_JS, CLUE_LINE_START, 2, "let "}, {LANG_JS, CLUE_WORD, 2, "var"},
    {LANG_JS, CLUE_TEXT, 3, "=>"}, {LANG_JS, CLUE_TEXT, 4, "console.log("}, {LANG_JS, CLUE_TEXT, 4, "require("}, {LANG_JS, CLUE_TEXT, 4, "module.exports"},
    {LANG_JS, CLUE_TEXT, 3, "document."}, {LANG_JS, CLUE_TEXT, 3, "===",}, {LANG_JS, CLUE_TEXT, 2, "!=="}, {LANG_JS, CLUE_WORD, 2, "undefined"},
    {LANG_JS, CLUE_LINE_START, 3, "export "}, {LANG_JS, CLUE_LINE_START, 3, "import "}, {LANG_JS, CLUE_WORD, 2, "async"}, {LANG_JS, CLUE_WORD, 2, "await"},
    {LANG_JS, CLUE_WORD, 2, "this"}, {LANG_JS, CLUE_TEXT, 2, "});"}, {LANG_JS, CLUE_SHEBANG, 9, "node"}, {LANG_JS, CLUE_LINE_START, 2, "'use strict'"},
    {LANG_JS, CLUE_LINE_START, 2, "\"use strict\""}, {LANG_JS, CLUE_WORD, 1, "return"}, {LANG_JS, CLUE_WORD, 1, "null"},
    /* ---- TypeScript (on top of JavaScript: types) ---- */
    {LANG_TS, CLUE_WORD, 3, "function"}, {LANG_TS, CLUE_LINE_START, 2, "const "}, {LANG_TS, CLUE_LINE_START, 2, "let "}, {LANG_TS, CLUE_TEXT, 3, "=>"},
    {LANG_TS, CLUE_LINE_START, 3, "export "}, {LANG_TS, CLUE_LINE_START, 3, "import "}, {LANG_TS, CLUE_TEXT, 2, "==="}, {LANG_TS, CLUE_WORD, 2, "async"},
    {LANG_TS, CLUE_WORD, 2, "this"}, {LANG_TS, CLUE_WORD, 1, "return"}, {LANG_TS, CLUE_WORD, 1, "null"}, {LANG_TS, CLUE_WORD, 2, "undefined"},
    {LANG_TS, CLUE_TEXT, 5, ": string;"}, {LANG_TS, CLUE_TEXT, 5, ": string)"}, {LANG_TS, CLUE_TEXT, 5, ": string,"}, {LANG_TS, CLUE_TEXT, 4, ": string ="},
    {LANG_TS, CLUE_TEXT, 5, ": number;"}, {LANG_TS, CLUE_TEXT, 5, ": number)"}, {LANG_TS, CLUE_TEXT, 5, ": number,"}, {LANG_TS, CLUE_TEXT, 5, ": boolean;"},
    {LANG_TS, CLUE_TEXT, 4, ": boolean)"}, {LANG_TS, CLUE_TEXT, 4, "): string"}, {LANG_TS, CLUE_TEXT, 4, "): number"}, {LANG_TS, CLUE_TEXT, 4, "): boolean"},
    {LANG_TS, CLUE_TEXT, 3, "?: "}, {LANG_TS, CLUE_WORD, 4, "interface"},
    {LANG_TS, CLUE_LINE_START, 5, "export type"}, {LANG_TS, CLUE_LINE_START, 5, "export interface"}, {LANG_TS, CLUE_LINE_START, 5, "declare "},
    {LANG_TS, CLUE_TEXT, 4, "): void"}, {LANG_TS, CLUE_TEXT, 3, "readonly "}, {LANG_TS, CLUE_TEXT, 3, "<T>"}, {LANG_TS, CLUE_TEXT, 3, ": any;"},
    {LANG_TS, CLUE_TEXT, 3, ": any)"},
    {LANG_TS, CLUE_TEXT, 3, "): Promise<"}, {LANG_TS, CLUE_WORD, 2, "enum"},
    /* ---- Rust ---- */
    {LANG_RUST, CLUE_LINE_START, 4, "fn "}, {LANG_RUST, CLUE_LINE_START, 4, "pub fn "}, {LANG_RUST, CLUE_TEXT, 5, "let mut "}, {LANG_RUST, CLUE_TEXT, 5, "println!("},
    {LANG_RUST, CLUE_LINE_START, 4, "use std::"}, {LANG_RUST, CLUE_LINE_START, 3, "impl "}, {LANG_RUST, CLUE_TEXT, 3, "&mut "}, {LANG_RUST, CLUE_WORD, 3, "match"},
    {LANG_RUST, CLUE_TEXT, 3, "::new("}, {LANG_RUST, CLUE_TEXT, 3, "Vec<"}, {LANG_RUST, CLUE_TEXT, 3, "Option<"}, {LANG_RUST, CLUE_TEXT, 3, "Result<"},
    {LANG_RUST, CLUE_LINE_START, 3, "#[derive("}, {LANG_RUST, CLUE_TEXT, 2, "-> "}, {LANG_RUST, CLUE_WORD, 3, "struct"}, {LANG_RUST, CLUE_TEXT, 3, ".unwrap()"},
    {LANG_RUST, CLUE_LINE_START, 3, "mod "}, {LANG_RUST, CLUE_WORD, 2, "pub"}, {LANG_RUST, CLUE_TEXT, 2, "Some("}, {LANG_RUST, CLUE_WORD, 2, "enum"},
    /* ---- Go ---- */
    {LANG_GO, CLUE_LINE_START, 5, "package "}, {LANG_GO, CLUE_LINE_START, 4, "func "}, {LANG_GO, CLUE_TEXT, 4, ":="}, {LANG_GO, CLUE_TEXT, 4, "fmt."},
    {LANG_GO, CLUE_LINE_START, 4, "import ("}, {LANG_GO, CLUE_TEXT, 3, "err != nil"}, {LANG_GO, CLUE_WORD, 3, "chan"}, {LANG_GO, CLUE_WORD, 2, "defer"},
    {LANG_GO, CLUE_TEXT, 3, "go func"}, {LANG_GO, CLUE_WORD, 2, "struct"}, {LANG_GO, CLUE_WORD, 1, "nil"}, {LANG_GO, CLUE_TEXT, 2, "[]string"},
    {LANG_GO, CLUE_TEXT, 2, "interface{}"}, {LANG_GO, CLUE_LINE_START, 3, "type "},
    /* ---- HTML ---- */
    {LANG_HTML, CLUE_TEXT, 6, "<!DOCTYPE"}, {LANG_HTML, CLUE_TEXT, 6, "<!doctype"}, {LANG_HTML, CLUE_TEXT, 5, "<html"}, {LANG_HTML, CLUE_TEXT, 3, "<head"},
    {LANG_HTML, CLUE_TEXT, 3, "<body"}, {LANG_HTML, CLUE_TEXT, 3, "<div"}, {LANG_HTML, CLUE_TEXT, 3, "</div>"}, {LANG_HTML, CLUE_TEXT, 2, "<p>"},
    {LANG_HTML, CLUE_TEXT, 2, "</p>"}, {LANG_HTML, CLUE_TEXT, 2, "href=\""}, {LANG_HTML, CLUE_TEXT, 2, "<a "}, {LANG_HTML, CLUE_TEXT, 2, "<span"},
    {LANG_HTML, CLUE_TEXT, 2, "<meta"}, {LANG_HTML, CLUE_TEXT, 2, "<script"}, {LANG_HTML, CLUE_TEXT, 2, "<title>"}, {LANG_HTML, CLUE_TEXT, 2, "class=\""},
    {LANG_HTML, CLUE_TEXT, 2, "<li>"}, {LANG_HTML, CLUE_TEXT, 2, "<br"}, {LANG_HTML, CLUE_TEXT, 2, "<table"}, {LANG_HTML, CLUE_TEXT, 3, "</"},
    {LANG_HTML, CLUE_TEXT, 2, "<!--"}, {LANG_HTML, CLUE_TEXT, 2, "<h1"}, {LANG_HTML, CLUE_TEXT, 2, "<h2"}, {LANG_HTML, CLUE_TEXT, 2, "<h3"},
    {LANG_HTML, CLUE_TEXT, 2, "<ul"}, {LANG_HTML, CLUE_TEXT, 2, "<img"}, {LANG_HTML, CLUE_TEXT, 2, "<link"}, {LANG_HTML, CLUE_TEXT, 2, "<style"},
    {LANG_HTML, CLUE_TEXT, 2, "<input"}, {LANG_HTML, CLUE_TEXT, 2, "<form"}, {LANG_HTML, CLUE_TEXT, 2, "<button"}, {LANG_HTML, CLUE_TEXT, 2, "<nav"},
    {LANG_HTML, CLUE_TEXT, 2, "<section"}, {LANG_HTML, CLUE_TEXT, 2, "<footer"}, {LANG_HTML, CLUE_TEXT, 2, "<header"}, {LANG_HTML, CLUE_TEXT, 2, "<td"},
    {LANG_HTML, CLUE_TEXT, 2, "<tr"}, {LANG_HTML, CLUE_TEXT, 2, "<code"}, {LANG_HTML, CLUE_TEXT, 2, "<pre"}, {LANG_HTML, CLUE_TEXT, 2, "<svg"},
    {LANG_HTML, CLUE_TEXT, 2, "{% "}, {LANG_HTML, CLUE_TEXT, 2, "{{ "},
    /* ---- CSS ---- */
    {LANG_CSS, CLUE_TEXT, 3, "px;"}, {LANG_CSS, CLUE_LINE_START, 3, "color:"}, {LANG_CSS, CLUE_LINE_START, 3, "margin"}, {LANG_CSS, CLUE_LINE_START, 3, "padding"},
    {LANG_CSS, CLUE_LINE_START, 3, "@media"}, {LANG_CSS, CLUE_LINE_START, 3, "display:"}, {LANG_CSS, CLUE_LINE_START, 3, "font-"}, {LANG_CSS, CLUE_LINE_START, 2, "background"},
    {LANG_CSS, CLUE_TEXT, 2, "!important"}, {LANG_CSS, CLUE_LINE_START, 2, "border"}, {LANG_CSS, CLUE_LINE_START, 2, "width:"}, {LANG_CSS, CLUE_LINE_START, 2, "height:"},
    {LANG_CSS, CLUE_TEXT, 2, "em;"}, {LANG_CSS, CLUE_TEXT, 2, "rgba("}, {LANG_CSS, CLUE_LINE_START, 2, "text-align:"}, {LANG_CSS, CLUE_LINE_START, 2, "position:"},
    {LANG_CSS, CLUE_LINE_START, 2, "cursor:"}, {LANG_CSS, CLUE_LINE_START, 2, "transition:"}, {LANG_CSS, CLUE_LINE_START, 2, "opacity:"}, {LANG_CSS, CLUE_LINE_START, 2, "--"},
    {LANG_CSS, CLUE_TEXT, 2, ":hover"}, {LANG_CSS, CLUE_TEXT, 2, "var(--"}, {LANG_CSS, CLUE_LINE_START, 2, "@import"}, {LANG_CSS, CLUE_LINE_START, 2, "@font-face"},
    /* ---- SQL ---- */
    {LANG_SQL, CLUE_WORD_ANYCASE, 3, "select"}, {LANG_SQL, CLUE_WORD_ANYCASE, 3, "from"}, {LANG_SQL, CLUE_WORD_ANYCASE, 3, "where"}, {LANG_SQL, CLUE_TEXT, 4, "INSERT INTO"},
    {LANG_SQL, CLUE_TEXT, 4, "CREATE TABLE"}, {LANG_SQL, CLUE_TEXT, 4, "create table"}, {LANG_SQL, CLUE_WORD_ANYCASE, 2, "join"}, {LANG_SQL, CLUE_TEXT, 3, "VALUES"},
    {LANG_SQL, CLUE_TEXT, 3, "PRIMARY KEY"}, {LANG_SQL, CLUE_WORD_ANYCASE, 2, "varchar"}, {LANG_SQL, CLUE_TEXT, 3, "GROUP BY"}, {LANG_SQL, CLUE_TEXT, 3, "ORDER BY"},
    {LANG_SQL, CLUE_LINE_START, 2, "--"}, {LANG_SQL, CLUE_WORD_ANYCASE, 2, "update"}, {LANG_SQL, CLUE_WORD_ANYCASE, 2, "delete"}, {LANG_SQL, CLUE_LINE_END, 1, ";"},
    {LANG_SQL, CLUE_TEXT, 3, "CREATE "}, {LANG_SQL, CLUE_TEXT, 3, "DROP "}, {LANG_SQL, CLUE_TEXT, 3, "SELECT "}, {LANG_SQL, CLUE_TEXT, 2, "BEGIN;"},
    /* ---- Shell ---- */
    {LANG_SHELL, CLUE_SHEBANG, 9, "sh"}, {LANG_SHELL, CLUE_SHEBANG, 9, "bash"}, {LANG_SHELL, CLUE_LINE_START, 3, "echo "}, {LANG_SHELL, CLUE_LINE_START, 3, "fi"},
    {LANG_SHELL, CLUE_TEXT, 3, "; then"}, {LANG_SHELL, CLUE_LINE_START, 2, "then"}, {LANG_SHELL, CLUE_LINE_START, 3, "done"}, {LANG_SHELL, CLUE_TEXT, 3, "; do"},
    {LANG_SHELL, CLUE_TEXT, 3, "$("}, {LANG_SHELL, CLUE_TEXT, 2, "${"}, {LANG_SHELL, CLUE_WORD, 3, "esac"}, {LANG_SHELL, CLUE_LINE_START, 2, "export "},
    {LANG_SHELL, CLUE_TEXT, 2, "[ -"}, {LANG_SHELL, CLUE_TEXT, 2, "[[ "}, {LANG_SHELL, CLUE_TEXT, 2, " 2>&1"}, {LANG_SHELL, CLUE_TEXT, 2, "$1"},
    {LANG_SHELL, CLUE_TEXT, 2, "\"$@\""}, {LANG_SHELL, CLUE_LINE_START, 2, "#"}, {LANG_SHELL, CLUE_LINE_START, 2, "set -e"},
    /* ---- PHP ---- */
    {LANG_PHP, CLUE_TEXT, 9, "<?php"}, {LANG_PHP, CLUE_SHEBANG, 9, "php"}, {LANG_PHP, CLUE_TEXT, 4, "$this->"}, {LANG_PHP, CLUE_TEXT, 2, "->"}, {LANG_PHP, CLUE_WORD, 2, "function"},
    {LANG_PHP, CLUE_LINE_START, 2, "echo "}, {LANG_PHP, CLUE_TEXT, 3, "array("}, {LANG_PHP, CLUE_TEXT, 2, "$_"}, {LANG_PHP, CLUE_TEXT, 2, "=> "},
    {LANG_PHP, CLUE_LINE_START, 3, "namespace "}, {LANG_PHP, CLUE_LINE_START, 3, "use "}, {LANG_PHP, CLUE_WORD, 2, "public"},
    /* ---- Ruby ---- */
    {LANG_RUBY, CLUE_LINE_START, 3, "def "}, {LANG_RUBY, CLUE_LINE_START, 4, "end"}, {LANG_RUBY, CLUE_LINE_END, 3, " do"}, {LANG_RUBY, CLUE_TEXT, 3, "#{"},
    {LANG_RUBY, CLUE_TEXT, 2, ".new"}, {LANG_RUBY, CLUE_TEXT, 2, "nil?"}, {LANG_RUBY, CLUE_LINE_START, 3, "require "}, {LANG_RUBY, CLUE_LINE_START, 3, "require_relative"},
    {LANG_RUBY, CLUE_TEXT, 3, " do |"}, {LANG_RUBY, CLUE_TEXT, 3, ".each"}, {LANG_RUBY, CLUE_LINE_START, 3, "puts "}, {LANG_RUBY, CLUE_TEXT, 3, "attr_"},
    {LANG_RUBY, CLUE_LINE_START, 3, "module "}, {LANG_RUBY, CLUE_LINE_START, 2, "class "}, {LANG_RUBY, CLUE_WORD, 2, "unless"}, {LANG_RUBY, CLUE_WORD, 2, "nil"},
    {LANG_RUBY, CLUE_TEXT, 2, "@"}, {LANG_RUBY, CLUE_SHEBANG, 9, "ruby"}, {LANG_RUBY, CLUE_LINE_START, 2, "#"}, {LANG_RUBY, CLUE_WORD, 2, "elsif"},
    {LANG_RUBY, CLUE_TEXT, 2, " => "}, {LANG_RUBY, CLUE_TEXT, 2, ":symbol"},
    /* ---- Kotlin ---- */
    {LANG_KOTLIN, CLUE_LINE_START, 4, "fun "}, {LANG_KOTLIN, CLUE_LINE_START, 3, "val "}, {LANG_KOTLIN, CLUE_LINE_START, 2, "var "}, {LANG_KOTLIN, CLUE_LINE_START, 4, "import kotlin"},
    {LANG_KOTLIN, CLUE_TEXT, 3, "println("}, {LANG_KOTLIN, CLUE_LINE_START, 3, "package "}, {LANG_KOTLIN, CLUE_TEXT, 3, "data class"}, {LANG_KOTLIN, CLUE_WORD, 3, "when"},
    {LANG_KOTLIN, CLUE_TEXT, 2, "?."}, {LANG_KOTLIN, CLUE_TEXT, 2, "!!"}, {LANG_KOTLIN, CLUE_WORD, 2, "override"}, {LANG_KOTLIN, CLUE_TEXT, 2, ": String"},
    {LANG_KOTLIN, CLUE_TEXT, 2, ": Int"},
    /* ---- Swift ---- */
    {LANG_SWIFT, CLUE_LINE_START, 4, "func "}, {LANG_SWIFT, CLUE_LINE_START, 4, "import Foundation"}, {LANG_SWIFT, CLUE_LINE_START, 4, "import UIKit"},
    {LANG_SWIFT, CLUE_LINE_START, 4, "import SwiftUI"}, {LANG_SWIFT, CLUE_WORD, 3, "guard"}, {LANG_SWIFT, CLUE_LINE_START, 2, "let "}, {LANG_SWIFT, CLUE_LINE_START, 2, "var "},
    {LANG_SWIFT, CLUE_TEXT, 2, "print("}, {LANG_SWIFT, CLUE_TEXT, 3, "-> "}, {LANG_SWIFT, CLUE_TEXT, 2, "?? "}, {LANG_SWIFT, CLUE_WORD, 2, "struct"},
    {LANG_SWIFT, CLUE_TEXT, 3, ": View"}, {LANG_SWIFT, CLUE_TEXT, 2, "@State"},
    /* ---- Assembly (x86 and 68k) ---- */
    {LANG_ASM, CLUE_LINE_START_ANYCASE, 3, "mov"}, {LANG_ASM, CLUE_LINE_START_ANYCASE, 3, "move."}, {LANG_ASM, CLUE_TEXT, 4, "section .text"}, {LANG_ASM, CLUE_TEXT, 4, "section .data"},
    {LANG_ASM, CLUE_LINE_START, 4, "global _start"}, {LANG_ASM, CLUE_LINE_START_ANYCASE, 3, "syscall"}, {LANG_ASM, CLUE_TEXT, 3, "int 0x80"}, {LANG_ASM, CLUE_WORD_ANYCASE, 2, "eax"},
    {LANG_ASM, CLUE_WORD_ANYCASE, 2, "rax"}, {LANG_ASM, CLUE_WORD_ANYCASE, 2, "rsp"}, {LANG_ASM, CLUE_LINE_START_ANYCASE, 2, "jmp"}, {LANG_ASM, CLUE_LINE_START_ANYCASE, 2, "push"},
    {LANG_ASM, CLUE_LINE_START_ANYCASE, 2, "pop"}, {LANG_ASM, CLUE_LINE_START_ANYCASE, 2, "lea"}, {LANG_ASM, CLUE_LINE_START_ANYCASE, 2, "ret"}, {LANG_ASM, CLUE_LINE_START_ANYCASE, 2, "call"},
    {LANG_ASM, CLUE_LINE_START_ANYCASE, 2, "cmp"}, {LANG_ASM, CLUE_LINE_START_ANYCASE, 2, "dc.b"}, {LANG_ASM, CLUE_LINE_START, 3, ".globl"}, {LANG_ASM, CLUE_LINE_START, 3, ".text"},
    {LANG_ASM, CLUE_LINE_START, 2, ".section"}, {LANG_ASM, CLUE_LINE_START, 2, ".align"}, {LANG_ASM, CLUE_TEXT, 2, "%rsp"}, {LANG_ASM, CLUE_TEXT, 2, "%eax"},
    {LANG_ASM, CLUE_LINE_START, 2, ";"}, {LANG_ASM, CLUE_LINE_START_ANYCASE, 2, "add"}, {LANG_ASM, CLUE_LINE_START_ANYCASE, 2, "sub"}, {LANG_ASM, CLUE_LINE_START_ANYCASE, 2, "bne"},
    {LANG_ASM, CLUE_LINE_START_ANYCASE, 2, "beq"}, {LANG_ASM, CLUE_LINE_START, 2, ".type"}, {LANG_ASM, CLUE_LINE_START, 2, ".size"}, {LANG_ASM, CLUE_LINE_START_ANYCASE, 2, "xor"},
    {LANG_ASM, CLUE_LINE_START_ANYCASE, 2, "movq"}, {LANG_ASM, CLUE_LINE_START_ANYCASE, 2, "movl"}, {LANG_ASM, CLUE_LINE_START_ANYCASE, 2, "movb"}, {LANG_ASM, CLUE_LINE_START_ANYCASE, 2, "leaq"},
    {LANG_ASM, CLUE_LINE_START_ANYCASE, 2, "addq"}, {LANG_ASM, CLUE_LINE_START_ANYCASE, 2, "subq"}, {LANG_ASM, CLUE_LINE_START_ANYCASE, 2, "cmpq"}, {LANG_ASM, CLUE_LINE_START_ANYCASE, 2, "testq"},
    {LANG_ASM, CLUE_LINE_START_ANYCASE, 2, "jne"}, {LANG_ASM, CLUE_LINE_START_ANYCASE, 2, "je"}, {LANG_ASM, CLUE_LINE_START_ANYCASE, 2, "jz"}, {LANG_ASM, CLUE_LINE_START_ANYCASE, 2, "jnz"},
    {LANG_ASM, CLUE_LINE_START_ANYCASE, 2, "jl"}, {LANG_ASM, CLUE_LINE_START_ANYCASE, 2, "jg"}, {LANG_ASM, CLUE_LINE_START_ANYCASE, 2, "pushq"}, {LANG_ASM, CLUE_LINE_START_ANYCASE, 2, "popq"},
    {LANG_ASM, CLUE_LINE_START_ANYCASE, 2, "xorl"}, {LANG_ASM, CLUE_LINE_START_ANYCASE, 2, "andq"}, {LANG_ASM, CLUE_LINE_START_ANYCASE, 2, "shlq"}, {LANG_ASM, CLUE_LINE_START_ANYCASE, 2, "shrq"},
    {LANG_ASM, CLUE_LINE_START_ANYCASE, 2, "ldr"}, {LANG_ASM, CLUE_LINE_START_ANYCASE, 2, "str"}, {LANG_ASM, CLUE_LINE_START_ANYCASE, 2, "bl"}, {LANG_ASM, CLUE_LINE_START_ANYCASE, 2, "stp"},
    {LANG_ASM, CLUE_LINE_START_ANYCASE, 2, "ldp"}, {LANG_ASM, CLUE_LINE_START_ANYCASE, 2, "nop"}, {LANG_ASM, CLUE_LINE_START_ANYCASE, 2, "inc"}, {LANG_ASM, CLUE_LINE_START_ANYCASE, 2, "dec"},
    {LANG_ASM, CLUE_LINE_START_ANYCASE, 2, "trap"}, {LANG_ASM, CLUE_LINE_START_ANYCASE, 2, "rts"}, {LANG_ASM, CLUE_LINE_START_ANYCASE, 2, "bra"}, {LANG_ASM, CLUE_LINE_START_ANYCASE, 2, "jsr"},
    {LANG_ASM, CLUE_LINE_START_ANYCASE, 2, "dc.l"}, {LANG_ASM, CLUE_LINE_START_ANYCASE, 2, "dc.w"}, {LANG_ASM, CLUE_LINE_START_ANYCASE, 2, "org"},
    {LANG_ASM, CLUE_LINE_START, 2, ".cfi_"}, {LANG_ASM, CLUE_LINE_START, 2, ".long"}, {LANG_ASM, CLUE_LINE_START, 2, ".quad"}, {LANG_ASM, CLUE_LINE_START, 2, ".byte"},
    {LANG_ASM, CLUE_LINE_START, 2, ".p2align"}, {LANG_ASM, CLUE_LINE_START, 2, ".data"}, {LANG_ASM, CLUE_LINE_START, 2, ".file"}, {LANG_ASM, CLUE_LINE_START, 2, ".word"},
    {LANG_ASM, CLUE_LINE_START, 3, "TEXT "}, {LANG_ASM, CLUE_TEXT, 3, "(SB)"}, {LANG_ASM, CLUE_TEXT, 3, "(FP)"}, {LANG_ASM, CLUE_TEXT, 2, "(%r"},
    {LANG_ASM, CLUE_TEXT, 2, ", $"}, {LANG_ASM, CLUE_TEXT, 1, ",%"}, {LANG_ASM, CLUE_LINE_START, 2, "ENTRY("}, {LANG_ASM, CLUE_LINE_START, 2, "END("},
    /* ---- Lua ---- */
    {LANG_LUA, CLUE_LINE_START, 4, "local "}, {LANG_LUA, CLUE_TEXT, 3, "local function"}, {LANG_LUA, CLUE_WORD, 2, "function"}, {LANG_LUA, CLUE_LINE_START, 2, "end"},
    {LANG_LUA, CLUE_WORD, 2, "then"}, {LANG_LUA, CLUE_WORD, 3, "elseif"}, {LANG_LUA, CLUE_WORD, 2, "nil"}, {LANG_LUA, CLUE_LINE_START, 2, "--"},
    {LANG_LUA, CLUE_TEXT, 3, "~="}, {LANG_LUA, CLUE_TEXT, 3, "require(\""}, {LANG_LUA, CLUE_TEXT, 3, "require \""}, {LANG_LUA, CLUE_TEXT, 2, " .. "},
    {LANG_LUA, CLUE_WORD, 2, "pairs"}, {LANG_LUA, CLUE_WORD, 2, "ipairs"}, {LANG_LUA, CLUE_LINE_START, 2, "return "},
    /* ---- Makefile ---- */
    {LANG_MAKE, CLUE_TEXT, 4, "$(CC)"}, {LANG_MAKE, CLUE_TEXT, 3, "$(CFLAGS)"}, {LANG_MAKE, CLUE_LINE_START, 4, ".PHONY"}, {LANG_MAKE, CLUE_TEXT, 3, "$@"},
    {LANG_MAKE, CLUE_TEXT, 3, "$<"}, {LANG_MAKE, CLUE_TEXT, 3, "$^"}, {LANG_MAKE, CLUE_LINE_START, 2, "all:"}, {LANG_MAKE, CLUE_LINE_START, 2, "clean:"},
    {LANG_MAKE, CLUE_TEXT, 3, " := "}, {LANG_MAKE, CLUE_TEXT, 2, " ?= "}, {LANG_MAKE, CLUE_TEXT, 2, "$("}, {LANG_MAKE, CLUE_LINE_START, 2, "include "},
    {LANG_MAKE, CLUE_LINE_START, 2, "ifeq"}, {LANG_MAKE, CLUE_LINE_START, 2, "endif"}, {LANG_MAKE, CLUE_TEXT, 3, "@echo"}, {LANG_MAKE, CLUE_TEXT, 3, "$(MAKE)"},
    {LANG_MAKE, CLUE_TEXT, 3, " += "}, {LANG_MAKE, CLUE_LINE_START, 2, "-include"}, {LANG_MAKE, CLUE_TEXT, 3, "%.o"}, {LANG_MAKE, CLUE_TEXT, 2, "$(RM)"},
    {LANG_MAKE, CLUE_LINE_START, 2, "ifdef"}, {LANG_MAKE, CLUE_LINE_START, 2, "ifneq"}, {LANG_MAKE, CLUE_LINE_START, 2, "install:"},
    /* ---- the C family's comment styles (Python, Ruby, Shell... never use these) ---- */
    {LANG_C, CLUE_LINE_START, 2, "//"}, {LANG_CPP, CLUE_LINE_START, 2, "//"}, {LANG_CSHARP, CLUE_LINE_START, 2, "//"}, {LANG_JAVA, CLUE_LINE_START, 2, "//"},
    {LANG_JS, CLUE_LINE_START, 2, "//"}, {LANG_TS, CLUE_LINE_START, 2, "//"}, {LANG_RUST, CLUE_LINE_START, 2, "//"}, {LANG_GO, CLUE_LINE_START, 2, "//"},
    {LANG_KOTLIN, CLUE_LINE_START, 2, "//"}, {LANG_SWIFT, CLUE_LINE_START, 2, "//"}, {LANG_PHP, CLUE_LINE_START, 1, "//"},
    {LANG_C, CLUE_LINE_START, 2, "/*"}, {LANG_CPP, CLUE_LINE_START, 2, "/*"}, {LANG_JAVA, CLUE_LINE_START, 2, "/*"}, {LANG_JS, CLUE_LINE_START, 1, "/*"},
    {LANG_CSS, CLUE_LINE_START, 2, "/*"}, {LANG_CSHARP, CLUE_LINE_START, 1, "/*"}, {LANG_GO, CLUE_LINE_START, 1, "/*"},
    /* ---- Markdown ---- */
    {LANG_MARKDOWN, CLUE_LINE_START, 3, "# "}, {LANG_MARKDOWN, CLUE_LINE_START, 3, "## "}, {LANG_MARKDOWN, CLUE_LINE_START, 3, "### "}, {LANG_MARKDOWN, CLUE_LINE_START, 2, "- "},
    {LANG_MARKDOWN, CLUE_LINE_START, 1, "* "}, {LANG_MARKDOWN, CLUE_LINE_START, 3, "```"}, {LANG_MARKDOWN, CLUE_TEXT, 3, "]("}, {LANG_MARKDOWN, CLUE_TEXT, 2, "**"},
    {LANG_MARKDOWN, CLUE_LINE_START, 2, "> "}, {LANG_MARKDOWN, CLUE_LINE_START, 2, "1. "}, {LANG_MARKDOWN, CLUE_TEXT, 2, "`"}, {LANG_MARKDOWN, CLUE_LINE_START, 2, "| "},
    /* ---- JSON ---- */
    {LANG_JSON, CLUE_TEXT, 3, "\": "}, {LANG_JSON, CLUE_TEXT, 2, "\":"}, {LANG_JSON, CLUE_LINE_END, 2, "\","}, {LANG_JSON, CLUE_LINE_END, 2, "},"},
    {LANG_JSON, CLUE_LINE_END, 1, "["}, {LANG_JSON, CLUE_LINE_END, 1, "{"}, {LANG_JSON, CLUE_WORD, 1, "true"}, {LANG_JSON, CLUE_WORD, 1, "false"},
    /* ---- YAML ---- */
    {LANG_YAML, CLUE_LINE_START, 3, "---"}, {LANG_YAML, CLUE_LINE_START, 2, "- name:"}, {LANG_YAML, CLUE_LINE_END, 2, ":"}, {LANG_YAML, CLUE_TEXT, 2, ": "},
    {LANG_YAML, CLUE_LINE_START, 1, "- "}, {LANG_YAML, CLUE_LINE_START, 1, "#"},
};
#define CLUE_COUNT ((int)(sizeof CLUES / sizeof CLUES[0]))

/* File endings, each worth a few points to its language. */
static const struct { const char *extension; int lang; } EXTENSIONS[] = {
    {"c", LANG_C}, {"h", LANG_C}, {"cpp", LANG_CPP}, {"cc", LANG_CPP}, {"cxx", LANG_CPP}, {"hpp", LANG_CPP}, {"hh", LANG_CPP},
    {"cs", LANG_CSHARP}, {"java", LANG_JAVA}, {"py", LANG_PYTHON}, {"pyw", LANG_PYTHON}, {"js", LANG_JS}, {"mjs", LANG_JS},
    {"cjs", LANG_JS}, {"jsx", LANG_JS}, {"ts", LANG_TS}, {"tsx", LANG_TS}, {"mts", LANG_TS}, {"rs", LANG_RUST}, {"go", LANG_GO},
    {"html", LANG_HTML}, {"htm", LANG_HTML}, {"css", LANG_CSS}, {"sql", LANG_SQL}, {"sh", LANG_SHELL}, {"bash", LANG_SHELL},
    {"php", LANG_PHP}, {"rb", LANG_RUBY}, {"kt", LANG_KOTLIN}, {"kts", LANG_KOTLIN}, {"swift", LANG_SWIFT}, {"s", LANG_ASM},
    {"asm", LANG_ASM}, {"x68", LANG_ASM}, {"lua", LANG_LUA}, {"mk", LANG_MAKE}, {"md", LANG_MARKDOWN}, {"markdown", LANG_MARKDOWN},
    {"json", LANG_JSON}, {"yml", LANG_YAML}, {"yaml", LANG_YAML}, {"txt", LANG_TEXT},
};

#define LANG_SCAN_BYTES (48 * 1024)  /* only the start of a big file is read: plenty to tell */

static bool WordChar(char ch) { return isalnum((unsigned char)ch) || ch == '_'; }

/* Is this text anywhere in the first length bytes? */
static bool ContainsText(const char *data, int length, const char *text)
{
    int textLength = (int)strlen(text);
    for (int i = 0; i + textLength <= length; i++) if (data[i] == text[0] && memcmp(data + i, text, (size_t)textLength) == 0) return true;
    return false;
}

/* Same letters, ignoring capitals (strncasecmp isn't on every compiler). */
static bool MatchesIgnoreCase(const char *a, const char *b, int length)
{
    for (int i = 0; i < length; i++) if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) return false;
    return true;
}

/* Points for each language, from the content alone. */
static void ScoreLanguages(const char *data, int length, unsigned short scores[LANG_COUNT])
{
    int scanLength = length < LANG_SCAN_BYTES ? length : LANG_SCAN_BYTES;

    /* Markdown code blocks (between ``` lines) are examples quoted in the
       document, not what the file itself is written in: blank them out,
       keeping the ``` lines themselves as a clue that this is Markdown. */
    char *withoutCodeBlocks = NULL;
    bool hasFence = false;
    for (int i = 0; i + 2 < scanLength && !hasFence; i++) if (data[i] == '`' && data[i + 1] == '`' && data[i + 2] == '`') hasFence = true;
    if (hasFence && (withoutCodeBlocks = malloc((size_t)scanLength + 1)) != NULL) {
        bool inFence = false;
        int cleanLength = 0;
        for (int fenceLineStart = 0; fenceLineStart < scanLength; ) {
            int lineEnd = fenceLineStart;
            while (lineEnd < scanLength && data[lineEnd] != '\n') lineEnd++;
            int textStart = fenceLineStart;
            while (textStart < lineEnd && (data[textStart] == ' ' || data[textStart] == '\t')) textStart++;
            bool isFence = lineEnd - textStart >= 3 && ((data[textStart] == '`' && data[textStart + 1] == '`' && data[textStart + 2] == '`') || (data[textStart] == '~' && data[textStart + 1] == '~' && data[textStart + 2] == '~'));
            if (isFence) inFence = !inFence;
            if (isFence || !inFence) { memcpy(withoutCodeBlocks + cleanLength, data + fenceLineStart, (size_t)(lineEnd - fenceLineStart)); cleanLength += lineEnd - fenceLineStart; }
            if (lineEnd < scanLength) withoutCodeBlocks[cleanLength++] = '\n';
            fenceLineStart = lineEnd + 1;
        }
        withoutCodeBlocks[cleanLength] = 0;
        data = withoutCodeBlocks;
        scanLength = cleanLength;
    }
    int clueHits[sizeof CLUES / sizeof CLUES[0]];
    memset(clueHits, 0, sizeof clueHits);
    #define COUNT_HIT(i) do { if (clueHits[i] < 4) clueHits[i]++; } while (0)

    /* line by line. Lines that are C-style comments (starting with // or
       slash-star) only count as "this language uses these comments": their
       words are English, not code, so they're skipped for everything else. */
    int lineStart = 0, lineNumber = 0, yamlKeys = 0, makeVars = 0, tabLines = 0;
    bool inBlockComment = false;
    while (lineStart < scanLength) {
        int lineEnd = lineStart;
        while (lineEnd < scanLength && data[lineEnd] != '\n') lineEnd++;
        int textStart = lineStart;
        while (textStart < lineEnd && (data[textStart] == ' ' || data[textStart] == '\t')) textStart++;
        int textEnd = lineEnd;
        while (textEnd > textStart && (data[textEnd - 1] == ' ' || data[textEnd - 1] == '\t' || data[textEnd - 1] == '\r')) textEnd--;
        bool isComment = false, opensBlockComment = false;
        if (inBlockComment) {
            isComment = true;
            for (int k = textStart; k + 1 < textEnd; k++) if (data[k] == '*' && data[k + 1] == '/') { inBlockComment = false; break; }
        } else if (textEnd - textStart >= 2 && data[textStart] == '/' && (data[textStart + 1] == '/' || data[textStart + 1] == '*')) {
            isComment = true;
            if (data[textStart + 1] == '*') {
                opensBlockComment = true;
                for (int k = textStart + 2; k + 1 < textEnd; k++) if (data[k] == '*' && data[k + 1] == '/') { opensBlockComment = false; break; }
            }
        }
        if (lineStart < scanLength && data[lineStart] == '\t' && textEnd > textStart) tabLines++;

        for (int i = 0; i < CLUE_COUNT; i++) {
            const Clue *clue = &CLUES[i];
            if (isComment && !(clue->kind == CLUE_LINE_START && clue->text[0] == '/')) continue;
            int clueLength;
            switch (clue->kind) {
            case CLUE_LINE_START: case CLUE_LINE_START_ANYCASE:
                clueLength = (int)strlen(clue->text);
                if (textEnd - textStart < clueLength) break;
                if (clue->kind == CLUE_LINE_START ? memcmp(data + textStart, clue->text, (size_t)clueLength) == 0 : MatchesIgnoreCase(data + textStart, clue->text, clueLength)) {
                    /* for word-like clues ("fi", "end", "mov") the next character mustn't continue the word */
                    char lastChar = clue->text[clueLength - 1], nextChar = textStart + clueLength < textEnd ? data[textStart + clueLength] : ' ';
                    if (!(WordChar(lastChar) && WordChar(nextChar))) COUNT_HIT(i);
                }
                break;
            case CLUE_LINE_END:
                clueLength = (int)strlen(clue->text);
                if (textEnd - textStart >= clueLength && memcmp(data + textEnd - clueLength, clue->text, (size_t)clueLength) == 0) COUNT_HIT(i);
                break;
            case CLUE_SHEBANG:
                if (lineNumber == 0 && lineEnd - lineStart > 2 && data[lineStart] == '#' && data[lineStart + 1] == '!') {
                    char firstLine[160];
                    snprintf(firstLine, sizeof firstLine, "%.*s", lineEnd - lineStart, data + lineStart);
                    if (strstr(firstLine, clue->text)) COUNT_HIT(i);
                }
                break;
            }
        }
        if (opensBlockComment) inBlockComment = true;

        if (!isComment) {
            /* whole words on this line */
            for (int i = textStart; i < textEnd; ) {
                if (!WordChar(data[i]) || (i > textStart && WordChar(data[i - 1]))) { i++; continue; }
                int wordEnd = i;
                while (wordEnd < textEnd && WordChar(data[wordEnd])) wordEnd++;
                int wordLength = wordEnd - i;
                for (int k = 0; k < CLUE_COUNT; k++) {
                    const Clue *clue = &CLUES[k];
                    if ((clue->kind != CLUE_WORD && clue->kind != CLUE_WORD_ANYCASE) || clueHits[k] >= 4) continue;
                    if ((int)strlen(clue->text) != wordLength) continue;
                    if (clue->kind == CLUE_WORD ? memcmp(data + i, clue->text, (size_t)wordLength) == 0 : MatchesIgnoreCase(data + i, clue->text, wordLength)) COUNT_HIT(k);
                }
                i = wordEnd;
            }
            /* YAML: "key: value" or "key:" lines (also "- key: value") */
            int k = textStart;
            if (k + 1 < textEnd && data[k] == '-' && data[k + 1] == ' ') k += 2;
            int keyStart = k;
            while (k < textEnd && (WordChar(data[k]) || data[k] == '-' || data[k] == '.')) k++;
            if (k > keyStart && k < textEnd && data[k] == ':' && (k + 1 == textEnd || data[k + 1] == ' ') && isalpha((unsigned char)data[keyStart])) {
                char lastChar = data[textEnd - 1];
                bool isPythonWord = (k - keyStart == 4 && memcmp(data + keyStart, "else", 4) == 0) || (k - keyStart == 3 && memcmp(data + keyStart, "try", 3) == 0);
                if (!isPythonWord && lastChar != '{' && lastChar != ';' && lastChar != ',' && lastChar != ')') yamlKeys++;
            }
            /* Make: $(NAME) with a capital-letter name */
            for (int at = textStart; at + 3 < textEnd; at++)
                if (data[at] == '$' && data[at + 1] == '(' && isupper((unsigned char)data[at + 2])) {
                    int nameEnd = at + 2;
                    while (nameEnd < textEnd && (isupper((unsigned char)data[nameEnd]) || data[nameEnd] == '_' || isdigit((unsigned char)data[nameEnd]))) nameEnd++;
                    if (nameEnd < textEnd && data[nameEnd] == ')') makeVars++;
                }
        }
        lineStart = lineEnd + 1;
        lineNumber++;
    }

    /* text anywhere (memmem isn't everywhere, so a simple search) */
    for (int k = 0; k < CLUE_COUNT; k++) {
        const Clue *clue = &CLUES[k];
        if (clue->kind != CLUE_TEXT) continue;
        int clueLength = (int)strlen(clue->text);
        for (int i = 0; i + clueLength <= scanLength && clueHits[k] < 4; i++)
            if (data[i] == clue->text[0] && memcmp(data + i, clue->text, (size_t)clueLength) == 0) { COUNT_HIT(k); i += clueLength - 1; }
    }
    #undef COUNT_HIT

    int points[LANG_COUNT] = {0};
    for (int k = 0; k < CLUE_COUNT; k++) points[CLUES[k].lang] += clueHits[k] * CLUES[k].weight;

    /* the special counters */
    points[LANG_YAML] += 2 * (yamlKeys < 6 ? yamlKeys : 6);
    points[LANG_MAKE] += 3 * (makeVars < 5 ? makeVars : 5) + (tabLines < 4 ? tabLines : 4);
    /* a few markers that settle it */
    if (ContainsText(data, scanLength, "<!DOCTYPE html") || ContainsText(data, scanLength, "<!doctype html") || ContainsText(data, scanLength, "<html")) points[LANG_HTML] += 20;
    for (int i = 0; i + 5 <= scanLength; i++)                 /* <?php starting a line (not just mentioned) */
        if ((i == 0 || data[i - 1] == '\n') && memcmp(data + i, "<?php", 5) == 0) { points[LANG_PHP] += 40; break; }

    /* JSON has to start like JSON: { then a "name" (or }), or [ then a value */
    int at = 0;
    while (at < scanLength && isspace((unsigned char)data[at])) at++;
    bool looksLikeJson = false;
    if (at < scanLength && (data[at] == '{' || data[at] == '[')) {
        int afterBracket = at + 1;
        while (afterBracket < scanLength && isspace((unsigned char)data[afterBracket])) afterBracket++;
        char clue = afterBracket < scanLength ? data[afterBracket] : 0;
        looksLikeJson = data[at] == '{' ? (clue == '"' || clue == '}') : (clue == '{' || clue == '[' || clue == '"' || clue == ']' || clue == '-' || isdigit((unsigned char)clue));
    }
    if (!looksLikeJson) points[LANG_JSON] = 0;
    else points[LANG_JSON] += 10;

    for (int lang = 0; lang < LANG_COUNT; lang++) scores[lang] = (unsigned short)(points[lang] > 65000 ? 65000 : points[lang]);
    free(withoutCodeBlocks);
}

static int LangFromExtension(const char *name)
{
    const char *slash = strrchr(name, '/');                  /* just the file's own name, not its folders */
    if (slash) name = slash + 1;
    if (EqualsIgnoreCase(name, "Makefile") || EqualsIgnoreCase(name, "GNUmakefile")) return LANG_MAKE;
    const char *dot = strrchr(name, '.');
    if (!dot) return -1;
    for (int i = 0; i < (int)(sizeof EXTENSIONS / sizeof EXTENSIONS[0]); i++) if (EqualsIgnoreCase(dot + 1, EXTENSIONS[i].extension)) return EXTENSIONS[i].lang;
    return -1;
}

/* Picks the language from the content's points plus the file ending. */
static int PickLang(const unsigned short scores[LANG_COUNT], const char *name)
{
    int points[LANG_COUNT];
    for (int lang = 0; lang < LANG_COUNT; lang++) points[lang] = scores[lang];
    int extensionLang = LangFromExtension(name);
    if (extensionLang >= 0) points[extensionLang] += 8;       /* the ending counts, but content can outvote it */
    const char *baseName = strrchr(name, '/');
    baseName = baseName ? baseName + 1 : name;
    if (extensionLang == LANG_MAKE && !strchr(baseName, '.')) points[extensionLang] += 8;  /* a file called "Makefile" really is one */
    if (extensionLang == LANG_C) points[LANG_CPP] += 4;                                    /* a .h file could be either */
    if (extensionLang == LANG_TEXT) return LANG_TEXT;                                      /* .txt is always plain text */
    int best = -1;
    for (int lang = 0; lang < LANG_COUNT; lang++) if (best < 0 || points[lang] > points[best]) best = lang;
    /* C++ and TypeScript are "C plus extra" and "JavaScript plus types". C-style code in a
       .cpp file is C++ (that's how it gets compiled), and plain JavaScript in a .ts file is TypeScript */
    if (best == LANG_C && extensionLang == LANG_CPP) best = LANG_CPP;
    if (best == LANG_JS && extensionLang == LANG_TS) best = LANG_TS;
    /* otherwise only call it C++ if it clearly has C++-only things */
    if (best == LANG_CPP && points[LANG_C] >= points[LANG_CPP] - 3 && extensionLang != LANG_CPP) best = LANG_C;
    /* TypeScript is "JavaScript plus types": same idea */
    if (best == LANG_TS && points[LANG_JS] >= points[LANG_TS] - 3 && extensionLang != LANG_TS) best = LANG_JS;
    if (points[best] < 8) return LANG_TEXT;                   /* not enough clues for anything */
    return best;
}

/* Works out a file's language clues and line count into *cache. */
void FillLangCache(LangCache *cache, const char *data, int length, bool binary)
{
    cache->binary = binary;
    cache->lines = 0;
    if (!binary) {
        ScoreLanguages(data, length, cache->scores);
        cache->lines = length ? 1 : 0;
        for (int i = 0; i < length; i++) if (data[i] == '\n' && i + 1 < length) cache->lines++;
    }
    cache->done = true;
}
/* The language of a file from its clues and its name (LANG_BINARY for non-text files). */
int LangFromCache(LangCache *cache, const char *name) { return cache->binary ? LANG_BINARY : PickLang(cache->scores, name); }

/* ---------- remembering each file's language ----------
   Working it out means reading the file, so it's done once and kept:
   for committed versions (blobs never change) until the program closes,
   and for files you're editing until you next change them. */
/* A blob's language clues, worked out the first time they're needed. */
LangCache *GetBlobLangCache(Repo *repo, int blobIndex)
{
    Blob *blob = &repo->blobs[blobIndex];
    if (!blob->lang) {
        blob->lang = calloc(1, sizeof(LangCache));
        if (!blob->lang) return NULL;
        FillLangCache(blob->lang, blob->data, blob->length, blob->binary);
    }
    return blob->lang;
}
static LangCache *GetWorkLangCache(WorkFile *file)
{
    if (!file->lang) { file->lang = calloc(1, sizeof(LangCache)); if (!file->lang) return NULL; file->langVersion = file->version - 1; }
    if (file->langVersion != file->version) { FillLangCache(file->lang, file->text, file->length, file->binary); file->langVersion = file->version; }
    return file->lang;
}

/* The language of a file as you see it, and how many lines it has. */
int ViewFileLang(Repo *repo, ViewFile *viewFile, int *lineCount)
{
    LangCache *cache = viewFile->draftFile ? GetWorkLangCache(viewFile->draftFile) : viewFile->blob >= 0 ? GetBlobLangCache(repo, viewFile->blob) : NULL;
    if (lineCount) *lineCount = cache ? cache->lines : 0;
    return cache ? LangFromCache(cache, viewFile->name) : LANG_TEXT;
}

/* The name to show for a language ("Binary" for non-text files). */
const char *LangLabel(int lang) { return lang == LANG_BINARY ? "Binary" : LANG_NAMES[lang]; }

/* ---------- colours ----------
   Each language has its own fixed colour from the same colour-blind-checked
   set as the contributions pie, so Python looks the same in every repository.
   There are only 8 colours for 20 languages, so if two languages in one
   repository share a colour, the second takes the next free one. */
const Color LANG_PALETTE[8] = {
    {0x39, 0x87, 0xe5, 255}, {0xd9, 0x59, 0x26, 255}, {0x19, 0x9e, 0x70, 255}, {0xc9, 0x85, 0x00, 255},
    {0xd5, 0x51, 0x81, 255}, {0x00, 0x83, 0x00, 255}, {0x90, 0x85, 0xe9, 255}, {0xe6, 0x67, 0x67, 255}
};
const Color LANG_OTHER = {0x6e, 0x76, 0x81, 255};
static const unsigned char LANG_SLOT[LANG_FIRST_NONCODE] = {
    /* C */ 2, /* C++ */ 4, /* C# */ 5, /* Java */ 1, /* Python */ 0, /* JavaScript */ 3, /* TypeScript */ 0,
    /* Rust */ 1, /* Go */ 2, /* HTML */ 1, /* CSS */ 6, /* SQL */ 7, /* Shell */ 5, /* PHP */ 6, /* Ruby */ 7,
    /* Kotlin */ 6, /* Swift */ 1, /* Assembly */ 7, /* Lua */ 0, /* Makefile */ 3
};

/* ---------- the breakdown ---------- */

LangPart langParts[6];
int      langPartCount, langTotal;
char     langSkipped[TEXT_LEN];       /* the kinds of file that aren't counted, e.g. "Markdown, JSON" */

/* Counts the lines of code in each language: the latest commit, plus your
   own changes if userId can edit. Text that isn't code (Markdown, JSON, YAML,
   plain text) and binary files aren't counted - the same as GitHub. */

void BuildLanguages(Repo *repo, int userId)
{
    int linesPerLang[LANG_COUNT] = {0}, filesPerLang[LANG_COUNT] = {0};
    bool skipped[LANG_COUNT] = {0}, hasBinary = false;
    BuildView(repo, userId);
    for (int i = 0; i < viewFileCount; i++) {
        if (ViewDeleted(&viewFiles[i])) continue;
        int lineCount = 0, lang = ViewFileLang(repo, &viewFiles[i], &lineCount);
        if (lang == LANG_BINARY) { hasBinary = true; continue; }
        if (lang >= LANG_FIRST_NONCODE) { skipped[lang] = true; continue; }
        linesPerLang[lang] += lineCount;
        filesPerLang[lang]++;
    }
    BuildLangParts(linesPerLang, filesPerLang, skipped, hasBinary, false);
}

/* Turns lines and files per language into the parts drawn by DrawLanguageBar (and the
   profile's tech stack): biggest first, at most 6 (the rest are "Other"), each with its colour.
   counts = "files" are really commits (for the tech stack). */
bool langPartsAreCommits;
/* Turns lines and files per language into the parts drawn by the language bar and the tech stack
   pie (into langParts). */
void BuildLangParts(const int *lines, const int *files, const bool *skipped, bool binary, bool countsAreCommits)
{
    langPartsAreCommits = countsAreCommits;
    /* biggest first */
    int order[LANG_FIRST_NONCODE], langCount = 0;
    for (int lang = 0; lang < LANG_FIRST_NONCODE; lang++) if (files[lang]) order[langCount++] = lang;
    for (int pos = 0; pos < langCount; pos++)
        for (int other = pos + 1; other < langCount; other++)
            if (lines[order[other]] > lines[order[pos]]) { int swap = order[pos]; order[pos] = order[other]; order[other] = swap; }

    langPartCount = langTotal = 0;
    bool slotUsed[8] = {0};
    for (int i = 0; i < langCount; i++) {
        int lang = order[i];
        langTotal += lines[lang];
        if (langCount > 6 && i >= 5) {                    /* more than 6 languages: the smallest share "Other" */
            if (langPartCount == 5) langParts[langPartCount++] = (LangPart){-1, 0, 0, LANG_OTHER};
            langParts[5].lines += lines[lang];
            langParts[5].files += files[lang];
            continue;
        }
        int slot = LANG_SLOT[lang];
        for (int tries = 0; tries < 8 && slotUsed[slot]; tries++) slot = (slot + 1) % 8;
        slotUsed[slot] = true;
        langParts[langPartCount++] = (LangPart){lang, lines[lang], files[lang], LANG_PALETTE[slot]};
    }

    langSkipped[0] = 0;
    for (int lang = LANG_FIRST_NONCODE; lang < LANG_COUNT; lang++)
        if (skipped[lang]) snprintf(langSkipped + strlen(langSkipped), sizeof langSkipped - strlen(langSkipped), "%s%s", langSkipped[0] ? ", " : "", LANG_NAMES[lang]);
    if (binary) snprintf(langSkipped + strlen(langSkipped), sizeof langSkipped - strlen(langSkipped), "%s%s", langSkipped[0] ? ", " : "", "binary files");
}
