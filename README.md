This is an AI only project as part of the Advanced Programming Module, in collaboration between Joseph Lalor and Ismael Charaf Megrini

## Mini GitHub

A small GitHub-style app that runs entirely on your own computer: accounts, repositories, a built-in editor, commits, diffs, branches, merging, forks, issues, profiles, following other people, a dozen animated themes, and a real git server, so `git clone` and `git push` work against `http://localhost:7979`. Written in C99 with [raylib](https://www.raylib.com/).

### Download

Get `minigithub.exe` from the [Releases](https://github.com/JosephLalor1/MiniGitHub/releases) page, put it in a folder of its own, and run it. It's a single file with nothing to install, and runs on Windows 10 and 11.

### What it does

- **Accounts:** sign up, sign in, change your password, and recover it with a one-time recovery code. Passwords are only ever stored as a salted PBKDF2-SHA256 hash.
- **Repositories:** public or private, with a built-in editor, uploads (drag files onto the window), commits, line-by-line diffs, branches, merging, forks with "Sync fork", issues, stars and collaborators. A repository's Settings tab makes it public or private, or deletes it.
- **Real git:** every repository is also a real git repository. `git clone`, `pull`, `fetch` and `push` work against the built-in server, using your Mini GitHub username and password. Press **Copy repo address** on a repository to get its address.
- **Profiles and people:** a picture, bio and theme, a contribution graph, and a tech-stack pie of the languages you've written. Follow other people to see their repositories under **Following** on your home page.
- **Home page:** your repositories, the ones of people you follow, or everyone's. The column about you tucks away behind your picture: hover the picture to see it, click it to keep it open.
- **Themes:** twelve animated backgrounds, from code rain and a night sky to aurora, synthwave and a rainy city. A splash screen shows one at random while the app opens.
- **Downloads:** any file, a whole repository as a .zip, or the files as they were in any commit.

### Your data

Everything is kept in `minigithub.dat` in the folder the program is run from, and saved automatically a moment after each change. Saving and loading happen on a background thread, so even a save file of hundreds of megabytes doesn't freeze the window.

A copy of the last file that loaded properly is kept as `minigithub.dat.last-good`. When a newer version of Mini GitHub upgrades an older save file, the old one is kept as `minigithub.dat.v<N>-backup`, for example `.v10-backup`. Older versions can't read the upgraded file, so use that backup if you need to go back.

### Building

With the raylib Windows installer (which puts raylib and a GCC compiler in `C:\raylib`):

```
make            # or mingw32-make; recompiles only what changed
build.bat       # or this, which compiles everything in one go
```

Either produces `minigithub.exe`. Elsewhere, point `RAYLIB` at the folder holding `raylib.h` and `libraylib.a`, for example `make RAYLIB=/usr/local/lib`.

### Source layout

| Folder | What's in it |
| --- | --- |
| `src/main.c` | The window and the loop that runs every frame |
| `src/common.h` | Limits and headers every file uses |
| `src/core/` | Building blocks that know nothing about the rest of the program: SHA-1, SHA-256, zlib, JPEG, a byte buffer, background threads |
| `src/data/` | The data model and everything done to it: repositories, commits, drafts, branches, merging, forks, accounts, following, the save file |
| `src/git/` | The git server: reading and writing packs, taking in pushes, the web server |
| `src/ui/` | Interface pieces shared by the screens: widgets, the text editor, themes, animation, menus, navigation and Back |
| `src/screens/` | One file per page or repository tab, plus the splash screen |

Every `.c` file has a `.h` file next to it that describes the module and lists what other files may use.
