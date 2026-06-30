# ============================================================
# WinAlp — Makefile
# Target: Windows x64, MinGW-w64 (gcc/g++)
# Usage : mingw32-make all | mingw32-make clean | mingw32-make run
# ============================================================

CC       = gcc
CXX      = g++
INCS     = -Iinclude -Ilib/raylib/include -Ilib/imgui \
           -Ilib/whisper.cpp/include -Ilib/whisper.cpp/ggml/include \
           -Ilib/llama.cpp/include -Ilib/llama.cpp/ggml/include
CFLAGS   = -std=c11 -O2 -Wall -Wextra -DWIN32_LEAN_AND_MEAN $(INCS)
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra -DWIN32_LEAN_AND_MEAN $(INCS)
LDFLAGS  = -Llib/raylib/lib \
           -Llib/whisper -lwhisper \
           -Llib/llama -lllama -lggml -lggml-cpu -lggml-base \
           -lraylib -lopengl32 -lgdi32 -lwinmm -lole32 -luuid -ldxgi -ld3d11 \
           -lksuser \
           -static -lstdc++ -lwinpthread -fopenmp

TARGET  = WinAlp.exe
BUILDDIR= build
SRCDIR  = src
LIBDIR  = lib

# C source files (project modules)
C_SRCS = $(SRCDIR)/main.c \
         $(SRCDIR)/logger.c \
         $(SRCDIR)/ai_engine.c \
         $(SRCDIR)/audio_capture.c \
         $(SRCDIR)/vision_engine.c \
         $(SRCDIR)/system_agent.c \
         $(SRCDIR)/memory_store.c \
         $(SRCDIR)/lua_runtime.c \
         $(SRCDIR)/pdf_reader.c \
         $(SRCDIR)/context_tracker.c \
         $(SRCDIR)/prompt_engine.c \
         $(SRCDIR)/plugin_manager.c

# C++ source files (project modules + ImGui + ImPlot + raylib backend)
CXX_SRCS = $(SRCDIR)/ui_render.cpp \
           $(SRCDIR)/stt_engine.cpp \
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
CXX_SRC_OBJS = $(patsubst $(SRCDIR)/%.cpp, $(BUILDDIR)/%.o, $(filter $(SRCDIR)/%.cpp, $(CXX_SRCS)))
CXX_LIB_OBJS = $(patsubst $(LIBDIR)/%.cpp, $(BUILDDIR)/%.o, $(filter $(LIBDIR)/%.cpp, $(CXX_SRCS)))
ALL_OBJS = $(C_OBJS) $(CXX_SRC_OBJS) $(CXX_LIB_OBJS)

.PHONY: all clean run submodule-libs

all: submodule-libs $(BUILDDIR) $(TARGET)

submodule-libs:
	@test -f "$(LIBDIR)/whisper/libwhisper.a" || { \
	    echo "Building whisper.cpp static libraries..."; \
	    "scripts/build_whisper.bat"; \
	}
	@test -f "$(LIBDIR)/llama/libllama.a" || { \
	    echo "Building llama.cpp static libraries..."; \
	    "scripts/build_llama.bat"; \
	}

$(BUILDDIR):
	mkdir -p $(BUILDDIR)/imgui

$(BUILDDIR)/%.o: $(SRCDIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILDDIR)/%.o: $(SRCDIR)/%.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILDDIR)/imgui/%.o: $(LIBDIR)/imgui/%.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(TARGET): $(ALL_OBJS)
	$(CXX) $(ALL_OBJS) -o $@ $(LDFLAGS)
	@echo "Build complete: $(TARGET)"

run: all
	./$(TARGET)

clean:
	rm -rf $(BUILDDIR) $(TARGET)

# ---------- Download STT model (Whisper tiny, ~75MB) ----------
WHISPER_MODEL_URL = https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-tiny.bin
WHISPER_MODEL_PATH = models/ggml-tiny.bin

download-stt-model:
	powershell -Command "if (-not (Test-Path '$(WHISPER_MODEL_PATH)')) { \
	    Write-Host 'Downloading whisper tiny model...'; \
	    curl.exe -L -o '$(WHISPER_MODEL_PATH)' '$(WHISPER_MODEL_URL)'; \
	    Write-Host 'Downloaded: $(WHISPER_MODEL_PATH)'; \
	} else { \
	    Write-Host 'Model already exists: $(WHISPER_MODEL_PATH)'; \
	}"
