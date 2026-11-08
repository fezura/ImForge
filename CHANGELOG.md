# Changelog

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and versions
follow [Semantic Versioning](https://semver.org/).

## 0.2.0 - 2026-11-08

### Added

- Motions: keyframe animations that fade, move, scale, rotate, skew, tint, clip, glow or
  type out a widget, on enter, exit, hover, press, focus, loop, change or attention triggers
  (`imb_motion.h`). See [docs/MOTION.md](docs/MOTION.md).
- 212 motion presets in `presets/animations/`, compiled into `imb_motion_presets.h`; 87 of
  them are ports of animate.css 4.1.0.
- Custom project animations (`animations/<Name>.imanim.json`) with a keyframe editor in the
  new Keyframes tab of the Anim Timeline. Export writes them to `imforge_animations.h`.
- A Motions section in Properties, with stagger for containers and an `on` event to replay a
  motion.
- 36 designs (theme, style classes, fonts, effects and default motions) that apply in one
  undo step. See [docs/DESIGNS.md](docs/DESIGNS.md).
- Three widget packs, controls, display and gaming, with 63 new widgets. Every pack now
  draws for real in the Canvas and the live client through adapter headers, and
  `imforge::live::RegisterBundledPacks()` registers them in your app. See
  [docs/WIDGETS.md](docs/WIDGETS.md).
- The Library panel (Ctrl+L) to browse, preview and apply animations, designs, widgets and
  templates.
- 12 new templates built on the packs and designs, and the `Showcase` sample project.
- 19 more easing curves (Sine, Quart, Quint, Circ and the missing In/InOut variants), plus
  `cubic-bezier`, `steps` and `spring` easings for keyframes.
- Live attach sends the project fonts to the app, Font Awesome merge included, so the app
  draws the same fonts and icons as the canvas without loading them itself. Call
  `imforge::live::PreNewFrame()` before the backend NewFrame and, below Dear ImGui 1.92, pass
  a texture rebuild function to `imforge::live::SetFontTextureRebuild()` once; the examples
  and Copy client snippet show it per renderer. The app checks every font file before Dear
  ImGui sees it. See [docs/LIVE_ATTACH.md](docs/LIVE_ATTACH.md#fonts).

### Changed

- Generated code includes `imb_motion.h` and `imb_motion_presets.h` only when a panel uses
  motions, and export copies them next to `imb_runtime.h`.
- The builder loads every bundled pack at startup, so their widgets are in the Palette
  without importing an `.imblib`.
- Generated code builds as C++14: fonts are `Font_<Name>()` accessors instead of `inline`
  variables (add the `()` where you use them), and an `a::b` namespace is written as nested
  blocks.

### Fixed

- Panels whose root nodes have the same id (every new panel starts with `window`) were drawn
  into one Dear ImGui window in the canvas and the live client, and shared motion state there
  and in generated code. The panel name is now part of those ids.

## 0.1.0 - unreleased

First public version.

### Added

- The builder (SDL3 + OpenGL 3, or Win32 + DX11): hierarchy, palette, canvas, properties,
  code view, theme editor, style classes, fonts with an icon picker, components, animation
  timeline, conflicts, diagnostics and live panels, undo/redo, and a new-project wizard with
  five templates.
- Live attach over TCP, named pipe / Unix socket or shared memory, with automatic pairing,
  auto-push and picker mode.
- Code generation with `@imb:` markers, and round-trip on re-export: user regions and
  locked blocks are kept, literal edits are read back, the rest goes to the conflicts panel.
- Binding through `IMFORGE_REFLECT` or `imforge-reflect` (libclang), with completion and
  type checking in the builder.
- `visibleIf` / `enabledIf` expressions, label interpolation, panel state, no-code actions
  and generated handlers.
- Components with parameters and slots, `.imblib` import and export.
- `imb_runtime.h` for transitions, enter/exit presets and DrawList effects, and shader blur
  for DX9, DX11, DX12, OpenGL 3 and Vulkan.
- The essentials widget pack (toggle, keybind picker, side tabs, section card, toasts and a
  few more).
- Example host apps for all five renderers, the `CheatMenu` and `ToolDock` sample projects,
  and CI against Dear ImGui 1.85 through `master` on Windows and Linux.
