# Design decisions

Short notes on why things are the way they are. New decisions go at the bottom.

### Save JSON, export C++

The builder saves a JSON project and exports plain Dear ImGui C++. There's a runtime loader for
the JSON (the interpreter), but shipping it is optional. Codegen alone gives you no way to
reopen a design, and a mandatory runtime loader is exactly the kind of dependency ImGui users
avoid. People want code they own, and we need a save format anyway.

### Live attach and real bindings are the point

The main feature is that edits show up inside your running application, with click-to-select
in the app. Next to it: binding widgets to real C++ members, with type checking. None of the
existing ImGui builders (ImRAD, ImStudio, Fellow ImGui) do either, and a web tool can't. We
looked at responsive layout and AI generation as the headline instead and passed.

### Who it's for

Mod and cheat-menu developers on one side, tool and app developers on the other. That's why
menu looks (animations, effects, blur) are first class, why the client header builds with
exceptions and RTTI off, and why old ImGui versions have to work. ImForge ships no injector, no
API hooks and nothing anti-cheat related, and won't.

### Interpret in development, compile for release

During development the client interprets the panel JSON; releases use the generated C++.
Hand-written handlers and locked blocks are compiled as named functions and registered, so they
run under the interpreter too, at the cost of a recompile when they change. An embedded C++
interpreter (cling) and DLL hot reload were both considered. Both are heavy and fragile, and
DLL reload can trip anti-cheat.

### TCP, pipes and shared memory

`tcp://` works everywhere, including to a VM or another machine. `pipe://` (named pipe on
Windows, unix socket on Linux) and `shm://` have less overhead. You pick per project, and the
URL passed to `Connect` decides at run time.

### Builder: C++, ImGui docking, SDL3 + OpenGL 3

The canvas has to be real ImGui or the preview lies, and a native tool fits the audience.
Win32 + DX11 is available as a build option. Electron or Tauri would mean a fake preview and a
heavy app; Qt is a big dependency with licensing questions.

### Windows and Linux first

macOS comes later. Nothing in the design is in the way.

### ImGui 1.85 and up

Every widget, flag and style entry records the version it first appeared in. The builder hides
or warns, and codegen and the client use `IMGUI_VERSION_NUM` guards. CI compiles the whole
matrix. Supporting only the latest release would lose a big part of the audience; supporting
everything ever released isn't maintainable.

### Round-trip through markers, not C++ parsing

The project JSON is the source of truth. Generated code carries `@imb:begin/end`, `@imb:user`
and `@imb:locked` markers. User slots are always kept. Literal edits inside generated blocks are
read back with a token-level diff against a fresh generation, and anything else is a conflict
you resolve (lock or regenerate). Locked blocks still preview through live attach because
they're registered functions. Parsing arbitrary C++ is unpredictable, and putting user code in
separate files with callbacks wouldn't round-trip.

### A folder of JSON files

`imforge.json` plus one file per panel, component and theme, with sorted keys, stable node ids
and a `schemaVersion` for migrations. One big file (binary or JSON) means merge conflicts; YAML
and TOML weren't worth a parser. We wrote our own small JSON parser so the client header has no
dependencies.

### Components with typed params and slots

A component instance is a call with parameters. If you need to change more than the parameters
allow, you unpack the instance into plain widgets. Libraries are zip files with a manifest
(`.imblib`). This keeps codegen and round-trip predictable.

### Bindings through panel parameters

Panels declare typed parameters (`player: Player&`) and widgets bind to paths under them.
Generated code takes those parameters and uses direct member access. Dev builds resolve paths
through `IMFORGE_REFLECT` tables, written by hand or produced by `imforge-reflect` from
`compile_commands.json`. Getters/setters, pointers and private members are for v2.

### Events: handler functions plus no-code actions

Every event slot is a named function that receives the panel state and parameters. It's called
inline in release and by the interpreter in dev. The built-in actions (set, toggle, open/close
popup, set tab, copy to clipboard) need no code and update live.

### Small expression language, state in a struct

`visibleIf`, `enabledIf` and label interpolation use a small typed expression language with no
calls and no assignment. Panel-local state lives in a generated `<Panel>State` struct instead of
statics, which gives you multiple instances and an easy reset.

### Flow layout by default, Canvas for absolute placement

`VStack`, `HStack`, `Grid`, `Child`, `Group`, tabs, headers, trees, popups and menus, with sizes as
`auto`, px, `fill`, `%` or `fill-N` of the available region. Absolute placement only happens
inside a `Canvas`. The generated code then looks like ordinary ImGui code and still
round-trips.

### DPI scaling from day one

Every px value is multiplied by a scale expression, `ImGui::GetFontSize() / baseFontSize` by
default, so generated code follows whatever font setup the app has.

### Themes, fonts, icons and style classes in v1

Full `ImGuiStyle` editor, multiple themes, per-widget overrides, named style classes, a font
manager with icon font merging and a picker, and import of an existing style function. The
1.92 dynamic font API is handled by a version-guarded `LoadFonts`.

### Widget SDK and packs

Custom widgets are described by `.imw.json` manifests: props, bindings, events, a call template
and preview shapes. The canvas previews them live or from the shapes. The essentials pack
(toggle, keybind picker, swatches, hotkey label, spinner, toasts, gradient header, animated
tabs) is in v1; ImPlot comes in v1.1.

### imb_runtime.h is the only shipped dependency

Animations and DrawList effects need per-frame state, so generated code that uses them includes
one small header with no networking in it. Projects without motion stay plain ImGui.

### Shader blur on all five renderers

DX9, DX11, DX12, OpenGL 3 and Vulkan. The scene is rendered, `PrepareFrame` copies the back
buffer and builds blurred mips, and ImGui draw callbacks sample them. Capturing before the ImGui
pass keeps all backends the same and avoids mid-render-pass trouble on DX12 and Vulkan. The
catch: the blur shows the scene, not other ImGui windows underneath.

### The wizard asks everything up front

Template, ImGui version and branch (detected from `imgui.h` if you point it at one), platform
and renderer, features, binding setup, live transport, codegen style and starting theme. All of
it can be changed later in project settings. Features the target can't support are greyed out
with the reason.

### Canonical files leave out defaults

`*ToJson` omits default values (zero layout offset, empty style vars, `handler: false`, empty
`params`). Examples and templates in the repo are kept in that form (`FromJson`, `ToJson`,
`DumpJson`: sorted keys, 2-space indent, trailing newline) with `imforge-fmt <dirs>`;
`imforge-fmt --check` exits 1 and lists the files that would change.

### Pairing code delivered by file, 160 bits

The live link is paired with a one-time code the builder writes to a per-user file, so nobody
types it. We first planned 40 bits, but `mac_b` lets whoever sent the hello check guesses
offline, and 40 bits falls to a GPU well within the code's 10 minute life. Since the code
travels by file, making it 160 bits costs nothing. See [SECURITY_MODEL.md](SECURITY_MODEL.md).

### MAC every message, don't encrypt

After pairing each message carries an HMAC and a sequence number. We don't encrypt: the
transports are local, the payload is UI definitions and app metadata the user's own processes
can read anyway, and only the same user or an admin can watch loopback or pipe traffic.
Integrity and replay protection are what matter, and a cipher in a header-only library would
add a lot of code without protecting against anyone new.
