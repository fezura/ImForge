# Dear ImGui version matrix

ImForge supports Dear ImGui 1.85 and newer, on both the `master` and `docking` branches. You
set the project's target version in the wizard (or let it read your `imgui.h`). The builder
hides or warns about features the target doesn't have, and generated code gates the rest with
`#if IMGUI_VERSION_NUM >= N`.

For reference, `IMGUI_VERSION_NUM` values: 1.85 = 18500, 1.87 = 18700, 1.89 = 18900,
1.89.5 = 18950, 1.90 = 19000, 1.91.9 = 19190, 1.92 = 19200, 1.92.2 = 19220, 1.92.8 = 19280,
1.93.0 = 19300.

## Features by version

| Feature | Needs | Older versions get |
|---|---|---|
| `BeginDisabled` / `EndDisabled` (used by `enabledIf`) | 1.86 (18600) | `PushItemFlag(ImGuiItemFlags_Disabled)` + `PushStyleVar(Alpha)` |
| `ImGuiKey` keyboard IO, `IsKeyPressed(ImGuiKey)` (keybind picker) | 1.87 (18700) | legacy `KeyMap` indices |
| `SeparatorText` | 1.89.5 (18950) | `Separator()` + `Text()` |
| `BeginChild` with `ImGuiChildFlags` (`Child.childFlags`) | 1.90 (19000) | the `bool border` overload |
| `ImGuiCol_TabSelected`, `TabDimmed*` names | 1.90.9 (19090) | `TabActive`, `TabUnfocused*` |
| `ImGuiStyleVar_TabBarOverlineSize` | 1.91 (19100) | ignored |
| `ImGuiInputTextFlags_ElideLeft` | 1.91.9 (19190) | ignored |
| Dynamic fonts (`ImFontConfig` sizing, `PushFont(font, size)`) | 1.92 (19200) | `LoadFonts` bakes each size |
| `ImTextureRef` for `Image` widgets | 1.92 (19200) | `ImTextureID` |
| Docking layouts, `DockSpace`, viewports (the builder itself; `docking` feature in projects) | `docking` branch | feature hidden |

Flags, colors and style variables added after 1.85 carry the version they first appeared in,
in the catalog (`imforge_widgets.h`), and the properties panel greys them out below that. The
catalog knows 1.92 and 1.93 too. Compiled after `imgui.h` it matches the Dear ImGui you're
running; otherwise, or with `IMFORGE_CATALOG_ALL_VERSIONS`, it's the union of all versions.
`FindFlagRename` lists renamed flags (`NavLeftJumpsBackHere` became `NavLeftJumpsToParent`,
`FittingPolicyResizeDown` became `FittingPolicyShrink`).

## Upstream changes the runtime headers guard against

These only break builds that define `IMGUI_DISABLE_OBSOLETE_FUNCTIONS`, since the old spelling
stays available otherwise. The exception is `SetCursorPos`, which asserts at run time.

| Change | Since | Handled in |
|---|---|---|
| `SetCursorPos()` past the content extent without a following item asserts (opt-in in 1.89, always on from 1.92.0) | 18900 / 19200 | `imb_anim.h` `EndEnter` |
| `ImGuiTreeNodeFlags_NavLeftJumpsBackHere` renamed to `NavLeftJumpsToParent` | 19200 | `imforge_interp_detail.h` |
| `ImGuiTabBarFlags_FittingPolicyResizeDown` renamed to `FittingPolicyShrink` (fitting policy bit values changed too) | 19220 | `imforge_interp_detail.h` |
| `ImDrawList::AddRect/AddPolyline/PathStroke`: `thickness` and `flags` swapped, old order `= delete` | 19280 | `imb_effects.h`, `imforge_interp_engine.h`, `imforge_client_detail.h`, essentials |
| `ImDrawCallback_ResetRenderState` moved to `GetPlatformIO().DrawCallback_ResetRenderState` | 19280 | `fx/imb_fx_*.h` |
| `ImDrawFlags_RoundCorners*` renamed to `ImDrawFlags_Round*` | 19300 | essentials (`detail::kRound*`) |
| `ImDrawListFlags_AntiAliasedFill` renamed to `ImDrawFlags_AAFill` | 19300 | `imb_effects.h` |

1.93.0 also changed where strokes go (`AddRect` strokes inside the rect by default, new line
renderer). That's visual only: outlines drawn by the runtime and the essentials pack sit up to
half a stroke further inside than on 1.92.

## The builder's own ImGui

The editor is built against a pinned `docking` tag (see `cmake/Dependencies.cmake`). That's
independent of what your project targets: the canvas renders your panel with the builder's
ImGui and tells you about features your target can't express.

## CI

The headers in `include/imforge/`, the essentials pack and all tests are compiled against each
of these tags on Windows (MSVC) and Linux (GCC):

`v1.85`, `v1.87`, `v1.89.9`, `v1.90.9`, `v1.91.9b-docking`, `v1.92.1`, `v1.93.0`,
`v1.93.0-docking`, `master`

The builder is only built and smoke-tested on `v1.91.9b-docking`; it doesn't compile against
1.93.0 yet (see below). On Linux, CI also runs `tools/check_version_matrix.py` (default and
`-D IMGUI_DISABLE_OBSOLETE_FUNCTIONS`) and `tools/amalgamate.py --check` against each tag.
Locally, `python tools/check_version_matrix.py` does the header compile pass against the pinned
checkouts; `-D IMGUI_DISABLE_OBSOLETE_FUNCTIONS` and `-D IMFORGE_LIVE` select the variants.

## Current results

Checked 2026-11-07. Master was `dbb30112c619990c593ab46f1855ac08d8ff1e52` (1.93.1 WIP,
`IMGUI_VERSION_NUM` 19302). The header rows cover the default build,
`IMGUI_DISABLE_OBSOLETE_FUNCTIONS` and (for the client) `IMFORGE_LIVE`, compiled with
`-fno-exceptions -fno-rtti -Wall -Wextra` on GCC 13, plus MSVC /W4 for the Windows-only fx
headers. `-` means not run. Reproduce with `python3 tools/check_version_matrix.py`.

| Module | 1.85 | 1.87 | 1.89.9 | 1.90.9 | 1.91.9b | 1.91.9b-dock | 1.92.1 | 1.93.0 | 1.93.0-dock | master |
|---|---|---|---|---|---|---|---|---|---|---|
| `fx/` (gl3, dx9, dx11, dx12, vulkan) | ok | ok | ok | ok | ok | ok | ok | ok | ok | ok |
| `imb_anim` / `imb_effects` / `imb_runtime` | ok | ok | ok | ok | ok | ok | ok | ok | ok | ok |
| `imforge_anim_names.h` | ok | ok | ok | ok | ok | ok | ok | ok | ok | ok |
| essentials pack | ok | ok | ok | ok | ok | ok | ok | ok | ok | ok |
| `imforge_client*.h` (with and without `IMFORGE_LIVE`) | ok | ok | ok | ok | ok | ok | ok | ok | ok | ok |
| `imforge_interp*.h` | ok | ok | ok | ok | ok | ok | ok | ok | ok | ok |
| value, json, core, widgets, expr, transport, protocol headers | ok | ok | ok | ok | ok | ok | ok | ok | ok | ok |
| headless tests (6 executables), default build | ok | - | - | - | ok | - | ok | ok | ok | ok |
| headless tests, `IMGUI_DISABLE_OBSOLETE_FUNCTIONS` | ok | - | - | - | ok | - | - | ok | - | ok |
| widget catalog vs. running ImGui (`widgets_tests`) | ok | - | - | - | ok | ok | ok | ok | ok | ok |
| builder (`imforge_builder`) | - | - | - | - | - | - | - | - | fails | - |

The builder doesn't compile against 1.93.0-docking: `ImFont::FontSize` was removed in 1.92, and
with `IMGUI_DISABLE_OBSOLETE_FUNCTIONS` it also uses `PushFont(font)`, the old `AddRect` /
`AddPolyline` argument order and a few other removed APIs. That only matters once the pin moves.
The builder also only sees the 1.92/1.93 colors, style vars and flags when it's compiled with
`IMFORGE_CATALOG_ALL_VERSIONS`, and the interpreter doesn't apply the new flag and style-var
names yet.

## Known limits

- 1.85 and 1.86 don't have `ImGuiKey` values for every key, so the keybind picker falls back to
  the legacy key map and can miss some keys.
- Before 1.90 `BeginChild` can't express `ChildFlags`; the builder hides that property.
- The 1.92 font API is handled by `LoadFonts`. If your app loads fonts itself, use the
  project's sizes to match.
