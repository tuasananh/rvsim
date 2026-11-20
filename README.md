# RISC-V Simulator (`rvsim`)

Cross-platform RISC-V simulator written in C++23 with Dear ImGui, GLFW, and OpenGL3/WebGL2, targeting **Linux, Windows, macOS, and the Web (Emscripten)**.

## Prerequisites

- **CMake** `3.21+` and **Ninja**
- **Strict C++23 toolchain** (must support `<print>` and `<ranges>`):
  - **Linux:** GCC `14+` or Clang `18+` (`libc++`)
  - **macOS:** Xcode `16.3+`
  - **Windows:** Visual Studio `2022 17.7+` (MSVC `19.37+`) or Visual Studio `2026`
  - **Web:** [Emscripten](https://emscripten.org/) (`emsdk` latest or system `emcmake`)

Dependencies (`ocornut/imgui` and `glfw/glfw` on native builds) are fetched automatically via `CPM.cmake` at configure time—no recursive git clone is required. Core UI fonts are embedded into the compiled binary at build time.

### Linux Desktop Dependencies

On Debian/Ubuntu, install X11, Wayland, and OpenGL development headers for GLFW:

```bash
sudo apt-get install \
  libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev \
  libgl1-mesa-dev libwayland-dev libxkbcommon-dev pkg-config
```

## Building and Running

Clone the repository:

```bash
git clone https://github.com/tuasananh/rvsim
cd rvsim
```

### Native (Linux / macOS / Windows)

On Windows, run from a **Visual Studio Developer Command Prompt** (or Developer PowerShell) so `Ninja` finds `cl.exe`. On Ubuntu 24.04, select GCC 14 with `CXX=g++-14`.

```bash
cmake --preset native-release
cmake --build --preset native-release
```

Run the native executable:

```bash
./build/native-release/rvsim      # Linux / macOS
.\build\native-release\rvsim.exe  # Windows
```

For a debug build, use the `native-debug` preset instead.

### Web (Emscripten)

If using `emsdk` (with `EMSDK` set via `source emsdk_env.sh`):

```bash
cmake --preset web
cmake --build --preset web
```

If using a distro-packaged Emscripten without `$EMSDK` (such as Arch Linux `/usr/lib/emscripten`):

```bash
emcmake cmake --preset web-emcmake
cmake --build --preset web-emcmake
```

Serve locally with `emrun`:

```bash
emrun build/web/index.html
```

> **Note on Web multi-threading (`-pthread`):** Because `rvsim` uses WebAssembly pthreads (`SharedArrayBuffer`), static hosts require a Secure Context (`https://` or `http://localhost`). `web/coi-serviceworker.js` is bundled automatically so deployments on static hosts such as GitHub Pages can enable Cross-Origin Isolation.

## Using the IDE

Write RISC-V assembly (GNU `as` syntax) in the **Editor**, then:

| Shortcut | Action |
|---|---|
| `Ctrl+B` | Assemble (errors appear inline and in **Console → Diagnostics**) |
| `F5` | Run / Continue / Pause |
| `F11` | Step one instruction |
| `F9` (or click the gutter) | Toggle a breakpoint on the current line |
| `Ctrl+R` | Reset the program |

The toolbar selects **RV32I / RV64I** and the run speed (1 Hz to Max). Programs use RARS/Venus-style `ecall` services (`a7` = 1 PrintInt, 4 PrintString, 5 ReadInt, 8 ReadString, 9 Sbrk, 10 Exit, 11 PrintChar, 12 ReadChar, 34 PrintHex, 35 PrintBin, 36 PrintUnsigned, 93 Exit with code); program input is typed into **Console I/O**. The **Disassembly**, **Registers**, and **Memory / Stack** panels update live, highlighting values changed by the last step.

## Command-Line Runner (`rvsim-cli`, native only)

```bash
./build/native-release/rvsim-cli run tests/testdata/fibonacci.s --dump-regs
./build/native-release/rvsim-cli run program.s --arch rv64i --trace --max-steps 100000
./build/native-release/rvsim-cli assemble program.s --disasm
```

`run` exits with the program's exit code (1 on a runtime fault or step limit, 2 on usage or assembly errors).

## Tests

Native builds include a GoogleTest suite (`rvsim_tests`) and CLI checks:

```bash
ctest --preset native-release --output-on-failure

# AddressSanitizer + UndefinedBehaviorSanitizer (GCC/Clang)
cmake --preset native-asan
cmake --build --preset native-asan
ctest --preset native-asan --output-on-failure
```
