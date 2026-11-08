# docs/media

Images used by the top-level README and the docs. They're all real captures of code in this
repository, nothing is mocked up.

| File | Size | What it is |
|---|---|---|
| `fx_dx9.png` | 1264 x 761 | `examples/fx/dx9_blur_demo` (Win32 + Direct3D 9): `imb::fx::Blur` over a striped background, `BlurCurrentWindow(radius)` on a draggable window, 143.8 FPS. |
| `fx_dx11.png` | 1084 x 681 | `examples/fx/dx11_blur_demo` (Win32 + Direct3D 11): `BlurCurrentWindow(12.f)` with a fully transparent window background, a `Blur(dl, min, max, BlurParams)` control window and an opaque window for comparison, 144 FPS. |
| `fx_dx12.png` | 1264 x 761 | `examples/fx/dx12_blur_demo` (Win32 + Direct3D 12): blur controls window and the default `BlurCurrentWindow(12)` call over soft circles, 144 FPS. |
| `fx_gl3.png` | 1280 x 720 | `examples/fx/gl3_blur_demo` (SDL3 + OpenGL 3): `Blur(GetWindowDrawList(), ...)` and `BlurCurrentWindow(12.f)` over moving striped quads, 144 FPS. |
| `fx_vulkan.png` | 1280 x 720 | `examples/fx/vulkan_sdl3_blur_demo` (SDL3 + Vulkan, needs the Vulkan SDK at configure time): a "Frosted" window calling `BlurCurrentWindow(12.f)` plus a "Blur controls" window, 144 FPS. |
| `fx_montage.png` | 1600 x 744 | The five captures above in a 3 x 2 grid with a caption cell. Shown in the README. |
| `hero.gif` | 1280 x 693 | The builder (left) running `tools/demo/live_demo.txt` against `examples/client/dx11_win32` (right): title, theme color and slider range edits show up in the running app. 57 frames at 5 fps. |
| `builder_first_run.png` | 1602 x 932 | The builder (SDL3 + OpenGL 3) with `examples/projects/CheatMenu` open and a bound checkbox selected. |
| `builder_wizard.png` | 1602 x 932 | The New Project wizard, template step. |
| `library_animations.png` | 1602 x 932 | `examples/projects/Showcase` open on the Library's Animations tab, `BounceInDown` picked and the entrance cards mid-play. |
| `library_designs.png` | 1602 x 932 | The Designs tab with Synthwave Sunset selected and "Preview on canvas" on. Shown in the README. |
| `library_widgets.png` | 1602 x 932 | The Widgets tab filtered to the display pack, each widget drawn live in its card. |
| `keyframes.png` | 1602 x 932 | The Anim Timeline's Keyframes tab editing `GlitchInCustom`, a project copy of the `GlitchIn` preset. Shown in the README. |
| `showcase.png` | 1602 x 932 | The canvas showing the Showcase `Arena` panel (IceFrost), zoomed to fit. |
| `motions.gif` | 908 x 622 | The Library panel cut from the builder window while the Animations tab goes through All, Attention, Loop and Text. 72 frames at about 12 fps. Shown in the README. |

The `fx_*.png` files were captured on 2026-11-07 on Windows 10 (10.0.19045) from the Release
examples built with MSVC 2022 (`build/win-examples`) against Dear ImGui `v1.91.9b-docking`,
at 100 % DPI. Each one is the demo window's client area at 1:1, cropped from a 1920 x 1080
screenshot, with no scaling or retouching.

The 0.2 Library shots (`library_*.png`, `keyframes.png`, `showcase.png`, `motions.gif`) were
taken on 2026-11-08 from a Release builder at its default 1600 x 900 size and 100 % DPI, with an
empty `%APPDATA%\ImForge`. For `keyframes.png` the bottom dock was dragged taller, and for
`showcase.png` shorter.

## Recording hero.gif

`tools/demo/run_live_demo.ps1` runs the whole demo by itself. It starts the DX11 example,
then starts the builder on `examples/projects/CheatMenu` with `--script
tools/demo/live_demo.txt`, and exits with the script's exit code. The script waits for the
app to pair (pairing is automatic, so nothing in it deals with codes), edits a title, a
slider range and a theme color, turns picker mode on and off, exports to a temp folder and
quits. It never saves, so the sample project stays as it is.

You need the builder and the `imforge_example_dx11_win32` example built in Release. By
default the script looks in `build\windows-msvc-builder` and `build\win-examples`; pass
`-Builder` and `-Client` if yours are somewhere else:

```powershell
powershell -ExecutionPolicy Bypass -File tools\demo\run_live_demo.ps1 -Builder <path to imforge_builder.exe> -Client <path to imforge_example_dx11_win32.exe>
```

Put the two windows side by side, record the screen while it runs, and convert the recording
to a GIF about 1280 px wide at 5 fps (ffmpeg's `palettegen` / `paletteuse` filters work
well). The run also leaves `live_demo_pairing.png` and `live_demo_builder.png` builder
screenshots in the repository root; they're ignored by git.

The current `hero.gif` puts the builder at the left 1152 px of a 1920 x 1040 work area and
the DX11 window over the rest, grabs only those two window rectangles every 200 ms, and
scales the frames to 1280 x 693 with Pillow using one shared palette and no dithering.

Capture from a fresh checkout with an empty `%APPDATA%\ImForge` so no recent projects or saved
layout show up. The default builder window is 1600 x 900 (1602 x 932 with borders).
