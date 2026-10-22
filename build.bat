@echo off
rem Builds minigithub.exe in one go with the compiler that comes with raylib.
rem (With make installed, "make" does the same but only recompiles what changed.)

cd /d "%~dp0"
set RAYLIB=C:\raylib\raylib\src
set PATH=C:\raylib\w64devkit\bin;%PATH%

gcc -std=c99 -O2 -Wall -Wextra ^
    src\*.c src\core\*.c src\data\*.c src\git\*.c src\ui\*.c src\screens\*.c ^
    -Isrc -Isrc\core -Isrc\data -Isrc\git -Isrc\ui -Isrc\screens -I%RAYLIB% ^
    -o minigithub.exe -L%RAYLIB% -lraylib -lopengl32 -lgdi32 -lwinmm
