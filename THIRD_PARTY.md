# Third-party components

ImForge itself is MIT licensed (see [LICENSE](LICENSE)). Below is everything else it builds
against, fetches, downloads or runs, with the exact pin and license. None of it is vendored
in this repository: libraries are fetched by CMake `FetchContent` at configure time
(`cmake/Dependencies.cmake`), fonts are downloaded at configure time (`cmake/Fonts.cmake`),
and SDKs are looked up on your machine.

A shipped application only depends on Dear ImGui. Generated code and the client header
(`include/imforge/imforge_client.h`, `imb_runtime.h`) need nothing else, unless the app opts
into one of the shader blur headers for its renderer.

## Fetched at configure time (CMake `FetchContent`)

| Component | Pin | License | URL | Used for |
|---|---|---|---|---|
| Dear ImGui | `v1.91.9b-docking` (default `IMFORGE_IMGUI_TAG`; CI also builds `v1.85`, `v1.87`, `v1.89.9`, `v1.90.9`, `master`) | MIT | https://github.com/ocornut/imgui | The builder UI and canvas, the interpreter, the example apps and the per-target `imgui_impl_*` backends (`win32`, `sdl3`, `opengl3`, `dx9`, `dx11`, `dx12`, `vulkan`). `misc/cpp/imgui_stdlib.cpp` is compiled into the `imgui` target. `examples/client/*` are the stock Dear ImGui examples with marked ImForge lines added. |
| SDL3 | `release-3.2.30`, static (`SDL_STATIC=ON`) | zlib | https://github.com/libsdl-org/SDL | Window, input and GL/Vulkan surface for the builder (`IMFORGE_BUILDER_BACKEND=SDL3_GL3`) and for the `gl3_*` / `vulkan_*` examples. Not used by generated code or the client header. |
| doctest | `v2.4.11` (header only) | MIT | https://github.com/doctest/doctest | Unit tests under `tests/`, only with `IMFORGE_BUILD_TESTS=ON`. |
| miniz | `3.0.2` (release zip, SHA-256 pinned) | MIT | https://github.com/richgel999/miniz | Reading and writing `.imblib` archives in the builder (`src/builder/imblib.cpp`, `src/builder/project_io.cpp`). `tools/pack_imblib.py` uses Python's `zipfile` instead. |

## Fonts downloaded at configure time (not redistributed here)

`cmake/Fonts.cmake` downloads these into `<build>/fonts` when the builder is built. Set
`IMFORGE_FONTS_DIR` to use a local copy, for example when you're offline. Their license files
go into `<build>/fonts/licenses` next to a generated `THIRD_PARTY.txt`; ship both with any
builder binary you distribute.

| Component | Pin | License | URL | Used for |
|---|---|---|---|---|
| Inter | `4.1` (`Inter-Regular.ttf`, `Inter-Bold.ttf`) | SIL Open Font License 1.1 | https://github.com/rsms/inter | Builder UI font. |
| JetBrains Mono | `v2.304` (`JetBrainsMono-Regular.ttf`) | SIL Open Font License 1.1 | https://github.com/JetBrains/JetBrainsMono | Code view and log panel font. |
| Font Awesome 6 Free | `6.7.2` (`fa-solid-900.ttf`) | Icons: CC BY 4.0. Font files: SIL Open Font License 1.1. (Code in the upstream package: MIT; not used.) | https://fontawesome.com and https://github.com/FortAwesome/Font-Awesome | Icon font merged into the builder font atlas; the icon picker; icons referenced by `{icon:name}` in labels. |
| IconFontCppHeaders | commit `8a381189ecfe58e732466cc52e79ca887dd6a297` (`IconsFontAwesome6.h`) | zlib | https://github.com/juliettef/IconFontCppHeaders | `ICON_FA_*` codepoint macros used by the icon picker and emitted into generated code. |

## Found on the machine (system libraries and SDKs)

| Component | Version | License | URL | Used for |
|---|---|---|---|---|
| Vulkan SDK (Vulkan-Headers, Vulkan-Loader) | whatever `find_package(Vulkan)` finds (developed against SDK 1.4.363.0) | Vulkan-Headers: Apache-2.0 OR MIT; Vulkan-Loader: Apache-2.0 | https://github.com/KhronosGroup/Vulkan-Headers, https://vulkan.lunarg.com | Only for building `imgui_impl_vulkan` and the Vulkan examples (`examples/fx/vulkan_sdl3_blur_demo`, `examples/client/vulkan_sdl3`). `include/imforge/fx/imb_fx_vulkan.h` includes `<vulkan/vulkan.h>`, so an application that uses the Vulkan blur needs the headers too. Skipped when the SDK is missing. |
| glslang | `11:16.6.0` (the version recorded in the generated headers; any recent release works) | BSD-3-Clause and Apache-2.0 (mixed, see the upstream `LICENSE.txt`) | https://github.com/KhronosGroup/glslang | Run offline by `tools/fx/compile_vulkan_shaders.py` to compile `tools/fx/vulkan_shaders/*.vert` / `*.frag` into the SPIR-V arrays checked in as `include/imforge/fx/imb_fx_vulkan_shaders.h` and `examples/fx/vulkan_sdl3_blur_demo/demo_shaders.h`. Not needed to build or run ImForge. |
| libclang (LLVM) | clang 14 to 20 (verified with 18.1.1) | Apache-2.0 with LLVM exception | https://llvm.org, https://github.com/llvm/llvm-project | Optional (`IMFORGE_WITH_LIBCLANG` = `AUTO`, `ON` or `OFF`). Dynamically linked by `imforge_binding` / `imforge-reflect` to scan `compile_commands.json` for bindable struct members (`src/binding/clang_scanner.cpp`). On Windows `libclang.dll` is copied next to `imforge-reflect`. Without it, binding falls back to `IMFORGE_REFLECT` tables and hand-written `types.json`. |
| Direct3D 9 / 11 / 12, DXGI, D3DCompiler, DWM, IMM32, Winsock (`d3d9`, `d3d11`, `d3d12`, `dxgi`, `d3dcompiler`, `dwmapi`, `imm32`, `ws2_32`) | Windows SDK | Microsoft Windows SDK license (system components) | https://developer.microsoft.com/windows/downloads/windows-sdk/ | DX backends, the `imb_fx_dx*.h` blur headers (which compile their HLSL at runtime with `d3dcompiler`), the Win32 builder backend and the TCP transport on Windows. |
| OpenGL (`opengl32`, `libGL`, OpenGL.framework) | system | system | | `imgui_impl_opengl3`, `imb_fx_gl3.h`, the SDL3 + GL3 builder backend. |
| X11 / Wayland development libraries | system (Linux only) | MIT / X11 | | SDL3 video backends on Linux. Without them SDL3 is built console-only and the builder can't open a window. |
| POSIX threads / `rt` | system | system | | `Threads::Threads` for the live server and transports; `rt` for shared memory on Linux. |

## Tooling that isn't linked

| Component | License | Used for |
|---|---|---|
| Python 3 (standard library only: `json`, `zipfile`, `subprocess`, ...) | PSF | `tools/*.py` (`amalgamate.py`, `pack_imblib.py`, `validate_json.py`, `check_version_matrix.py`, `fx/compile_vulkan_shaders.py`). No third-party Python packages. |
| GitHub Actions `actions/checkout@v4`, `actions/cache@v4`, `actions/upload-artifact@v4`, `actions/download-artifact@v4`, `actions/setup-python@v5` | MIT | `.github/workflows/ci.yml` and `.github/workflows/release.yml`. |

## Keeping this file current

The pins live in `cmake/Dependencies.cmake` (Dear ImGui, SDL3, doctest, miniz),
`cmake/Fonts.cmake` (fonts and `IconsFontAwesome6.h`), `src/binding/CMakeLists.txt` (libclang
lookup) and `tools/fx/compile_vulkan_shaders.py` (glslang). When one of them changes, update
the matching row here. The per-build `THIRD_PARTY.txt` is generated from `cmake/Fonts.cmake`
and should say the same thing.
