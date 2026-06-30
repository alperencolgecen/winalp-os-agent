# ============================================================
# WinAlp — Makefile
# Target: Windows x64, MinGW-w64 (gcc/g++)
# Usage : mingw32-make all | mingw32-make clean | mingw32-make run
# ============================================================

CC       = gcc
CXX      = g++
CFLAGS   = -std=c11 -O2 -Wall -Wextra -DWIN32_LEAN_AND_MEAN \
            -Iinclude -Ilib/raylib/include -Ilib/imgui
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra -DWIN32_LEAN_AND_MEAN \
            -Iinclude -Ilib/raylib/include -Ilib/imgui
LDFLAGS  = -Llib/raylib/lib \
            -lraylib -lopengl32 -lgdi32 -lwinmm \
            -lole32 -luuid -ldxgi -ld3d11 \
            -static -lstdc++ -lwinpthread

TARGET  = WinAlp.exe
BUILDDIR= build
SRCDIR  = src
LIBDIR  = lib

# C source files (project modules)
C_SRCS = $(SRCDIR)/main.c \
         $(SRCDIR)/logger.c \
         $(SRCDIR)/ai_engine.c \
         $(SRCDIR)/stt_engine.c \
         $(SRCDIR)/audio_capture.c \
         $(SRCDIR)/vision_engine.c \
         $(SRCDIR)/system_agent.c \
         $(SRCDIR)/memory_store.c \
         $(SRCDIR)/lua_runtime.c \
         $(SRCDIR)/pdf_reader.c \
         $(SRCDIR)/context_tracker.c \
         $(SRCDIR)/prompt_engine.c \
         $(SRCDIR)/plugin_manager.c

# C++ source files (project modules using C++ + ImGui, ImPlot, raylib backend)
CXX_SRCS = $(SRCDIR)/ui_render.cpp \
           $(LIBDIR)/imgui/imgui.cpp \
           $(LIBDIR)/imgui/imgui_draw.cpp \
           $(LIBDIR)/imgui/imgui_tables.cpp \
           $(LIBDIR)/imgui/imgui_widgets.cpp \
           $(LIBDIR)/imgui/imgui_demo.cpp \
           $(LIBDIR)/imgui/implot.cpp \
           $(LIBDIR)/imgui/implot_items.cpp \
           $(LIBDIR)/imgui/implot_demo.cpp \
           $(LIBDIR)/imgui/imgui_impl_raylib.cpp

C_OBJS = $(patsubst $(SRCDIR)/%.c, $(BUILDDIR)/%.o, $(C_SRCS))

# C++ objects: map src/%.cpp -> build/%.o and lib/imgui/%.cpp -> build/imgui/%.o
CXX_SRC_OBJS = $(patsubst $(SRCDIR)/%.cpp, $(BUILDDIR)/%.o, $(filter $(SRCDIR)/%.cpp, $(CXX_SRCS)))
CXX_LIB_OBJS = $(patsubst $(LIBDIR)/%.cpp, $(BUILDDIR)/%.o, $(filter $(LIBDIR)/%.cpp, $(CXX_SRCS)))
CXX_OBJS = $(CXX_SRC_OBJS) $(CXX_LIB_OBJS)
OBJS = $(C_OBJS) $(CXX_OBJS)

.PHONY: all clean run

all: $(BUILDDIR) $(TARGET)

$(BUILDDIR):
	mkdir -p $(BUILDDIR) $(BUILDDIR)/imgui

# C compilation
$(BUILDDIR)/%.o: $(SRCDIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

# C++ compilation (project modules in src/)
$(BUILDDIR)/%.o: $(SRCDIR)/%.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# C++ compilation (vendored libs in lib/imgui/)
$(BUILDDIR)/imgui/%.o: $(LIBDIR)/imgui/%.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(TARGET): $(OBJS)
	$(CXX) $(OBJS) -o $@ $(LDFLAGS)
	@echo "Build complete: $(TARGET)"

run: all
	./$(TARGET)

clean:
	rm -rf $(BUILDDIR) $(TARGET)

# ---- Test target (single-file raylib smoke test) ----
test_raylib: $(BUILDDIR)
	$(CC) $(CFLAGS) build/test_raylib.c -o build/test_raylib.exe $(LDFLAGS)
	@echo "Raylib test built: build/test_raylib.exe"
