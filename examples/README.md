# Examples

```
examples/
  client/common/demo_types.h    sample game state (Player, Settings, ...) + IMFORGE_REFLECT tables
  client/common/demo_app.h      shared per-frame code: imforge::live::NewFrame/Draw, status window
  client/dx9_win32/main.cpp     Win32 + Direct3D 9
  client/dx11_win32/main.cpp    Win32 + Direct3D 11
  client/dx12_win32/main.cpp    Win32 + Direct3D 12
  client/gl3_sdl3/main.cpp      SDL3 + OpenGL 3
  client/vulkan_sdl3/main.cpp   SDL3 + Vulkan
  fx/<backend>_blur_demo/       standalone shader blur demos
  projects/                     sample projects to open in the builder
```

Each `client/*/main.cpp` is the matching Dear ImGui example (`examples/example_<platform>_<renderer>`)
plus the few lines an app needs for live attach and shader fx. Those lines are marked
`ImForge:`; the rest is the stock window, device and main loop code, so you can diff an example
against the official one to see exactly what to add to your own app.

## Building

Examples are off by default:

```powershell
# Windows (MSVC): all five with the Vulkan SDK installed, four without
cmake -S . -B build-msvc -G "Visual Studio 17 2022" -A x64 -DIMFORGE_BUILD_EXAMPLES=ON
cmake --build build-msvc --config Release --target imforge_example_dx11_win32
build-msvc\examples\Release\imforge_example_dx11_win32.exe
```

```bash
# Linux / WSL: gl3_sdl3 (+ vulkan_sdl3 when find_package(Vulkan) succeeds)
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DIMFORGE_BUILD_EXAMPLES=ON
cmake --build build --target imforge_example_gl3_sdl3
./build/examples/imforge_example_gl3_sdl3
```

| Target | Platform | Needs |
|---|---|---|
| `imforge_example_dx9_win32` | Windows | Windows SDK |
| `imforge_example_dx11_win32` | Windows | Windows SDK |
| `imforge_example_dx12_win32` | Windows | Windows SDK |
| `imforge_example_gl3_sdl3` | Windows, Linux, macOS | SDL3 (fetched by CMake), OpenGL 3.0+ |
| `imforge_example_vulkan_sdl3` | Windows, Linux | SDL3 (fetched), Vulkan SDK (`find_package(Vulkan)`) |

The blur demos build the same way as `imforge_fx_<name>` targets. `examples/CMakeLists.txt`
skips anything whose backend isn't available and says so at configure time
(`example client/<name> skipped (missing: ...)`). Dear ImGui is fetched at `IMFORGE_IMGUI_TAG`
(default `v1.91.9b-docking`). The SDL3 examples need a tag that has
`backends/imgui_impl_sdl3.cpp`, and DX12 needs 1.91.6+ for `ImGui_ImplDX12_InitInfo`.

## Running with the builder

1. Start `imforge_builder` and open a project with panels named `MainMenu` and `Overlay`
   (`examples/projects/CheatMenu` has both). The live transport is in Project Settings; the
   default is `tcp://127.0.0.1:7777`.
2. Start an example. The first command-line argument overrides the URL, e.g.
   `imforge_example_gl3_sdl3 pipe://ImForge` or `... shm://ImForge`. Without a builder the
   example runs normally and keeps retrying once a second.
3. The "ImForge demo" window shows the connection state, lets you change the URL and
   connect/disconnect, and prints the animated sample values. Pairing happens on its own (see
   [LIVE_ATTACH.md](../docs/LIVE_ATTACH.md)), and the builder's Live panel shows the app once
   it's connected.
4. Edit the panel in the builder and the example renders every change right away, with its own
   fonts and style. Press the picker button in the Live panel and click a widget in the example
   to select it.

The examples register these, and the sample project refers to them:

| Name | Where | What |
|---|---|---|
| binding root `player` (`Player`) | `DemoFrame` | paths like `player.health`, `player.pos.x`, `player.mode` |
| binding root `cfg` (`Settings`) | `DemoFrame` | paths like `cfg.esp.enabled`, `cfg.aim.fov`, `cfg.misc.watermark` |
| handler `MainMenu.btn_play.onClick` | `DemoRegisterOnce` | refills `player.health` and counts clicks |
| locked block `MainMenu.custom_1` | `DemoRegisterOnce` | draws a hand-written health history graph |

`SimulateGame()` in `demo_types.h` moves the player, drains health and ammo and cycles the
`Mode` enum, so bound widgets visibly change.

## Adding live attach to your own app

The minimum, taken from `demo_app.h` and the `main.cpp` files:

```cpp
#define IMFORGE_LIVE                                   // development builds only
#include "imforge/imforge_client.h"

IMFORGE_REFLECT_ENUM(Mode, Off, Legit, Rage);          // enums and nested structs first
IMFORGE_REFLECT(Player, name, health, max_hp, pos, ammo, mode);

// after your ImGui backend init:
imforge::live::Connect("tcp://127.0.0.1:7777");
imforge::live::RegisterHandler("MainMenu.btn_play.onClick", &OnPlay);   // or #include "generated/imforge_dev.h"
imforge::live::SetFontTextureRebuild(ImGui_ImplDX11_InvalidateDeviceObjects);   // per renderer, see main.cpp

// every frame, before the backend NewFrame calls:
imforge::live::PreNewFrame();

// every frame, after ImGui::NewFrame():
imforge::live::NewFrame();
imforge::live::Draw("MainMenu", { imforge::live::Bind("player", g_player), imforge::live::Bind("cfg", g_cfg) });
```

Handlers and blocks are plain functions `void Fn(imforge::DrawCtx&)`. Without `IMFORGE_LIVE`
every `imforge::live::*` call is an empty inline function, so the lines can stay in release
builds. `dist/imforge_client_single.h` (made by `tools/amalgamate.py`) is the same API in one
file.

## Shader fx

Every example includes `imforge/fx/imb_fx_<backend>.h` behind `#if __has_include(...)`, calls
`imb::fx::Init(<Backend>InitDesc)` after the ImGui renderer backend is up,
`imb::fx::PrepareFrame(FrameDesc)` after the scene is rendered and before
`ImGui_ImplXXX_RenderDrawData`, and `imb::fx::Shutdown()` at exit.

| Example | Init needs | Where PrepareFrame goes |
|---|---|---|
| dx9 | `IDirect3DDevice9*` | inside `BeginScene/EndScene`; `OnDeviceLost/OnDeviceReset` around `Reset()` |
| dx11 | device + context | after `ClearRenderTargetView`, with the back-buffer RTV |
| dx12 | device, RTV format, frames in flight, `frames_in_flight + 6` consecutive SRV descriptors (the example reserves the tail of its SRV heap) | on the open command list, back buffer in `RENDER_TARGET` state |
| gl3 | `SDL_GL_GetProcAddress` + GLSL version string | after `glClear`, with the framebuffer size and `fbo = 0` |
| vulkan | instance/device/queue, render pass, surface format, image count | on the frame command buffer, outside any render pass |

In the Vulkan example fx is opt-in (`-DIMFORGE_EXAMPLE_VULKAN_FX`): the `ImGui_ImplVulkanH`
helper creates the swapchain without `VK_IMAGE_USAGE_TRANSFER_SRC_BIT`, which fx needs to copy
the back buffer. Add that usage bit to your own swapchain and define the macro.

## Shipping: exported code instead of the client

Live attach is for development. For the build you ship, press `Ctrl+E` in the builder. It
writes `MainMenu.h/.cpp`, `Overlay.h/.cpp`, `imforge_components.*`, `imforge_theme_*.h`,
`imforge_fonts.h` and friends into `codegen.outputDir` (see
[CODEGEN.md](../docs/CODEGEN.md)). Then:

1. Add the generated `.cpp` files to your build and `#include "MainMenu.h"`.
2. Replace `imforge::live::Draw("MainMenu", {...})` with
   `ui::DrawMainMenu(main_menu_state, player, cfg);`, one `MainMenuState` per instance.
3. Drop `#define IMFORGE_LIVE` (or the whole `imforge_client.h` include) and `imforge_dev.h`.
   The `IMFORGE_REFLECT` tables are only needed by the live client.
4. Keep `imb_runtime.h` if the project uses animations or DrawList effects, and the
   `fx/imb_fx_<backend>.h` include with the `Init/PrepareFrame/Shutdown` calls if it uses blur.
   Generated code only calls `imb::fx::Blur` when `IMB_FX_AVAILABLE` is defined.
5. Your handler bodies live in the `@imb:user` regions of the generated `.cpp`, and
   re-exporting keeps them.

Generated code depends only on Dear ImGui (plus `imb_runtime.h`/`imb_fx_*.h` when used), never
on `imforge::`.
