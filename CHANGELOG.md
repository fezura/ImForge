# Changelog

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and versions
follow [Semantic Versioning](https://semver.org/).

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
