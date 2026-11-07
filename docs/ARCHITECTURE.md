# Architecture

ImForge is really three programs sharing one data model: the builder (the editor), the client
(a header you compile into your app during development) and the code generator (runs inside
the builder and writes the C++ you ship).

The data model is a tree of nodes per panel, stored as JSON (the formats are in
[SPEC.md](SPEC.md#file-formats)). Everything else reads or writes that tree.

## Modules

```
include/imforge/            header-only, shared by builder and client
  imforge_json.h            JSON DOM                       no deps
  imforge_value.h           Value, CType, reflection       json
  imforge_widgets.h         stock widget catalog, flags    json, value
  imforge_core.h            Node/Panel/Component/Project   json, value, widgets
  imforge_expr.h            expression language            value
  imforge_interp.h          interpreter engine             core, expr, imgui, imb_runtime
  imforge_interp_widgets.h  per-widget drawing             interp
  imforge_transport.h       TCP / pipe / shared memory     OS sockets
  imforge_protocol.h        framing + messages             json
  imforge_pairing*.h        one-time code pairing, MACs    json, protocol, OS RNG
  imforge_client.h          live-attach API for your app   everything above
  imb_runtime.h             animation + DrawList effects   imgui only (ships with generated code)
  fx/imb_fx*.h              shader blur per renderer       imgui + graphics API (ships, optional)

src/codegen/                panel tree -> C++ files        core, expr
src/roundtrip/              C++ files -> panel edits       codegen (literal map)
src/binding/                headers -> types.json          libclang (optional)
src/builder/                the editor                     all of the above + SDL3/GL3 or Win32/DX11
packs/essentials/           extra widgets as a pack        imb_runtime
```

Dependencies only point down. Nothing in `include/imforge/` depends on `src/`.

## Designing with live attach

```
  builder (server)                transport              your app (client, dev build)
 +---------------------+   tcp:// pipe:// shm://   +-----------------------------------+
 | hierarchy  canvas   | ---- panel JSON --------> | imforge::live::NewFrame()         |
 | properties codeview | ---- theme / mode ------> |   applies messages                |
 |                     |                           | imforge::live::Draw("MainMenu")   |
 | Interpreter         |                           |   interpreter renders the tree    |
 | (design mode: mock  | <--- select / hover ----- |   bindings via reflection table   |
 |  bindings, click to | <--- blocks + hashes ---- |   handlers and locked blocks are  |
 |  select)            | <--- client_info/stats -- |   your compiled functions         |
 +---------------------+                           +-----------------------------------+
```

Both sides run the same `imforge_interp.h`. The builder canvas is the interpreter in design
mode: bindings point at mock storage and item rectangles are reported back for the selection
overlay. The app runs it with real pointers. Before any of this happens the two sides pair
with a one-time code and from then on every message carries a MAC (see
[SECURITY_MODEL.md](SECURITY_MODEL.md)).

## Export and round-trip

```
 panels/*.imb.json --> codegen --> generated/MainMenu.cpp   (plain ImGui calls)
        ^                 |   '-- literal map: node id, prop, token index
        |                 v
        |           @imb:begin / @imb:user / @imb:locked markers
        |                 |
        |           you edit MainMenu.cpp by hand
        |                 |
        '---- roundtrip <-'
```

On the way back, user slots and locked bodies are carried into the fresh output. Blocks whose
hash changed are tokenized and compared with the fresh block; if only literals differ, the new
values go back into the panel's props. Anything else is a conflict you resolve in the builder
(keep your code, which locks the block, or regenerate). Code generation is deterministic, so a
block hash only changes when the project or the user changed it. [CODEGEN.md](CODEGEN.md) has
the details.

## Bindings

```
 your headers --> imforge-reflect (libclang) --> types.json --> builder autocomplete, type checks
                                             '-> imforge_reflect.h (IMFORGE_REFLECT macros)
                                                        |
 your app, dev build:     offsetof-based StructInfo registry, interpreter resolves "player.health"
 your app, release build: generated code uses &player.health directly, no reflection
```

Without libclang you write `IMFORGE_REFLECT(Player, health, ammo, ...)` by hand and get the same
table; the dev glue can send it to the builder over the live link.

## Inside the builder

`EditorContext` owns the `Project`, the selection, the undo stack (JSON snapshots per panel,
merged while dragging), the live server, the type database and one `PanelInstance` per open
panel. The editor's own panels are independent classes with `Draw(EditorContext&)`: Hierarchy,
Palette, Properties, Canvas, CodeView, ThemeEditor, Fonts, Wizard, ProjectSettings, Live,
Components, AnimTimeline, Conflicts, Diagnostics and Log. Every change goes through a command,
which keeps undo, the dirty flag and live pushes in sync.

## Why the client interprets

You can't reload C++ cheaply. DLL hot reload is fragile on both Windows and Linux and tends to
upset anti-cheat software. Interpreting the panel definition keeps the loop at "edit, see it"
with no compile at all, and what you ship is still plain generated C++. The only things that
need a recompile are hand-written handlers and locked blocks, and those are ordinary functions
the interpreter calls by name.

## ImGui versions

The `imforge_*` headers and generated code gate API differences on `IMGUI_VERSION_NUM`. The
builder itself is built against a pinned docking tag, and the version a project targets is a
separate choice made in the wizard. CI compiles the headers and tests on Windows and Linux
against 1.85, 1.87, 1.89.9, 1.90.9, 1.91.9b-docking, 1.92.1, 1.93.0, 1.93.0-docking and
`master`; see [VERSION_MATRIX.md](VERSION_MATRIX.md).
