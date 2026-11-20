# AGENTS.md

Guidelines for coding agents working in `rvsim`.

## 1. Project Overview & Target Scope

`rvsim` is a cross-platform RISC-V simulator written in **C++23**, targeting **Linux, Windows, macOS, and WebAssembly (Emscripten)**, with a **Dear ImGui** (`docking` branch) + GLFW + OpenGL3/WebGL2 UI.

- **Target simulator scope**: Full-featured **RV32/RV64** simulator supporting both **ELF binaries** and **assembly input**.
- **Core architectural boundary**: Keep `src/simulator/` (ISA decode/execute, memory, CSRs, ELF loader, assembler) decoupled from ImGui, GLFW, and Emscripten APIs so the simulator core remains portable and unit-testable on a native host compiler.

## 2. Repository Layout

- `CMakeLists.txt` — Unified multi-OS + Web CMake build: `rvsim_core` (UI-free simulator library), `rvsim` (GUI), `rvsim-cli` (native-only), `rvsim_tests` (native-only GoogleTest, `RVSIM_BUILD_TESTS`), plus `imgui_lib`, `imgui_text_editor`, and `rvsim_platform`; fetches `ocornut/imgui`, native `glfw`, and `googletest` via `CPM.cmake`. `RVSIM_ENABLE_SANITIZERS` enables ASan + UBSan.
- `CMakePresets.json` — Configure/build presets (`native-debug`, `native-release`, `native-asan`, `web`, `web-emcmake`) and test presets for the three native configurations.
- `ROADMAP.md` — Architecture and checkpoint roadmap (check off items as they land).
- `cmake/` — Build helpers:
  - `get_cpm.cmake` — `CPM.cmake` (`v0.43.2`) bootstrapper with SHA256 verification.
  - `embed_assets.cmake` — Build-time `.ttf` asset embedder generating `generated/embedded_fonts.{h,cpp}`.
- `web/coi-serviceworker.js` — Cross-Origin Isolation service worker (`v0.1.7`), copied next to `build/web/index.html` via `POST_BUILD` for static hosts such as GitHub Pages.
- `shell_minimal.html` — Custom Emscripten HTML shell (`#canvas`) loading `coi-serviceworker.js` and gating `{{{ SCRIPT }}}` inside `<template id="emscripten-script">` until `window.crossOriginIsolated` is true.
- `.github/workflows/ci.yml` — 4-platform CI (`linux`, `windows`, `macos`, `web`) running `ctest` on native jobs, a Linux `native-asan` test job, plus `v*`-tag GitHub Release and GitHub Pages deployment.
- `.clang-format` — Formatting rules (`BasedOnStyle: Google`).
- `.clangd` — `clangd` configuration (`CompilationDatabase: build/native-release`, strips `-sDISABLE_EXCEPTION_CATCHING=*`, `-sUSE_*`, and `--use-port=*` without matching `-std=c++23`).
- `.vscode/` — `settings.json` (cSpell words, `clangd.arguments` with `--query-driver=**/*`).
- `src/`
  - `main.cpp` — Entry point; constructs `Context`, registers panels on `App`, runs `App::Loop()`.
  - `app.h`, `app.cpp` — GLFW/OpenGL/WebGL window setup, Dear ImGui initialization and theme, embedded font loading, main loop (`Context::BeginFrame()` → `ui::Workspace::Render()` → panels).
  - `context.h`, `context.cpp` — UI-thread application state: `TextEditor`, `SimulationController`, the per-frame `UiSnapshot`, line breakpoints (TextEditor line user data), and IDE actions.
  - `ui/` — `design_tokens.h` (palette, spacing, `ApplyTheme`), `riscv_language.{h,cpp}` (editor syntax + palette), `workspace.{h,cpp}` (menu bar, toolbar, DockBuilder layout, status bar, shortcuts).
  - `windows/` — Panels inheriting `rvsim::Window` (stable `###name` window IDs): `editor_window`, `disassembly_window`, `register_window`, `memory_window`, `console_window`, `demo_window` (Help → Dear ImGui Demo).
  - `cli/main.cpp` — `rvsim-cli run|assemble` (native only).
  - `simulator/` — `rvsim_core` (`namespace rvsim`, no UI/Emscripten includes): `core/` (types, CPU state, registers, instruction table, decoder, encoder, disassembler, executor), `memory/` (sparse `PagedRam`, `MemoryBus`, `MmioDevice`), `env/` (`ConsoleIo`, RARS/Venus ecalls, `BufferedConsole`), `loader/` (`ProgramImage`, lexer, pseudo-instruction expansion, two-pass assembler), `runtime/` (`Machine`, thread-safe `SimulationController`).
- `tests/` — `unit/` and `integration/` GoogleTest suites; `testdata/` assembly fixtures.
- `assets/fonts/` — `NotoSans-Regular.ttf`, `NotoSansMono-Regular.ttf` (embedded into the binary at build time).
- `external/` — Vendored third-party dependencies (`text_editor/TextEditor.{h,cpp}`, `emscripten_mainloop_stub.h`). **Never edit files in `external/` directly** unless explicitly asked.

## 3. Build, Run & Tooling Commands

```bash
# 1. Native build (Linux / macOS / Windows via Ninja; dependencies fetched via CPM.cmake)
cmake --preset native-release
cmake --build --preset native-release
./build/native-release/rvsim

# 2. Web build via Emscripten SDK (with $EMSDK set via source emsdk_env.sh)
cmake --preset web
cmake --build --preset web

#    Or with distro-packaged Emscripten without $EMSDK (e.g., Arch /usr/lib/emscripten)
emcmake cmake --preset web-emcmake
cmake --build --preset web-emcmake

# 2b. Native tests (unit + integration + rvsim-cli checks), incl. ASan/UBSan
ctest --preset native-release --output-on-failure
cmake --preset native-asan && cmake --build --preset native-asan
ctest --preset native-asan --output-on-failure

# 2c. Headless CLI
./build/native-release/rvsim-cli run tests/testdata/fibonacci.s --dump-regs
./build/native-release/rvsim-cli assemble tests/testdata/quicksort.s --disasm

# 3. Symlink compile_commands.json at repo root for Serena / clangd tools (gitignored)
ln -sf build/native-release/compile_commands.json compile_commands.json
#    (Or build/web/compile_commands.json when inspecting Emscripten-only code)

# 4. Serve Web build locally with COOP/COEP headers for -pthread
emrun build/web/index.html
# Headless server for browser/DevTools inspection:
emrun --no_browser --port 8080 build/web/index.html

# 5. Format modified C++ sources
clang-format -i src/<modified-files>
```

> **Note on `clangd` / Serena:** When pointing `compile_commands.json` at `build/web/compile_commands.json` (`em++`), `clangd` needs `--query-driver` (e.g., `--query-driver="/usr/lib/emscripten/em*"` or `--query-driver=**/*` as in `.vscode/settings.json`) to resolve Emscripten sysroot headers.

## 4. Code Conventions & Technical Constraints

- **Language & warnings**: Strict C++23 (`<print>`, `<ranges>`, `CMAKE_CXX_EXTENSIONS OFF`), compiled with `-Wall -Wextra` on GCC/Clang/Emscripten and `/W4` on MSVC on `rvsim` (third-party headers marked `SYSTEM`). Requires GCC 14+, Clang 18+ (`libc++`), Xcode 16.3+, Visual Studio 2022 17.7+ (MSVC 19.37+), or Emscripten. Keep builds free of warnings.
- **No exceptions**: Emscripten builds default to `-sDISABLE_EXCEPTION_CATCHING=1`. Do not use `throw` / `try` / `catch` for control flow or error reporting; use `std::expected`, `std::optional`, or explicit error structs/enums.
- **Emscripten `-pthread` & thread safety**: WebAssembly pthreads use `-sPTHREAD_POOL_SIZE=2` and `-sALLOW_MEMORY_GROWTH=1`, and require COOP/COEP HTTP headers (`emrun` sets them automatically; `web/coi-serviceworker.js` enables them on static hosts such as GitHub Pages under HTTPS/`localhost`). Keep all Dear ImGui, `TextEditor`, and OpenGL calls strictly on the UI thread and synchronize any shared state touched by worker threads. All assembly and execution runs on the single `SimulationController` worker thread: send commands in (source text is copied), read immutable `UiSnapshot`s out — never touch `Machine` from the UI thread.
- **C++ style**: Follow Google C++ Style Guide (`.clang-format`):
  - Put project symbols in `namespace rvsim` (`rvsim::ui` for `src/ui/`).
  - Use `#pragma once` header guards.
  - `PascalCase` for classes, structs, and methods; `snake_case` for local variables and simple accessors (`clear_color()`); trailing `_` on private member variables (`context_`, `window_`).
- **Dear ImGui conventions**:
  - Under `__EMSCRIPTEN__`, `io.IniFilename` is set to `nullptr` (no filesystem INI persistence).
  - Core UI fonts (`assets/fonts/*.ttf`) are embedded at build time into `generated/embedded_fonts.{h,cpp}` and loaded via `AddFontFromMemoryTTF(..., 0.0f, &font_cfg)` with `font_cfg.FontDataOwnedByAtlas = false` (no `--preload-file`).
  - Add new UI panels by subclassing `rvsim::Window` (`Render()` override) and registering them in `main.cpp` via `App::RegisterWindow(...)`.
- **CMake structure**: `CMakeLists.txt` supports both native desktop (`OpenGL::GL`, `Threads::Threads`, CPM `glfw`) and Emscripten (`contrib.glfw3`, `-pthread`) via `rvsim_platform` and `CMakePresets.json`. Sources are globbed per target (`src/simulator/**` → `rvsim_core`; `src/windows`, `src/ui`, `src/platform` → `rvsim`; `tests/**` → `rvsim_tests`). Warnings come from the `rvsim_warnings` interface target.
- **Git & commit conventions**:
  - **Commit very atomically**: Keep every commit small, single-purpose, and logically self-contained. Separate distinct concerns (e.g., header fixes, standalone bug fixes, build scripts, build migration, presets/IDE config, CI workflows, and documentation) into individual commits instead of bundling unrelated edits.
  - **Conventional Commits**: Use concise imperative subjects with Conventional Commit prefixes (`feat:`, `fix:`, `build:`, `ci:`, `refactor:`, `docs:`, `chore:`, with optional scopes such as `feat(web):`).
  - Never push commits to a remote unless explicitly asked by the user.

## 5. Installed Agent Skills (`.agents/skills/`)

Activate the relevant skill before non-trivial work in its domain:

- `imgui-ui-ux-engineering` — Dear ImGui dockspace layout, widget IDs, tables/`ImGuiListClipper`, DPI scaling, and UI reviews.
- `assembly-riscv` — RV32/RV64 ISA instructions, psABI calling conventions, extensions, and RISC-V assembly patterns.
- `elf-inspection` — ELF32/ELF64 headers, sections, program segments, symbol tables, and binary loading.
- `interpreters` — Bytecode/instruction decode and dispatch loop design, VM architecture, and execution engines.
- `cmake` — Target-first CMake refactoring, native test targets, and cross-compilation setups.
- `cpp-coding-standards` — Modern C++ Core Guidelines, RAII, ownership, and type safety.
- `cpp-testing` — GoogleTest/CTest and sanitizer setup when introducing native unit tests.

## 6. Configured MCP Servers & Usage Rules

Project MCP servers are configured in `.agents/plugins/rvsim-tools/mcp_config.json` (Antigravity workspace plugin) and mirrored alongside global tools in `.mcp.json` (for Claude Code / Cursor / VS Code). **Use these MCP tools proactively instead of guessing or relying on brittle manual workarounds:**

- **RISC-V encodings, assembly, and compiler test vectors → `compiler-explorer`** (`https://compiler-explorer.com/mcp`):
  - Whenever you need to verify RV32/RV64 instruction encodings, ABI output, or assembly test fixtures (especially since no local `riscv64-unknown-elf-gcc` is installed), call `compile` / `list_compilers` / `lookup_asm_instruction` on Godbolt instead of hand-encoding instructions from memory.
- **Third-party repo architecture & internals → `deepwiki`** (`https://mcp.deepwiki.com/mcp`):
  - Before guessing internal behavior, backend APIs, or ISA specs for public repositories (such as `ocornut/imgui`, `emscripten-core/emscripten`, or `riscv/riscv-isa-manual`), query `read_wiki_structure`, `read_wiki_contents`, or `ask_wiki_question`.
- **Library API references → `context7`** (global Antigravity plugin, mirrored in `.mcp.json`):
  - Before writing unfamiliar library calls or CMake/toolchain flags, call `resolve-library-id` and `query-docs` to verify current API signatures.
- **Cross-file C++ symbol lookup & semantic refactoring → `serena`** (`uvx --from serena-agent serena start-mcp-server --context ide --project-from-cwd`):
  - Prefer `serena` (`initial_instructions`, then `serena_repl`) over text-only grep/replace when tracing types, references, or refactoring symbols across `src/`. Requires `compile_commands.json` at the repo root and `--query-driver` when pointing at Emscripten headers.
- **Live UI, WebGL canvas & runtime verification → `chrome-devtools`** (global Antigravity plugin, mirrored in `.mcp.json`):
  - After changing `src/windows/`, `app.*`, `shell_minimal.html`, or Emscripten flags, build (`cmake --build --preset web-emcmake` or `web`), serve with `emrun --no_browser --port 8080 build/web/index.html`, and verify rendering/console output via Chrome DevTools.
