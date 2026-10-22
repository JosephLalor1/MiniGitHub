# Mini GitHub - build with:  make        (or mingw32-make on Windows)
#
# Needs raylib 5.x or newer. On Windows the raylib installer puts it in
# C:\raylib; elsewhere, point RAYLIB at the folder holding raylib.h and
# libraylib.a, e.g.  make RAYLIB=/usr/local/lib
#
# Each .c file is compiled on its own into build/, so after a change only
# the files affected are compiled again.

RAYLIB ?= C:/raylib/raylib/src
CFLAGS ?= -std=c99 -O2 -Wall -Wextra
ifeq ($(origin CC),default)
    CC := gcc
endif

FOLDERS := core data git ui screens
SRC     := $(wildcard src/*.c) $(foreach f,$(FOLDERS),$(wildcard src/$(f)/*.c))
OBJ     := $(patsubst src/%.c,build/%.o,$(SRC))
INCLUDE := -Isrc $(addprefix -Isrc/,$(FOLDERS)) -I$(RAYLIB)

ifeq ($(OS),Windows_NT)
    TARGET := minigithub.exe
    LIBS   := -L$(RAYLIB) -lraylib -lopengl32 -lgdi32 -lwinmm
else
    TARGET := minigithub
    LIBS   := -L$(RAYLIB) -lraylib -lGL -lm -lpthread -ldl -lrt -lX11
endif

$(TARGET): $(OBJ)
	$(CC) $(OBJ) -o $@ $(LIBS)

# -MMD -MP also write build/*.d, which list the headers each file uses
build/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDE) -MMD -MP -c $< -o $@

clean:
	rm -rf build $(TARGET)

.PHONY: clean
-include $(OBJ:.o=.d)
