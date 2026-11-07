# Roadmap

v1 is five milestones. A milestone counts as done when its features build on Windows and
Linux, pass the tests, and work end to end in the sample projects.

## M1: live demo

- Builder with hierarchy, palette, properties, canvas, code view, theme editor and the
  project wizard
- Stock widgets, flow layout containers and sizing (`auto`, px, `fill`, `%`, `fill-N`)
- One-way code generation with markers
- Live attach over TCP, with the DX11 (Win32) and OpenGL 3 (SDL3) sample apps
- A recording of a slider dragged in the builder moving inside a running app

## M2: real projects

- Round-trip: user slots kept, literal edits merged back, conflict panel, locked blocks
- Events, generated handlers and no-code actions
- Panel state struct, the expression language, `visibleIf` / `enabledIf`, label interpolation
- Components with parameters and slots

## M3: binding

- `imforge-reflect` on libclang with `compile_commands.json`
- Completion and type checking from `types.json` in the properties panel
- `IMFORGE_REFLECT` as the fallback without libclang; the type table is sent over the live link

## M4: look and motion

- `imb_runtime.h`: transitions with easing and springs, enter/exit presets, stagger, tab cross-fade
- DrawList effects: gradients, shadows, glow, animated borders, particles, noise, cursor highlight
- Essentials pack, icon fonts with a picker, style classes, theme import

## M5: coverage

- Shader blur on DX9, DX11, DX12, OpenGL 3 and Vulkan
- Named pipe / Unix socket and shared-memory transports
- CI against ImGui 1.85 through latest on both operating systems
- `.imblib` import and export

## v1.0

M1 to M5 hardened: no crashes in normal builder sessions, sample projects round-trip
cleanly, docs complete.

## v1.1

- ImPlot pack (line, bar and scatter bound to `std::vector<float>`)
- Getter/setter bindings and pointer members
- Per-instance component overrides (today you can only detach)
- A gallery of panel templates

## Later

- macOS builder
- ImGuizmo and imgui-node-editor packs
- Helpers for remote attach (port forwarding presets for VMs)
