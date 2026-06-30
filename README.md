# WinAlp — Jarvis-Local OS Agent

> **%100 Offline · Sıfır Kurulum · Taşınabilir · Windows x64**

WinAlp is a Jarvis-style desktop AI agent that runs entirely on your local machine without any internet connection. It listens to your voice, reads your screen, manages your files, and remembers your context — all powered by a single portable `.exe`.

---

## Philosophy

| Principle | Detail |
|---|---|
| **Zero Dependency** | Only the `.gguf` model file is needed from the user. Every library (llama.cpp, whisper.cpp, raylib, SQLite, Lua, Dear ImGui) is statically linked into the executable. |
| **Zero Installation** | No Python, Node.js, CUDA Toolkit, or package manager required. |
| **Fully Portable** | Copy the folder anywhere — it just works. |
| **100% Offline** | No data ever leaves your machine. |

---

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────────┐
│                         WinAlp.exe                              │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────────┐   │
│  │  THREAD 1 │  │ THREAD 2 │  │ THREAD 3 │  │   THREAD 4   │   │
│  │  UI/HUD   │  │ Audio+STT│  │  LLM     │  │ Vision/OCR   │   │
│  │ Raylib+   │  │miniaudio │  │llama.cpp │  │ DXGI Dupl.   │   │
│  │ ImGui 60  │  │+whisper  │  │ streaming│  │ pixel-change │   │
│  │   FPS     │  │  .cpp    │  │  tokens  │  │  triggered   │   │
│  └─────┬─────┘  └────┬─────┘  └────┬─────┘  └──────┬───────┘   │
│        │             │              │                │           │
│        └─────────────┴──────────────┴────────────────┘          │
│                    Thread-safe message queues                    │
│                                                                  │
│  ┌──────────────────────────────────────────────────────────┐   │
│  │                    THREAD 5 — Memory & Plugins           │   │
│  │         memory_store.c (SQLite mutex) + plugin_manager   │   │
│  └──────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────┘
```

---

## Core Modules

| Module | File | Role |
|---|---|---|
| **Ear** | `audio_capture.c` | miniaudio.h — realtime PCM capture |
| **STT** | `stt_engine.c` | whisper.cpp — speech → text |
| **Brain** | `ai_engine.c` | llama.cpp — LLM inference + streaming |
| **Eye** | `vision_engine.c` | DXGI Duplication + OCR |
| **Hand** | `system_agent.c` | JSON action parser + OS executor |
| **Memory** | `memory_store.c` | SQLite + profile.json persistence |
| **UI** | `ui_render.c` | Raylib + ImGui holographic HUD |
| **Scripts** | `lua_runtime.c` | Embedded Lua + hot-reload |
| **Prompts** | `prompt_engine.c` | Template-based system prompt synthesis |
| **Plugins** | `plugin_manager.c` | Sandboxed Lua plugin loader |
| **PDF** | `pdf_reader.c` | Static C PDF text/image extractor |
| **Context** | `context_tracker.c` | Win32 active window tracker |

---

## Directory Structure

```
WinAlp/
├── WinAlp.exe            ← Portable executable (~15-25 MB, statically linked)
├── models/               ← Drop your .gguf model here
│   ├── brain-model.gguf
│   └── whisper-tiny-tr.bin
├── src/                  ← C source files
├── include/              ← Header files
├── lib/                  ← Static library archives
├── scripts/              ← Lua behavior scripts
├── plugins/              ← Extensible plugin packages
├── prompts/              ← System prompt templates
├── build/                ← Compiler intermediates (generated)
├── profile/              ← User data (auto-created on first run)
│   ├── conversations.db
│   ├── profile.json
│   ├── tasks/
│   ├── files/
│   └── cache/
└── Makefile
```

---

## Getting Started

1. **Download a GGUF model** (e.g. `Qwen-2.5-1.5B-Instruct Q4_K_M`) and place it in `models/`.
2. **Run `WinAlp.exe`** — the model selector screen will appear automatically.
3. Select your model and start talking or typing. That's it.

> No installation. No setup wizard. No internet. Just run.

---

## Hardware Recommendations

| Tier | Model | RAM Required |
|---|---|---|
| **Light** (CPU only) | Qwen-2.5-1.5B Q4_K_M | ~2 GB |
| **Medium** | Llama-3.2-3B Q4_K_M | ~3 GB |
| **Pro** (high VRAM) | DeepSeek-R1-14B Q8_0 | ~18 GB |

Tested reference hardware: **ASUS N55JK** (i7-4700HQ · GT 850M 2GB · 8GB RAM) — Light tier recommended.

---

## Security

- All file operations are sandboxed to the working directory.
- Destructive actions (delete, overwrite) require explicit user confirmation via UI overlay.
- All profile data is stored locally. Nothing is transmitted over the network.
- Optional: Windows DPAPI encryption for `profile/` contents.

---

## Build (for developers)

```bash
# Requires MinGW-w64 or MSVC toolchain
make all
```

See `Makefile` for build flags and static library configuration.

---

## License

MIT License — see `LICENSE` file.

---

*WinAlp is an open-source project. Contributions, bug reports and model compatibility notes are welcome.*
