# ============================================================
# WinAlp — Makefile
# Target: Windows x64, MinGW-w64 (gcc)
# Usage : mingw32-make all | mingw32-make clean | mingw32-make run
# ============================================================

CC      = gcc
CFLAGS  = -std=c11 -O2 -Wall -Wextra -DWIN32_LEAN_AND_MEAN \
           -Iinclude -Ilib/raylib/include
LDFLAGS = -Llib/raylib/lib \
           -lraylib -lopengl32 -lgdi32 -lwinmm \
           -lole32 -luuid -ldxgi -ld3d11 \
           -static -lstdc++ -lwinpthread

TARGET  = WinAlp.exe
BUILDDIR= build
SRCDIR  = src

SRCS = $(SRCDIR)/main.c \
       $(SRCDIR)/logger.c \
       $(SRCDIR)/ai_engine.c \
       $(SRCDIR)/stt_engine.c \
       $(SRCDIR)/audio_capture.c \
       $(SRCDIR)/vision_engine.c \
       $(SRCDIR)/system_agent.c \
       $(SRCDIR)/memory_store.c \
       $(SRCDIR)/lua_runtime.c \
       $(SRCDIR)/ui_render.c \
       $(SRCDIR)/pdf_reader.c \
       $(SRCDIR)/context_tracker.c \
       $(SRCDIR)/prompt_engine.c \
       $(SRCDIR)/plugin_manager.c

OBJS = $(patsubst $(SRCDIR)/%.c, $(BUILDDIR)/%.o, $(SRCS))

.PHONY: all clean run

all: $(BUILDDIR) $(TARGET)

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

$(BUILDDIR)/%.o: $(SRCDIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)
	@echo "Build complete: $(TARGET)"

run: all
	./$(TARGET)

clean:
	rm -rf $(BUILDDIR) $(TARGET)

# ---- Test target (single-file raylib smoke test) ----
test_raylib: $(BUILDDIR)
	$(CC) $(CFLAGS) build/test_raylib.c -o build/test_raylib.exe $(LDFLAGS)
	@echo "Raylib test built: build/test_raylib.exe"
