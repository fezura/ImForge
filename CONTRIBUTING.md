# Contributing to ImForge

Thanks for taking a look. Here's how to build it, the rules the shared headers follow, and
what a pull request needs.

## Build

You need CMake 3.21+, a C++17 compiler (MSVC 2019+, GCC 9+, Clang 10+), git, and internet
access the first time you configure, because Dear ImGui, SDL3, doctest, miniz and the fonts
are fetched by CMake.

```sh
cmake --preset wsl-gcc            # Linux / WSL, Ninja
cmake --build --preset wsl-gcc
ctest --preset wsl-gcc            # or: ctest --test-dir build/wsl-gcc

cmake --preset windows-msvc       # Windows, Visual Studio 2022 generator
cmake --build --preset windows-msvc --config Release
```

A few options that come in handy: `-DIMFORGE_BUILD_BUILDER=OFF` (headers, libs and tests
only), `-DIMFORGE_IMGUI_TAG=v1.85` (build against another ImGui release),
`-DIMFORGE_WITH_LIBCLANG=OFF`, `-DIMFORGE_BUILDER_BACKEND=WIN32_DX11`.

libclang (LLVM) is optional and only used by the header scanner. The Vulkan SDK is only
needed for the Vulkan blur backend and example.

## Layout

[docs/SPEC.md](docs/SPEC.md) describes the file formats, the public API of each header and
the shape of the generated code, so read the relevant part before changing a module. If you
change a public signature, update the spec in the same pull request.

- `include/imforge/`: header-only modules shared by the builder and the client
  (`imforge_*.h`, `imb_runtime.h`, `fx/`)
- `src/codegen`, `src/roundtrip`, `src/binding`: libraries used by the builder
- `src/builder`: the editor
- `packs/`: widget packs (essentials, ImPlot)
- `schemas/`: JSON Schema for every file format
- `tests/`: doctest suites, one `*_tests.cpp` per module
- `tools/`: amalgamation, packaging and version matrix scripts

## Rules for `include/imforge/` and `packs/`

These headers end up inside other people's applications, sometimes DLLs built with odd
flags, so they're stricter than the rest of the code:

- C++17, header-only, `#pragma once`, every function `inline`.
- Builds warning-free with `-fno-exceptions -fno-rtti -Wall -Wextra` and with MSVC `/W4`.
- No `<iostream>`, `<regex>` or `<filesystem>`, and no `std::function` outside `imforge_interp.h`.
- No mutable globals. Use a function-local static behind an accessor.
- Works with Dear ImGui 1.85 up to the latest release, on both branches. Gate newer API with
  `#if IMGUI_VERSION_NUM >= ...` and docking API with `#ifdef IMGUI_HAS_DOCK`.
- Deterministic output: sorted keys, `\n` line endings, no timestamps.

`tools/check_version_matrix.py` compiles every header against each pinned ImGui checkout.
Run it before opening a pull request that touches a header.

## Style

`.clang-format` wins: 4 spaces, attached braces, 100 columns. Types and functions are
`PascalCase`, variables and members `snake_case`, enums are `enum class`. Try to keep files
under about 800 lines, and only comment what the code can't say by itself.

## Tests

Each module has a doctest suite in `tests/<module>_tests.cpp`, picked up automatically by
`tests/CMakeLists.txt`. Tests that need ImGui use `tests/imgui_headless.h`, which runs ImGui
without a window or GPU. If you change behavior, add a test; if you fix a bug, add one that
would have caught it.

## Pull requests

Keep each pull request to one topic and use conventional commit titles (`feat:`, `fix:`,
`docs:`, `refactor:`, `test:`, `chore:`). CI has to pass on both operating systems and on
every ImGui version in the matrix.

If you change the shape of the generated code, update `docs/CODEGEN.md` and the round-trip
tests. A new widget needs to land in the catalog, the interpreter, the code generator, the
schema and the tests all in the same change.

## Reporting bugs

Tell us the ImGui version and branch, platform, renderer and (for live attach problems) the
transport, and attach a small panel JSON or project folder that shows the problem.
