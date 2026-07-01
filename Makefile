# ============================================================
# WinAlp — Makefile
# Target: Windows x64, MinGW-w64 (gcc/g++)
# Usage : mingw32-make all | mingw32-make clean | mingw32-make run
# ============================================================

CC       = gcc
CXX      = g++
INCS     = -Iinclude -Ilib/raylib/include -Ilib/imgui \
           -Ilib/whisper.cpp/include -Ilib/whisper.cpp/ggml/include \
           -Ilib/llama.cpp/include -Ilib/llama.cpp/ggml/include \
           -Ilib/lua/include \
           -Ilib/llama.cpp/tools/mtmd \
           -Ilib/llama.cpp/vendor
CFLAGS   = -std=c11 -O2 -Wall -Wextra -DWIN32_LEAN_AND_MEAN $(INCS)
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra -DWIN32_LEAN_AND_MEAN -D_USE_MATH_DEFINES $(INCS)
LDFLAGS  = -Llib/raylib/lib \
           -Llib/whisper -lwhisper \
           -Llib/llama -lllama -lggml -lggml-cpu -lggml-base \
           -Llib/lua -llua \
            -lraylib -lopengl32 -lgdi32 -lwinmm -lole32 -luuid -ldxgi -ld3d11 \
            -lksuser -lcrypt32 -lm -liphlpapi -lsapi \
           -static -lstdc++ -lwinpthread -fopenmp \
           -Wl,--allow-multiple-definition

TARGET  = WinAlp.exe
BUILDDIR= build
SRCDIR  = src
LIBDIR  = lib

# C source files (project modules)
C_SRCS = $(SRCDIR)/main.c \
         $(SRCDIR)/logger.c \
         $(SRCDIR)/ai_engine.c \
         $(SRCDIR)/audio_capture.c \
         $(SRCDIR)/system_agent.c \
         $(SRCDIR)/memory_store.c \
         $(SRCDIR)/lua_runtime.c \
         $(SRCDIR)/pdf_reader.c \
         $(SRCDIR)/context_tracker.c \
         $(SRCDIR)/prompt_engine.c \
          $(SRCDIR)/plugin_manager.c \
          $(SRCDIR)/thread_mutex.c \
          $(SRCDIR)/dpapi_crypt.c \
          $(SRCDIR)/sys_diag.c \
           $(SRCDIR)/thread_pool.c \
            $(SRCDIR)/doc_router.c \
            $(SRCDIR)/vlm_engine.c \
            $(SRCDIR)/sys_monitor.c \
            $(SRCDIR)/tts_engine.c

# C++ source files (project modules + ImGui + ImPlot + raylib backend)
CXX_SRCS = $(SRCDIR)/ui_render.cpp \
           $(SRCDIR)/stt_engine.cpp \
           $(SRCDIR)/vision_engine.cpp \
           $(SRCDIR)/ocr_engine.cpp \
           $(SRCDIR)/pdf_image_extract.cpp \
           $(LIBDIR)/imgui/imgui.cpp \
           $(LIBDIR)/imgui/imgui_draw.cpp \
           $(LIBDIR)/imgui/imgui_tables.cpp \
           $(LIBDIR)/imgui/imgui_widgets.cpp \
           $(LIBDIR)/imgui/imgui_demo.cpp \
           $(LIBDIR)/imgui/implot.cpp \
           $(LIBDIR)/imgui/implot_items.cpp \
           $(LIBDIR)/imgui/implot_demo.cpp \
           $(LIBDIR)/imgui/imgui_impl_raylib.cpp

# mtmd multimodal sources
MTMD_DIR  = lib/llama.cpp/tools/mtmd
MTMD_SRCS = $(MTMD_DIR)/clip.cpp \
            $(MTMD_DIR)/mtmd.cpp \
            $(MTMD_DIR)/mtmd-image.cpp \
            $(MTMD_DIR)/mtmd-helper.cpp \
            $(MTMD_DIR)/mtmd-audio.cpp
MTMD_MODEL_SRCS = $(wildcard $(MTMD_DIR)/models/*.cpp)

C_OBJS = $(patsubst $(SRCDIR)/%.c, $(BUILDDIR)/%.o, $(C_SRCS))
CXX_SRC_OBJS = $(patsubst $(SRCDIR)/%.cpp, $(BUILDDIR)/%.o, $(filter $(SRCDIR)/%.cpp, $(CXX_SRCS)))
CXX_LIB_OBJS = $(patsubst $(LIBDIR)/%.cpp, $(BUILDDIR)/%.o, $(filter $(LIBDIR)/%.cpp, $(CXX_SRCS)))
MTMD_OBJS = $(patsubst $(MTMD_DIR)/%.cpp, $(BUILDDIR)/mtmd/%.o, $(MTMD_SRCS) $(MTMD_MODEL_SRCS))
ALL_OBJS = $(C_OBJS) $(CXX_SRC_OBJS) $(CXX_LIB_OBJS) $(MTMD_OBJS)

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
	@test -f "$(LIBDIR)/lua/liblua.a" || { \
	    echo "Building Lua static library..."; \
	    "scripts/build_lua.bat"; \
	}

$(BUILDDIR):
	mkdir -p $(BUILDDIR)/imgui

$(BUILDDIR)/%.o: $(SRCDIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILDDIR)/%.o: $(SRCDIR)/%.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILDDIR)/imgui/%.o: $(LIBDIR)/imgui/%.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILDDIR)/mtmd/%.o: $(MTMD_DIR)/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(TARGET): $(ALL_OBJS)
	$(CXX) $(ALL_OBJS) -o $@ -mwindows $(LDFLAGS)
	@echo "Build complete: $(TARGET)"

run: all
	./$(TARGET)

clean:
	rm -rf $(BUILDDIR) $(TARGET)

# ---------- Tests ----------
TEST_TARGET = tests/WinAlp_tests.exe
TEST_SRC    = tests/test_runner.c
TEST_OBJ    = $(BUILDDIR)/test_runner.o

$(TEST_OBJ): $(TEST_SRC)
	$(CC) $(CFLAGS) -c $< -o $@

$(TEST_TARGET): submodule-libs $(BUILDDIR) $(ALL_OBJS) $(TEST_OBJ)
	$(CC) $(filter-out $(BUILDDIR)/main.o, $(ALL_OBJS)) $(TEST_OBJ) -o $@ $(LDFLAGS)
	@echo "Test build complete: $(TEST_TARGET)"

test: $(TEST_TARGET)
	./$(TEST_TARGET)

# ---------- Download STT model (Whisper tiny, ~75MB) ----------
WHISPER_MODEL_URL = https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-tiny.bin
WHISPER_MODEL_PATH = models/ggml-tiny.bin

download-stt-model:
	powershell -Command "if (-not (Test-Path '$(WHISPER_MODEL_PATH)')) { \
	    Write-Host 'Downloading whisper tiny model...'; \
	    Invoke-WebRequest -Uri '$(WHISPER_MODEL_URL)' -OutFile '$(WHISPER_MODEL_PATH)' -UseBasicParsing; \
	    Write-Host 'Downloaded: $(WHISPER_MODEL_PATH)'; \
	} else { \
	    Write-Host 'Model already exists: $(WHISPER_MODEL_PATH)'; \
	}"

# ---------- Download small AI model (Qwen 2.5 1.5B Q4_K_M, ~1GB) ----------
QWEN_MODEL_URL = https://huggingface.co/Qwen/Qwen2.5-1.5B-Instruct-GGUF/resolve/main/qwen2.5-1.5b-instruct-q4_k_m.gguf
QWEN_MODEL_PATH = models/brain-model.gguf

download-ai-model:
	powershell -Command "if (-not (Test-Path '$(QWEN_MODEL_PATH)')) { \
	    Write-Host 'Downloading Qwen 1.5B Q4_K_M (~1GB) ...'; \
	    Invoke-WebRequest -Uri '$(QWEN_MODEL_URL)' -OutFile '$(QWEN_MODEL_PATH)' -UseBasicParsing; \
	    Write-Host 'Downloaded: $(QWEN_MODEL_PATH)'; \
	} else { \
	    Write-Host 'AI model already exists: $(QWEN_MODEL_PATH)'; \
	}"
