# RISC-V Simulator

## Building and running

First, clone the repository recursively for all the dependencies:

```bash
git clone --recursive https://github.com/tuasananh/rvsim
```

Make sure you have installed [Emscripten](https://emscripten.org/), then run these commands:

```bash
emcmake cmake -B build
cmake --build build
```

After that, just run: 

```bash
emrun build/web/index.html
```