This is an AI only project as part of the Advanced Programming Module, in collaboration between Joseph Lalor and Ismael Charaf Megrini

## Mini GitHub

A small GitHub-style app that runs entirely on your own computer: accounts, repositories, a built-in editor, commits, diffs, branches, merging, forks, issues, profiles, following other people, a dozen animated themes, and a real git server, so `git clone` and `git push` work against `http://localhost:7979`. Written in C99 with [raylib](https://www.raylib.com/).

### Building

With the raylib Windows installer (which puts raylib and a GCC compiler in `C:\raylib`):

```
make            # or mingw32-make; recompiles only what changed
build.bat       # or this, which compiles everything in one go
```

Either produces `minigithub.exe`. Elsewhere, point `RAYLIB` at the folder holding `raylib.h` and `libraylib.a`, for example `make RAYLIB=/usr/local/lib`.

The program keeps everything in `minigithub.dat` in the folder it's run from.

### Source layout

| Folder | What's in it |
| --- | --- |
| `src/main.c` | The window and the loop that runs every frame |
| `src/common.h` | Limits and headers every file uses |
| `src/core/` | Building blocks that know nothing about the rest of the program: SHA-1, SHA-256, zlib, JPEG, a byte buffer |
| `src/data/` | The data model and everything done to it: repositories, commits, drafts, branches, merging, forks, accounts, the save file |
| `src/git/` | The git server: reading and writing packs, taking in pushes, the web server |
| `src/ui/` | Interface pieces shared by the screens: widgets, the text editor, themes, animation, menus, navigation and Back |
| `src/screens/` | One file per page or repository tab |

Every `.c` file has a `.h` file next to it that describes the module and lists what other files may use.
