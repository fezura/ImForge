# ImForge design spec

ImForge is a visual UI builder for Dear ImGui. You design panels in a native editor, see the
edits live inside your running app, bind widgets to your real C++ variables and export plain
ImGui C++ that can be read back into the builder.

This file is the reference for how the modules talk to each other: file formats, the widget
catalog, the public C++ API of each module and the live-attach protocol. The other docs explain
how to use things; this one says what they are. If you change a public signature or a file
format, update this file in the same change. Where something here is ambiguous, go with the
simplest reading that doesn't break another module and add a line to
[DECISIONS.md](DECISIONS.md).

## Conventions

C++17 throughout. Anything that ends up in a user's app (`include/imforge/*.h`, `packs/`,
generated code) has to build with `-fno-exceptions -fno-rtti`, without `<iostream>`, `<regex>`
or `<filesystem>`, and keeps `std::function` out of the runtime, transports and packs. Those
headers are self-contained and `inline`, with no mutable globals, and they support Dear ImGui
1.85 and up (master and docking) through `IMGUI_VERSION_NUM` and `IMGUI_HAS_DOCK` guards.
Errors come back as `bool` plus an `imforge::Error` or a list of `Diagnostic`s, never as
exceptions.

Namespaces: `imforge::` for tooling and internals, `imforge::live` for the client API, `imb::`
for the shipped runtime, `imb::fx` for shader effects, `imb::ess` for the essentials pack.
Generated code never depends on `imforge::`. Serialization and codegen are deterministic
(sorted keys, `%.6g`-trimmed floats, `\n` line endings). Formatting follows `.clang-format`, and
each module has a doctest suite registered with `imforge_add_test(<module>)`.

## Repository layout

```
ImForge/
  CMakeLists.txt, CMakePresets.json
  cmake/                  Dependencies.cmake (pinned imgui tag, SDL3, doctest, miniz, fonts), ImForgeTest.cmake, ...
  include/imforge/
    imforge_json.h        small JSON DOM, no deps
    imforge_value*.h      Value, CType, StructInfo/FieldInfo, IMFORGE_REFLECT
    imforge_core*.h       Node/Panel/Component/Project/Theme model, JSON conversion, validation
    imforge_widgets*.h    stock widget catalog, PropDef schema, flag tables
    imforge_expr*.h       expression lexer, parser, type check, eval, C++ emit
    imforge_interp*.h     interpreter engine and per-widget draw functions
    imforge_transport*.h  Transport interface, TCP, named pipe / unix socket, shared memory
    imforge_protocol.h    framing and message builders/parsers
    imforge_pairing*.h    one-time code, pairing file, handshake, session channel
    imforge_client*.h     live-attach client
    imb_runtime.h         shipped runtime (imb_anim.h + imb_effects.h)
    fx/imb_fx.h           shader fx API; fx/imb_fx_{dx9,dx11,dx12,gl3,vulkan}.h backends
  src/
    codegen/              static lib imforge_codegen
    roundtrip/            static lib imforge_roundtrip: marker parser, tolerant merge
    binding/              static lib imforge_binding (libclang optional) + imforge-reflect CLI
    builder/              the editor, imforge_builder
  packs/essentials/       imb_essentials.h, widgets/*.imw.json, imblib manifest
  packs/implot/           planned for v1.1, README only
  examples/client/        live-attach host apps: dx9/dx11/dx12 (Win32), gl3/vulkan (SDL3)
  examples/fx/            blur demos per backend
  examples/projects/      CheatMenu and ToolDock sample projects
  schemas/                JSON Schema (draft 2020-12) for every file format
  templates/              new-project wizard templates
  tests/                  doctest suites, imgui_headless.h
  tools/                  amalgamate.py, pack_imblib.py, validate_json.py, check_version_matrix.py,
                          package_release.py, imforge_fmt, imforge_fake_builder
  docs/
```

## File formats

All project files are UTF-8 JSON with 2-space indent, sorted keys and a trailing newline. Each
one has `"schemaVersion": 1` and a `"kind"`. Keys holding default values are left out when
saving.

### Identifiers and value types

A node id matches `[A-Za-z_][A-Za-z0-9_]*` and is unique within its panel or component. The
builder generates `<typeLowerCamel>_<n>` (`button_3`) and you can rename it. Ids show up in
code markers, `PushID` calls and protocol messages, so they matter.

A binding path is `root(.field|[index])*`, where `root` is a panel param name or `state`:
`player.health`, `cfg.esp.enabled`, `state.selectedTab`, `inv.items[2].name`. `items.size` is a
pseudo-field on vectors and arrays.

Colors are always `"#RRGGBBAA"` strings. Vec2 and Vec4 are `{"x":0,"y":0}` and
`{"x":0,"y":0,"z":0,"w":0}`. Flags are arrays of flag names without the `ImGui*Flags_` prefix,
e.g. `["NoResize","NoCollapse"]`.

A SizeSpec is a string or number: `"auto"`, a number (px, DPI-scaled), `"fill"`, `"<n>%"` or
`"fill-<n>"` (fill minus n scaled px).

Any prop of type `expr` or `string` can be written as `{"expr": "..."}` to compute it at run
time with the [expression language](#expressions). String props also support `{...}`
interpolation, which is a separate mechanism (see [string interpolation](#string-interpolation)).

### Project file

`imforge.json` at the project root:

```json
{
  "schemaVersion": 1, "kind": "project", "name": "CheatMenu",
  "imgui": { "version": "1.91.9", "branch": "docking" },
  "platform": { "window": "win32", "renderer": "dx11" },
  "features": { "animations": true, "drawlistFx": true, "shaderFx": true, "iconFonts": true,
                "dpiScaling": true, "docking": false },
  "codegen": { "cppStandard": 17, "namespace": "ui", "splitHeader": true,
               "functionCase": "pascal", "variableCase": "snake", "indent": 4, "useTabs": false,
               "outputDir": "generated", "markers": true, "dpiMode": "fontSize", "baseFontSize": 13,
               "runtimeHeader": "imb_runtime.h", "stdString": true },
  "binding": { "compileCommands": "build/compile_commands.json", "includePaths": [], "types": ["Player","Settings"],
               "typesJson": "generated/types.json" },
  "live": { "enabled": true, "transport": "tcp", "tcpPort": 7777, "pipeName": "ImForge", "shmName": "ImForge" },
  "fonts": [ { "name": "Default", "file": "fonts/Inter-Regular.ttf", "size": 16, "ranges": "default",
               "mergeIcons": "fontawesome6" } ],
  "themes": [ "themes/Dark.theme.json" ], "activeTheme": "Dark",
  "styleClasses": { "danger": { "colors": { "Button": "#c0392bff" }, "vars": { "FrameRounding": 6 } } },
  "panels": [ "panels/MainMenu.imb.json" ], "components": [ "components/LabeledSlider.imc.json" ],
  "libraries": [ "libs/essentials.imblib" ]
}
```

`platform.window` is one of `win32`, `sdl2`, `sdl3`, `glfw`; `platform.renderer` one of `dx9`,
`dx11`, `dx12`, `gl3`, `vulkan`. `codegen.dpiMode` is `none`, `fontSize` or `variable` (the last
adds a `float imb_scale` parameter to every draw function). File lists are relative paths
inside the project; the loader refuses absolute paths and `..`.

### Panel files

`panels/<Name>.imb.json`:

```json
{
  "schemaVersion": 1, "kind": "panel", "name": "MainMenu",
  "params": [ { "name": "player", "type": "Player" }, { "name": "cfg", "type": "Settings" } ],
  "state":  [ { "name": "selectedTab", "type": "int", "default": 0 },
              { "name": "open", "type": "bool", "default": true } ],
  "root": { "id": "window", "type": "Window", "props": { "title": "Main Menu", "flags": ["NoCollapse"],
            "initialSize": {"x": 520, "y": 380}, "sizeCond": "FirstUseEver" },
            "bind": { "open": "state.open" }, "children": [ ... ] }
}
```

`params[].type` is a C++ type name, passed by reference in generated code. `state[].type` is
one of `bool`, `int`, `float`, `double`, `string`, `vec2`, `vec4`, `color`.

### Nodes

```json
{
  "id": "btn_play", "type": "Button", "name": "Play button",
  "props": { "label": "{icon:fa-play} Play", "size": { "w": "fill", "h": "auto" } },
  "bind": { "value": "player.health" },
  "visibleIf": "cfg.esp.enabled", "enabledIf": "player.health > 0",
  "tooltip": "Starts the game", "font": "Bold",
  "style": { "colors": { "Button": "#2ecc71ff" }, "vars": { "FrameRounding": 8 }, "classes": ["danger"] },
  "events": { "onClick": { "actions": [ { "type": "toggle", "target": "cfg.esp.enabled" } ], "handler": true } },
  "anim": { "transitions": [ { "prop": "color.Button", "on": "hover", "to": "#27ae60ff", "duration": 0.15, "ease": "OutCubic" } ],
            "enter": { "preset": "FadeSlideUp", "duration": 0.25, "stagger": 0.03 } },
  "fx": [ { "type": "shadow", "size": 12, "color": "#00000080" }, { "type": "blur", "radius": 8 } ],
  "layout": { "anchor": "TopRight", "offset": { "x": -10, "y": 10 } },
  "locked": false,
  "children": []
}
```

Only `id` and `type` are required. `layout` only means something for direct children of a
`Canvas`. `style.vars` values are numbers or Vec2, depending on the style var. Which `events`
keys exist depends on the widget type (see the [catalog](#stock-widgets)); an event with no
actions and no `"handler": true` is ignored. Animation and effect fields are described in
[ANIMATIONS.md](ANIMATIONS.md).

Actions, in `events.<event>.actions[]`:

| type | fields | does |
|---|---|---|
| `set` | `target` (path), `value` (literal or `{"expr":...}`) | assigns |
| `toggle` | `target` (bool path) | flips |
| `openPopup` | `target` (id of a `Popup` or `Modal`) | `ImGui::OpenPopup` |
| `closePopup` | | `ImGui::CloseCurrentPopup` |
| `setTab` | `target` (TabBar id), `value` (Tab id) | selects the tab next frame |
| `copyClipboard` | `value` (string or expr) | `ImGui::SetClipboardText` |
| `handler` | | calls the generated handler, same as `"handler": true` |

### Components

`components/<Name>.imc.json`:

```json
{
  "schemaVersion": 1, "kind": "component", "name": "LabeledSlider",
  "params": [ { "name": "label", "type": "string", "default": "Value" },
              { "name": "value", "type": "binding<float>" },
              { "name": "min", "type": "float", "default": 0 }, { "name": "max", "type": "float", "default": 1 } ],
  "slots": [ "content" ],
  "root": { "id": "row", "type": "HStack", "children": [
     { "id": "lbl", "type": "Text", "props": { "text": { "param": "label" } } },
     { "id": "sl",  "type": "SliderFloat", "props": { "label": "##v", "min": { "param": "min" }, "max": { "param": "max" } },
       "bind": { "value": { "param": "value" } } },
     { "id": "slot_content", "type": "Slot", "props": { "name": "content" } } ] }
}
```

Param types are `string`, `int`, `float`, `bool`, `color`, `vec2`, `vec4`, `enum:<A,B,C>` and
`binding<T>` with T one of `bool|int|float|string|vec2|vec4|color|any`. Inside the component
tree any prop or bind value can be `{"param":"name"}`. An instance is a node like
`{"type":"Component","component":"LabeledSlider","props":{...param values...},"slots":{"content":[...nodes...]}}`.
More in [COMPONENTS.md](COMPONENTS.md).

### Widget manifests

A custom widget is described by a `*.imw.json` manifest (the Widget SDK, see
[WIDGET_SDK.md](WIDGET_SDK.md)):

```json
{
  "schemaVersion": 1, "kind": "widget", "name": "Toggle", "category": "Essentials",
  "include": "imb_essentials.h", "minImGuiVersion": 18500,
  "props": [ { "name": "label", "type": "string", "default": "Toggle" },
             { "name": "size", "type": "size", "default": {"w":"auto","h":"auto"} },
             { "name": "onColor", "type": "color", "default": "#2ecc71ff" } ],
  "bindings": [ { "name": "value", "types": ["bool"], "required": true } ],
  "events": [ "onChange" ],
  "container": false,
  "call": "imb::ess::Toggle({label}, {value}, {size}, {onColor})",
  "returns": "changed",
  "preview": [ { "shape": "rect", "rounding": 10, "fill": "{onColor}", "w": 40, "h": 20 },
               { "shape": "circle", "fill": "#ffffffff", "r": 8, "x": 30, "y": 10 } ]
}
```

Each `{prop}` in `call` is replaced by the C++ expression for that prop: strings get quoted,
colors become `ImVec4(...)` or `IM_COL32(...)`, sizes `ImVec2(...)`, bindings a pointer
expression. `returns` is `changed`, `clicked`, `open` or `void`. `preview` is a list of
DrawList shapes the canvas draws when no app is attached. Containers may add a `callEnd`
statement that runs after the children.

### Themes

`themes/<Name>.theme.json`:

```json
{ "schemaVersion": 1, "kind": "theme", "name": "Dark",
  "colors": { "<ImGuiCol name>": "#rrggbbaa" },
  "vars": { "<ImGuiStyleVar name>": 4, "<vec2 var>": { "x": 8, "y": 4 } } }
```

Names the catalog doesn't know (from a newer ImGui) are kept and get a version guard in
generated code.

### .imblib libraries

An `.imblib` is a zip with a `manifest.json` at the root, plus the `.imc.json`, `.imw.json` and
`.h` files it lists:

```json
{ "schemaVersion": 1, "kind": "imblib", "name": "essentials", "version": "1.0.0",
  "minImGuiVersion": 18500, "components": [ ... ], "widgets": [ ... ], "headers": [ ... ],
  "dependencies": [] }
```

What the importer accepts is described in [SECURITY_MODEL.md](SECURITY_MODEL.md#imblib-import).

## Stock widgets

Every stock widget takes the common node fields. Widgets marked (S) have a `size` prop, a
`{w,h}` pair of SizeSpecs. The bind column lists the slot and the C types it accepts. The
interpreter, the code generator and the builder all implement exactly this list; the table in
`imforge_widgets.h` is the single source for prop defaults, binding slots and events.

### Containers

These accept children unless noted.

| type | props | bind | events | notes |
|---|---|---|---|---|
| `Window` | title(string), flags(WindowFlags), initialSize(vec2), initialPos(vec2), sizeCond(Always/Once/FirstUseEver/Appearing), menuBar(bool) | open(bool, optional) | onOpen, onClose | panel root only; the builder canvas draws it as a child region |
| `VStack` | spacing(float, null = default), indent(float, 0) | | | the default flow |
| `HStack` | spacing(float), align(Start/Center/End) | | | SameLine between children; `fill` widths share the leftover space |
| `Group` | | | | BeginGroup/EndGroup |
| `Child` (S) | border(bool), flags(WindowFlags), childFlags(ChildFlags, 1.90+) | | | defaults to fill/fill |
| `Grid` | columns(int, 2), flags(TableFlags), headers(list of string, optional), columnWidths(list of SizeSpec, optional) | | | BeginTable; each child is one cell, row-major |
| `TabBar` | flags(TabBarFlags) | | onChange | children must be `Tab` |
| `Tab` | label(string), flags(TabItemFlags), closable(bool) | open(bool) | onOpen, onClose | |
| `CollapsingHeader` | label, defaultOpen(bool), flags(TreeNodeFlags) | | onOpen, onClose | |
| `TreeNode` | label, defaultOpen, flags(TreeNodeFlags) | | onOpen, onClose | |
| `Popup` | flags(WindowFlags) | | onOpen, onClose | opened by an `openPopup` action; popup id = node id |
| `Modal` | title, flags, closable(bool) | open(bool) | onOpen, onClose | BeginPopupModal |
| `MenuBar` | | | | child of a Window with menuBar=true |
| `Menu` | label, enabled(bool) | | | children: MenuItem, Menu, Separator |
| `Canvas` (S) | border(bool) | | | children placed by `layout.anchor` / `layout.offset` |
| `Component` | `component` (name) plus param values in props | | | expanded from the definition |
| `Slot` | name(string) | | | only inside component definitions, no children |
| `Code` | code(string), preview(Placeholder/Live) | | | locked block, no children |

### Widgets

None of these take children.

| type | props | bind | events |
|---|---|---|---|
| `Text` | text(string, interpolated), color(color, optional), wrapped(bool), bullet(bool), disabled(bool) | | |
| `SeparatorText` | text | | (1.89.5+, falls back to Separator + Text) |
| `Separator` | | | |
| `Spacing` | count(int, 1) | | |
| `NewLine` | | | |
| `Dummy` (S) | | | |
| `Button` (S) | label, repeat(bool) | | onClick |
| `SmallButton` | label | | onClick |
| `ArrowButton` | dir(Left/Right/Up/Down) | | onClick |
| `Checkbox` | label | value(bool) | onChange |
| `RadioButton` | label, value(int) | value(int) | onChange |
| `Combo` | label, items(list of string), flags(ComboFlags) | value(int or enum) | onChange |
| `ListBox` | label, items(list of string), heightItems(int, -1) | value(int or enum) | onChange |
| `SliderFloat` | label, min(float, 0), max(float, 1), format(string, "%.3f"), flags(SliderFlags), components(int 1..4) | value(float, float[N], vec2, vec4) | onChange |
| `SliderInt` | label, min(int, 0), max(int, 100), format("%d"), flags, components | value(int, int[N]) | onChange |
| `VSliderFloat` (S) | label, min, max, format, flags | value(float) | onChange |
| `VSliderInt` (S) | label, min, max, format, flags | value(int) | onChange |
| `DragFloat` | label, speed(float, 1), min(0), max(0), format, flags, components | value(float...) | onChange |
| `DragInt` | label, speed, min, max, format, flags, components | value(int...) | onChange |
| `InputText` | label, hint(string), flags(InputTextFlags), maxLength(int, 256) | value(string, char[N]) | onChange, onEnter |
| `InputTextMultiline` (S) | label, flags, maxLength | value(string, char[N]) | onChange |
| `InputInt` | label, step(int, 1), stepFast(int, 100), flags, components | value(int...) | onChange |
| `InputFloat` | label, step(float, 0), stepFast(float, 0), format, flags, components | value(float...) | onChange |
| `ColorEdit` | label, alpha(bool, true), flags(ColorEditFlags) | value(vec4, float[4], float[3]) | onChange |
| `ColorPicker` | label, alpha, flags | value(vec4, float[4], float[3]) | onChange |
| `ColorButton` (S) | desc(string), color(color), flags | | onClick |
| `ProgressBar` (S) | fraction(float or expr), overlay(string) | value(float, optional, overrides fraction) | |
| `Image` (S) | uv0(vec2), uv1(vec2), tint(color), border(color) | texture(texture) | |
| `ImageButton` (S) | uv0, uv1, bg(color), tint(color) | texture(texture) | onClick |
| `Selectable` (S) | label, flags(SelectableFlags) | selected(bool, optional) | onClick |
| `MenuItem` | label, shortcut(string), enabled(bool) | selected(bool, optional) | onClick |
| `PlotLines` (S) | label, overlay(string), scaleMin(float), scaleMax(float) | values(float[] or vector<float>) | |
| `PlotHistogram` (S) | same as PlotLines | values | |

### Labels, IDs and flags

The interpreter wraps every node in `PushID(id)`/`PopID()`. Generated code is lighter: it only
appends `###<id>` to a label when two siblings would collide, and wraps Component instances and
`Grid` cells in `PushID`.

`components > 1` picks `SliderFloat2/3/4` and friends; the binding must then be `float[N]`,
`ImVec2` (N=2) or `ImVec4` (N=4). An `InputText` bound to a `std::string` uses
`misc/cpp/imgui_stdlib.h` when `codegen.stdString` is on, otherwise a `char[maxLength]` buffer.
A `Combo` bound to an enum with no `items` lists the enum's value names from reflection. Enum
props (`sizeCond`, `dir`, `align`) are stored as their name strings.

Flag props use ImGui enum names without the prefix. `imforge_widgets.h` has
`FlagsFor(enumName, n)` returning `{ name, value, min_version }` entries so the builder,
interpreter and codegen agree. The value table is hard-coded from ImGui 1.91; when compiled
after `imgui.h` the interpreter uses the running ImGui's enums, and codegen emits names. Flags
the running ImGui doesn't have are ignored by the interpreter.

### Sizing

The interpreter and the code generator must produce the same numbers. `S` is the DPI scale:
`ImGui::GetFontSize() / baseFontSize` with `dpiMode=fontSize`, `1` with `none`, and the
`imb_scale` parameter with `variable`.

| SizeSpec | width | height |
|---|---|---|
| `auto` | `0` | `0` |
| `N` | `N * S` | `N * S` |
| `fill` | `-FLT_MIN` | `-FLT_MIN` |
| `P%` | `ImGui::GetContentRegionAvail().x * P/100` | `.y * P/100` |
| `fill-N` | `ImMax(ImGui::GetContentRegionAvail().x - N*S, 1.0f)` | `.y - N*S` |

In an `HStack` with k `fill`-width children out of n, each gets
`(avail.x - fixed_total - spacing*(n-1)) / k`, computed once before drawing the children.
`auto` children count as 0 in `fixed_total`; that's a known limitation.

`Canvas` children are placed at
`anchor_point(canvas_rect, anchor) + offset*S - anchor_fraction*child_size`, where
`child_size` is the child's explicit size (`auto` counts as 0).

## Core API

These four headers make up the data model. They're shared by the builder, the client and the
code generator.

### imforge_json.h

```cpp
namespace imforge {
struct Json {
    enum class Kind { Null, Bool, Number, String, Array, Object };
    Kind kind = Kind::Null; bool b = false; double num = 0; std::string str;
    std::vector<Json> arr; std::vector<std::pair<std::string, Json>> obj;   // insertion order kept
    Json(); Json(bool); Json(int); Json(double); Json(const char*); Json(std::string);
    static Json Array(); static Json Object();
    bool IsNull/IsBool/IsNumber/IsString/IsArray/IsObject() const;
    bool AsBool(bool def=false) const; int AsInt(int def=0) const; double AsDouble(double def=0) const;
    const std::string& AsString() const; std::string AsString(const std::string& def) const;
    const Json& operator[](const char* key) const;  // static Null if missing / not object
    const Json& operator[](size_t i) const;
    Json* Find(const char* key); const Json* Find(const char* key) const; bool Has(const char* key) const;
    Json& Set(const std::string& key, Json v);      // replaces existing key
    Json& Push(Json v); size_t Size() const; void Erase(const char* key);
    bool operator==(const Json&) const;
};
struct JsonError { std::string message; int line = 0, col = 0; };
inline bool ParseJson(const char* text, size_t len, Json& out, JsonError* err = nullptr);
inline bool ParseJson(const std::string& text, Json& out, JsonError* err = nullptr);
inline std::string DumpJson(const Json& j, int indent = 2, bool sort_keys = true);   // indent 0 = compact
}
```

Numbers parse as double. Integers are dumped without a decimal point; everything else gets the
shortest of `%.15g`, `%.16g`, `%.17g` that round-trips. Strings escape `"\\/\b\f\n\r\t` and
control characters as `\u00XX`. The parser stops at 256 levels of nesting and 500k values.

### imforge_value.h

```cpp
namespace imforge {
struct Error { std::string message; int line=0, col=0; };
struct Diagnostic { enum class Level { Info, Warning, Error } level; std::string message, panel, node_id, prop; };
struct Vec2 { float x=0,y=0; }; struct Vec4 { float x=0,y=0,z=0,w=0; };
struct Value {   // literals, props, expression results, state storage
    enum class Kind { Null, Bool, Int, Float, String, Vec2, Vec4, Color, List };
    Kind kind = Kind::Null; bool b=false; long long i=0; double f=0; std::string s; Vec2 v2; Vec4 v4; std::vector<Value> list;
    // constructors from each type; ToDouble(), ToBool(), ToString(), FromJson(const Json&, Kind hint), ToJson()
    // Color is stored in v4 (0..1 floats) and serialized as "#rrggbbaa"
};
enum class CKind { Bool, I8, U8, I16, U16, I32, U32, I64, U64, Float, Double, StdString, CharArray,
                   Vec2, Vec4, FloatArray, IntArray, Enum, Struct, Vector, Array, TexID, Unknown };
struct StructInfo; struct EnumInfo { const char* name; std::vector<std::pair<std::string,long long>> values; };
struct CType { CKind kind = CKind::Unknown; int count = 0;  /* CharArray/FloatArray/IntArray/Array length */
               const StructInfo* strct = nullptr; const EnumInfo* enm = nullptr; const CType* elem = nullptr; /* Vector/Array */
               std::string name;  /* C++ spelling, e.g. "float", "Player" */ };
struct FieldInfo { std::string name; CType type; size_t offset; };
struct StructInfo { std::string name; size_t size; std::vector<FieldInfo> fields; };
struct TypeRegistry { /* RegisterStruct/RegisterEnum/FindStruct/FindEnum; static TypeRegistry& Global(); */ };
template<class T> CType CTypeOf();           // fundamentals, std::string, ImVec2/ImVec4, T[N], std::vector<T>, registered structs/enums
#define IMFORGE_REFLECT(Type, ...)           // StructInfo via offsetof + CTypeOf per field (up to 32 fields)
#define IMFORGE_REFLECT_ENUM(Enum, ...)      // EnumInfo with names and values
inline bool ResolvePath(void* root, const StructInfo* root_type, const char* path, void** out_ptr, CType* out_type, std::string* err);
inline bool ReadValue(const void* ptr, const CType& t, Value& out);
inline bool WriteValue(void* ptr, const CType& t, const Value& v);
}
```

`Error` and `Diagnostic` live here; `imforge_core.h` reuses them. `ResolvePath` walks a binding
path from a root pointer and returns the field's address and type. `ReadValue`/`WriteValue`
convert between `Value` and typed storage (int widths, float/double, strings, arrays, enums).

### imforge_core.h

`imforge_core.h` includes the JSON, value and widgets headers. Files are loaded by the builder;
core only converts `Json` objects to and from the model.

```cpp
namespace imforge {
struct SizeSpec { enum class Mode { Auto, Px, Fill, Percent, FillMinus } mode = Mode::Auto; float value = 0;
                  static bool Parse(const Json&, SizeSpec&); Json ToJson() const; std::string ToString() const; };
struct PropValue { Json raw; /* literal, {"expr":...} or {"param":...} */ bool IsExpr() const; bool IsParam() const;
                   std::string Expr() const; std::string Param() const; };
struct Action { std::string type, target; Json value; };
struct EventSpec { std::vector<Action> actions; bool handler = false; };
struct StyleOverride { std::vector<std::pair<std::string,Vec4>> colors; std::vector<std::pair<std::string,Json>> vars; std::vector<std::string> classes; };
struct Transition { std::string prop, on; Json to; float duration = 0.15f; std::string ease = "OutCubic"; float stiffness=0, damping=0; };
struct AnimSpec { std::vector<Transition> transitions; std::string enter_preset, exit_preset; float enter_duration=0.25f, exit_duration=0.2f, stagger=0; };
struct FxSpec { std::string type; Json params; };
struct Layout { std::string anchor = "TopLeft"; Vec2 offset; };
struct Node {
    std::string id, type, name, component;
    std::vector<std::pair<std::string, PropValue>> props;     // ordered, unique keys
    std::vector<std::pair<std::string, PropValue>> bind;      // slot -> path (raw string or {"param":..})
    std::string visible_if, enabled_if, tooltip, font;
    StyleOverride style; std::vector<std::pair<std::string, EventSpec>> events; AnimSpec anim; std::vector<FxSpec> fx;
    Layout layout; bool locked = false; std::vector<Node> children;
    std::vector<std::pair<std::string, std::vector<Node>>> slots;   // Component instances
    const PropValue* Prop(const char* name) const; PropValue* Prop(const char* name); void SetProp(const char* name, Json v); void RemoveProp(const char*);
    const PropValue* Bind(const char* slot) const;
};
struct Param { std::string name, type; Json def; };     // panel params (C++ type) and component params
struct StateVar { std::string name, type; Json def; };
struct Panel { std::string name, file; std::vector<Param> params; std::vector<StateVar> state; Node root; };
struct Component { std::string name, file; std::vector<Param> params; std::vector<std::string> slots; Node root; };
struct Theme { std::string name, file; std::vector<std::pair<std::string,Vec4>> colors; std::vector<std::pair<std::string,Json>> vars; };
struct FontSpec { std::string name, file, ranges, merge_icons; float size = 16; };
struct CodegenSettings { int cpp_standard=17; std::string nmspace="ui", function_case="pascal", variable_case="snake", output_dir="generated", dpi_mode="fontSize", runtime_header="imb_runtime.h"; bool split_header=true, use_tabs=false, markers=true, std_string=true; int indent=4; float base_font_size=13; };
struct LiveSettings { bool enabled=true; std::string transport="tcp", pipe_name="ImForge", shm_name="ImForge"; int tcp_port=7777; };
struct Project {
    std::string name, dir, imgui_version="1.91.9", imgui_branch="docking", window="sdl3", renderer="gl3";
    std::vector<std::pair<std::string,bool>> features; CodegenSettings codegen; LiveSettings live;
    std::vector<std::string> binding_types, include_paths; std::string compile_commands, types_json;
    std::vector<FontSpec> fonts; std::vector<Theme> themes; std::string active_theme;
    std::vector<std::pair<std::string, StyleOverride>> style_classes;
    std::vector<Panel> panels; std::vector<Component> components; std::vector<std::string> libraries;
    int ImGuiVersionNum() const;    // "1.91.9" -> 19190 ; "1.85" -> 18500
};
bool NodeFromJson(const Json&, Node&, Error*); Json NodeToJson(const Node&);
bool PanelFromJson(const Json&, Panel&, Error*); Json PanelToJson(const Panel&);
bool ComponentFromJson(const Json&, Component&, Error*); Json ComponentToJson(const Component&);
bool ThemeFromJson(const Json&, Theme&, Error*); Json ThemeToJson(const Theme&);
bool ProjectFromJson(const Json&, Project&, Error*); Json ProjectToJson(const Project&);  // file lists only; the loader fills them
Node* FindNode(Node& root, const std::string& id); const Node* FindNode(const Node& root, const std::string& id);
Node* FindParent(Node& root, const std::string& id, size_t* index_out);
template<class F> void Walk(Node& root, F&& f); template<class F> void Walk(const Node& root, F&& f);
std::string GenerateId(const Node& root, const std::string& type);   // "button_1", unique
void CollectIds(const Node& root, std::vector<std::string>& out);
bool ValidatePanel(const Project&, const Panel&, std::vector<Diagnostic>& out);  // unknown types/props, duplicate ids, Tab outside TabBar, ...
Vec4 ParseColor(const std::string& hex, bool* ok=nullptr); std::string ColorToHex(const Vec4&);
}
```

The `*ToJson` functions leave out default-valued keys (zero layout offset, empty style vars,
`handler: false`, empty `params`). Checked-in examples and templates are kept in that canonical
form with `imforge-fmt`.

### imforge_widgets.h

```cpp
namespace imforge {
enum class PropType { Bool, Int, Float, String, Color, Vec2, Vec4, Size, Enum, Flags, StringList, SizeList, Expr, Code };
struct PropDef { const char* name; PropType type; const char* def_json; const char* enum_values; /* "A|B|C" or flags enum name */ float min, max; int min_version; const char* hint; };
struct BindDef { const char* name; const char* types; /* "float|float[N]|vec2|vec4" */ bool required; };
struct WidgetDef { const char* type; const char* category; const PropDef* props; int prop_count; const BindDef* binds; int bind_count;
                   const char* const* events; int event_count; bool container; bool sizable; const char* child_types; /* "Tab" or nullptr */ int min_version; const char* icon; };
const WidgetDef* StockWidgets(size_t& count); const WidgetDef* FindWidget(const char* type);
struct FlagDef { const char* name; int value; int min_version; };
const FlagDef* FlagsFor(const char* enum_name, size_t& count);   // "WindowFlags", "TableFlags", "TabBarFlags", "TabItemFlags", "TreeNodeFlags",
                                                                 // "ComboFlags", "SliderFlags", "InputTextFlags", "ColorEditFlags", "SelectableFlags", "ChildFlags"
const char* const* ImGuiColNames(size_t& count); const char* const* ImGuiStyleVarNames(size_t& count); bool StyleVarIsVec2(const char* name);
// runtime registry, so .imw.json manifests extend the catalog
struct CustomWidgetDef { std::string type, category, include, call, returns; Json manifest; std::vector<PropDef> props; /* ...owned strings */ };
struct WidgetCatalog { std::vector<CustomWidgetDef> custom; bool AddManifest(const Json&, Error*); const WidgetDef* Find(const char*) const; const CustomWidgetDef* FindCustom(const char*) const; };
}
```

## Expressions

`imforge_expr.h` implements a small typed language used by `visibleIf`, `enabledIf`, `{"expr"}`
props and label interpolation. No assignment, no function calls.

### Grammar

```
expr     := or ;  or := and ('||' and)* ;  and := eq ('&&' eq)* ;
eq       := rel (('=='|'!=') rel)* ;  rel := add (('<'|'<='|'>'|'>=') add)* ;
add      := mul (('+'|'-') mul)* ;  mul := unary (('*'|'/'|'%') unary)* ;
unary    := ('!'|'-') unary | primary ;
primary  := NUMBER | STRING | 'true' | 'false' | path | IDENT '::' IDENT | '(' expr ')' ;
path     := IDENT ('.' IDENT | '[' expr ']')* ;
```

Types are `bool`, `int`, `float`, `string` and `enum`. Arithmetic works on int and float with
promotion, `+` on two strings concatenates, comparisons work on numbers, strings and enums,
`&& || !` only on bools, `%` only on ints. `path.size` on a vector or array is an int. There's
no ternary and no function calls in v1.

### API

```cpp
namespace imforge::expr {
enum class Type { Bool, Int, Float, String, Enum, Error };
struct Ast; // opaque tree, value type with std::vector children
struct TypeEnv { virtual bool TypeOfPath(const std::string& path, CType& out) = 0; virtual bool EnumValue(const std::string& enum_name, const std::string& member, long long& out) = 0; };
struct ValueEnv { virtual bool ReadPath(const std::string& path, Value& out) = 0; };
bool Parse(const std::string& src, Ast& out, Error* err);
bool TypeCheck(const Ast&, TypeEnv&, Type& result, Error* err);
bool Eval(const Ast&, ValueEnv&, Value& out, Error* err);           // untyped eval, coerces per the rules above
std::string EmitCpp(const Ast&, const std::function<std::string(const std::string& path)>& path_to_cpp);  // builder/codegen only
std::vector<std::string> CollectPaths(const Ast&);
}
```

### C++ emission

`player.health > 0 && cfg.esp.enabled` becomes `(player.health > 0 && cfg.esp.enabled)`. Paths
go through the callback: panel params stay as written (`player.health`), state becomes
`state.selectedTab`, `.size` becomes `.size()` on vectors and `IM_ARRAYSIZE(...)` on arrays.
`Mode::Aim` stays `Mode::Aim`. String literals become C string literals; string `==` becomes
`strcmp(...) == 0` for `char[]` and plain `==` for `std::string`.

### String interpolation

`"HP: {player.health:%.0f} / {player.max_hp}"`. A placeholder is `{path}`, `{path:fmt}` or
`{icon:fa-name}`, and `{{` / `}}` are literal braces. Default formats: int `%d`, float `%.2f`,
string `%s`, bool `true`/`false`. Codegen turns the example into
`ImGui::Text("HP: %.0f / %d", player.health, player.max_hp)`, adding `(double)`/`(int)` casts
where needed and `.c_str()` for `std::string`. `{icon:fa-play}` becomes the UTF-8 string from the
icon font table (the `ICON_FA_PLAY` macro when the `iconFonts` feature is on).

The parser is `bool ParseInterpolation(const std::string& s, std::vector<InterpSegment>& out, Error*)`
with `InterpSegment { bool is_expr; std::string text_or_expr, fmt; bool is_icon; }`, also in
`imforge_expr.h`.

## Interpreter

`imforge_interp.h` draws a `Panel` with ImGui at run time. The builder canvas and the live
client both use it.

```cpp
namespace imforge {
struct BindingRoot { std::string name; void* ptr; const StructInfo* type; };
struct PanelInstance {     // per (panel, instance) runtime state
    std::vector<std::pair<std::string, Value>> state;     // panel state vars
    std::vector<std::pair<std::string, Value>> mock;      // design-mode storage for unbound/mock paths
    std::vector<std::pair<std::string, std::string>> pending_tab;  // setTab actions
    std::vector<std::string> pending_popups; bool first_frame = true;
};
struct DrawCtx;   // passed to widget draw functions
using WidgetDrawFn = bool (*)(DrawCtx& ctx);               // returns "changed/clicked"
using BlockFn = void (*)(DrawCtx& ctx);                    // locked Code blocks
using HandlerFn = void (*)(DrawCtx& ctx);                  // generated event handlers in dev builds
struct Hooks {                                             // builder design mode
    std::function<void(const Node&, const ImVec2& min, const ImVec2& max, bool hovered)> on_item;
    std::function<void(const Node&)> on_click;             // selection
    std::function<ImTextureID(const std::string& name)> texture;  // Image widgets
    std::function<ImFont*(const std::string& name)> font;
};
struct InterpOptions { bool design_mode = false; float dpi_scale = 1.0f; bool run_handlers = true; bool animations = true; bool fx = true; const std::string* selected_id = nullptr; const char* picker_mode = nullptr; };
struct Interpreter {
    const Project* project = nullptr;                      // themes, classes, components, fonts
    WidgetCatalog catalog;                                 // custom manifests
    std::vector<std::pair<std::string, WidgetDrawFn>> widgets;   // custom draw fns (by type)
    std::vector<std::pair<std::string, BlockFn>> blocks;         // "Panel.nodeId"
    std::vector<std::pair<std::string, HandlerFn>> handlers;     // "Panel.nodeId.onClick"
    Hooks hooks; std::vector<Diagnostic> last_diagnostics;
    void DrawPanel(const Panel&, PanelInstance&, const std::vector<BindingRoot>& roots, const InterpOptions&);
    void DrawNode(const Node&, DrawCtx& parent_ctx);       // recursive; used by widgets with children
    void ApplyTheme(const Theme&);                          // writes ImGui::GetStyle()
};
struct DrawCtx {
    Interpreter& interp; const Panel& panel; PanelInstance& inst; const std::vector<BindingRoot>& roots; const InterpOptions& opts;
    const Node& node; const Component* component = nullptr; const Node* instance_node = nullptr;   // inside a component expansion
    float S() const;                                        // dpi scale
    // typed prop access, resolving expressions, params and catalog defaults
    bool GetBool(const char* prop, bool def=false) const; int GetInt(const char*, int def=0) const; float GetFloat(const char*, float def=0) const;
    std::string GetString(const char* prop, const char* def="") const;   // interpolation applied
    ImVec4 GetColor(const char*, ImVec4 def) const; ImVec2 GetVec2(const char*, ImVec2 def) const; ImVec2 GetSize(const char* prop="size") const;
    int GetFlags(const char* prop, const char* enum_name) const; int GetEnum(const char* prop, const char* values, int def=0) const;
    std::vector<std::string> GetStringList(const char*) const;
    // bindings: pointer + type; in design mode unbound slots get mock storage of the slot's first accepted type
    bool Binding(const char* slot, void** ptr, CType* type) const;
    bool* BindBool(const char* slot) const; int* BindInt(const char* slot) const; float* BindFloat(const char* slot, int count=1) const; std::string* BindStdString(const char* slot) const; char* BindCharArray(const char* slot, int* cap) const;
    bool Eval(const std::string& expr, Value& out) const;
    void FireEvent(const char* event);                      // runs actions + handler
    void DrawChildren();                                    // containers call this
    void DrawChildrenInCells();                             // Grid
};
}
```

`DrawPanel` draws the root Window as a real ImGui window (Begin/End), except in design mode,
where the caller has already opened a child region and the root is drawn inline. The engine
handles `PushID`, `visibleIf`/`enabledIf` (`BeginDisabled` where available, otherwise alpha
plus the disabled item flag), style overrides and classes, fonts, tooltips, sizing, stack and
canvas layout, component expansion and slots, Code blocks, events and actions, animations and
fx through `imb_runtime.h` (blur via `imb::fx` when `IMB_FX_AVAILABLE` is defined), and the
design-mode hooks. `imforge_interp_widgets.h` has `bool DrawStockWidget(DrawCtx&)`, which
dispatches on `ctx.node.type` for every stock widget, version guards included. Node trees
deeper than 64 levels are cut off with an error diagnostic.

In design mode the builder opens `ImGui::BeginChild("canvas")` at the panel's
`initialSize * S`, sets `design_mode=true`, and the engine draws the Window's children inline
(with a fake title bar if the Window has one). After every item it calls
`hooks.on_item(node, GetItemRectMin(), GetItemRectMax(), IsItemHovered())`. Clicking an item in
design or picker mode calls `hooks.on_click(node)` and does not fire the widget's own event.
Interactive widgets still work on mock storage, so sliders move.

## Code generation

`src/codegen/` turns the model into C++ files. [CODEGEN.md](CODEGEN.md) is the user-facing
description.

### API

```cpp
namespace imforge::codegen {
struct Options { CodegenSettings settings; int imgui_version = 19190; bool features_animations=true, features_fx=true, features_icons=true; };
struct FileOut { std::string path; std::string content; };
struct LiteralRef { std::string node_id, prop; int token_index; };          // for round-trip
struct Result { std::vector<FileOut> files; std::vector<Diagnostic> diagnostics; std::vector<LiteralRef> literals; };
Result GeneratePanel(const Project&, const Panel&, const Options&);           // <Name>.h/.cpp (or single .h if !split_header)
Result GenerateComponents(const Project&, const Options&);                    // imforge_components.h/.cpp
Result GenerateTheme(const Project&, const Theme&, const Options&);           // ApplyTheme_<Name>
Result GenerateFonts(const Project&, const Options&);                         // imforge_fonts.h: LoadFonts(ImGuiIO&), ImFont* by name
Result GenerateReflection(const Project&, const Options&);                    // imforge_reflect.h: IMFORGE_REFLECT for binding.types (from types.json)
Result GenerateDevGlue(const Project&, const Options&);                       // imforge_dev.h: RegisterBlock/RegisterHandler for all panels (dev builds)
Result GenerateAll(const Project&, const Options&);                           // everything, deterministic order
std::string EmitExpression(const Panel&, const std::string& expr, const Options&);   // expr::EmitCpp with the panel path mapping
std::string EmitSize(const SizeSpec& w, const SizeSpec& h, const Options&);   // "ImVec2(...)", see Sizing
}
```

### Output

With `splitHeader: true`, namespace `ui` and markers on, a panel produces a header and a
source file. Each file starts with a one-line "Generated by ImForge" banner (left out below).

`MainMenu.h`:

```cpp
// @imb:file panel=MainMenu schema=1
#pragma once
#include "imgui.h"
struct Player; struct Settings;
namespace ui {
struct MainMenuState { int selected_tab = 0; bool open = true; };
void DrawMainMenu(MainMenuState& state, Player& player, Settings& cfg);
// @imb:user id=header_extra
// @imb:end id=header_extra
}
```

`MainMenu.cpp`:

```cpp
// @imb:file panel=MainMenu schema=1
#include "MainMenu.h"
#include "Player.h"      // from binding.types include hints when known, else add it in @imb:user id=includes
#include <cfloat>
#include <cstring>
#include "imb_runtime.h"   // only when animations/fx are used
// @imb:user id=includes
// @imb:end id=includes
namespace ui {
// @imb:user id=file_top
// @imb:end id=file_top

static void MainMenu_btn_play_OnClick(MainMenuState& state, Player& player, Settings& cfg) {
    // @imb:user id=btn_play.onClick
    // @imb:end id=btn_play.onClick
}

// @imb:locked id=custom_1 h=1a2b3c4d
static void MainMenu_custom_1_Block(MainMenuState& state, Player& player, Settings& cfg) {
    ImGui::Text("hand written");
}
// @imb:end id=custom_1

void DrawMainMenu(MainMenuState& state, Player& player, Settings& cfg) {
    const float imb_s = ImGui::GetFontSize() / 13.0f; (void)imb_s;
    // @imb:user id=before_draw
    // @imb:end id=before_draw
    // @imb:begin id=window h=9f8e7d6c
    ImGui::SetNextWindowSize(ImVec2(520 * imb_s, 380 * imb_s), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Main Menu", &state.open, ImGuiWindowFlags_NoCollapse)) {
        // @imb:begin id=btn_play h=11223344
        if (cfg.esp.enabled) {
            if (ImGui::Button("Play", ImVec2(-FLT_MIN, 0))) {
                cfg.esp.enabled = !cfg.esp.enabled;
                MainMenu_btn_play_OnClick(state, player, cfg);
            }
        }
        // @imb:end id=btn_play
        // @imb:begin id=custom_1 h=1a2b3c4d
        MainMenu_custom_1_Block(state, player, cfg);
        // @imb:end id=custom_1
    }
    ImGui::End();
    // @imb:end id=window
    // @imb:user id=after_draw
    // @imb:end id=after_draw
}
}
```

### Rules

Every node is wrapped in `// @imb:begin id=<id> h=<hash>` and `// @imb:end id=<id>`. The hash
is FNV-1a 32 (hex) of the block body with leading whitespace stripped from each line. Child
blocks are nested inside the parent's body, but the parent hash only covers the parent's own
lines: each child is replaced by the token `@child:<id>` before hashing.

`Code` nodes and nodes with `locked: true` are emitted once as an `@imb:locked` function and
called from the tree. The round-trip keeps that body; its initial content is `Code.code` or the
normally generated body.

Handlers are `static void <Panel>_<id>_<Event>(State&, params...)` and run after the node's
actions. `visibleIf` wraps the node in `if (...) { }`. `enabledIf` becomes
`ImGui::BeginDisabled(!(expr)); ... EndDisabled();` on 1.86+, and
`PushItemFlag(ImGuiItemFlags_Disabled, true)` plus an alpha style var before that. Style
overrides become `PushStyleColor`/`PushStyleVar` before the item with matching pops after it;
classes expand to `ui::Style_<class>_Push()/Pop()` helpers in `imforge_styles.h`.

Animations call into `imb_runtime.h`. A hover transition is
`imb::Transition(ImGui::GetID("btn_play"), hovered, 0.15f, imb::Ease::OutCubic)` feeding a lerped
color push; an enter preset is `if (imb::BeginEnter(id, visible, dur, preset)) { ... imb::EndEnter(); }`.
Effects like `imb::Shadow(ImGui::GetWindowDrawList(), min, max, ...)` are drawn after the item;
blur is `imb::fx::Blur(...)` inside `#ifdef IMB_FX_AVAILABLE`.

Version guards (`#if IMGUI_VERSION_NUM >= N`) are only emitted where the API differs:
`SeparatorText`, `BeginChild` flags, `BeginDisabled`, `TableFlags` names, `ImGuiKey`. The naming
case settings apply to generated function and variable names, never to user identifiers. With
`splitHeader: false` each panel is one header with `inline` functions. Files come out in
alphabetical order and carry no timestamps.

## Round-trip

`src/roundtrip/` reads exported files back and merges them with fresh output.

```cpp
namespace imforge::roundtrip {
struct Block { enum class Kind { Begin, User, Locked, File } kind; std::string id; std::string hash; std::string body; int line_begin, line_end; };
struct ParsedFile { std::string panel; std::vector<Block> blocks; std::string full_text; };
bool ParseGenerated(const std::string& text, ParsedFile& out, Error*);
std::string Fnv1a32Hex(const std::string& normalized_body);
struct Conflict { std::string node_id; std::string reason; std::string user_code; std::string generated_code; };
struct MergeReport { std::vector<std::string> updated_nodes; std::vector<Conflict> conflicts; std::vector<Diagnostic> diagnostics; };
MergeReport Merge(Panel& panel, const std::string& existing_source, const codegen::Result& fresh, std::string& merged_source_out);
std::string ApplyResolution(const std::string& merged_source, const Conflict&, bool keep_user_code /* true: lock node */);
}
```

`Merge` first carries every user slot and locked body from the existing file into the freshly
generated text. Then it looks at blocks whose hash changed and tries to pull literal edits back
into `panel`. Both versions of the block are tokenized (identifiers, numbers, string literals,
punctuation). If the token sequences match everywhere except at indices listed in the codegen
`LiteralRef`s, those literals (strings, numbers, flag identifiers joined by `|`) are written back
into the node's props. Anything else becomes a `Conflict`.

## Live attach

Live attach is spread over `imforge_transport*.h`, `imforge_protocol.h`, `imforge_pairing*.h`
and `imforge_client.h`. [LIVE_ATTACH.md](LIVE_ATTACH.md) covers usage and
[SECURITY_MODEL.md](SECURITY_MODEL.md) explains the reasoning behind pairing.

### Transports

```cpp
namespace imforge {
struct Transport {                        // non-blocking, used by both builder (server) and app (client)
    virtual ~Transport() {}
    virtual bool Listen(const char* endpoint) = 0;      // builder side
    virtual bool Connect(const char* endpoint) = 0;     // app side
    virtual bool IsConnected() const = 0;
    virtual bool Send(const void* data, size_t len) = 0;        // one framed message; queued, never blocks
    virtual bool Receive(std::vector<unsigned char>& out) = 0;  // pops one framed message if available
    virtual void Poll() = 0;                                    // accept/reconnect/flush; call every frame
    virtual void Close() = 0;
};
std::unique_ptr<Transport> CreateTransport(const char* url);   // "tcp://127.0.0.1:7777" | "pipe://ImForge" | "shm://ImForge"
}
```

On tcp and pipe a frame is a little-endian `uint32` length followed by the payload; frames over
32 MB mark the stream as corrupt. TCP listens on loopback only unless remote listening is
switched on explicitly. `pipe://<name>` is the Windows named pipe `\\.\pipe\<name>` (message
mode, current user only) or, on Linux, a unix domain socket at
`$XDG_RUNTIME_DIR/imforge-<name>.sock` (falling back to `/tmp/imforge-<name>.sock`).
`shm://<name>` is a shared memory object `ImForge.<name>` (`CreateFileMapping` on Windows,
`shm_open` on Linux) laid out as `{ u32 magic='IMFG', u32 version=1, Ring to_app, Ring to_builder }`
with `Ring { u32 capacity=1<<20, volatile u32 head, volatile u32 tail, u8 data[capacity] }`. The
writer appends `[len][payload]`; each ring is single producer, single consumer, with memory
fences, polled every frame.

TCP and pipe use one background thread per transport for blocking I/O, with mutex-protected
queues. On the app side `Poll()` reconnects with a 1 s backoff.

### Messages

Payloads are JSON objects with a `"t"` field naming the message. `imforge_protocol.h` has
`std::string Frame(const Json&)`, `bool Unframe(const std::vector<unsigned char>&, Json&)` and a
`Msg*` builder per message (`MsgPairingHello`, `MsgClientInfo`, ...). `kProtocolVersion` is 2.
Payloads are capped at 16 MB.

| t | direction | fields |
|---|---|---|
| `hello` | app to builder | `v: 2, pairing: true, nonce_a`. Sent before authentication, nothing else in it. |
| `pair_challenge` | builder to app | `nonce_b, session_id, mac_b` |
| `pair_response` | app to builder | `session_id, mac_a` |
| `pair_ok` | builder to app | `session_id` |
| `pair_fail` | builder to app | `attempts_left, reason`, then the builder closes the connection |
| `client_info` | app to builder | `app, pid, imgui_version (int), backend, platform, widgets: [names], textures: [names]`. First message after `pair_ok`. |
| `project` | builder to app | `project`: imforge.json content with themes, classes and fonts inlined |
| `panel` | builder to app | `panel`: panel JSON |
| `components` | builder to app | `components: [...]` |
| `widgets` | builder to app | `manifests: [...]` |
| `remove_panel` | builder to app | `name` |
| `theme` | builder to app | `theme` |
| `mode` | builder to app | `picker: bool, selected: "id", panel: "name"` |
| `ping` / `pong` | builder to app / back | |
| `blocks` | app to builder | `panel, blocks: [{id, h}]` |
| `select`, `hover` | app to builder | `panel, id` |
| `log` | app to builder | `level, msg` |
| `stats` | app to builder | `fps, panels: [name]` |

The old protocol v1 `hello` carried the `client_info` fields directly and had no pairing. The
builder only accepts it when its "Require pairing" preference is off.

### Pairing

Every link starts with a one-time-code handshake. Until it succeeds the builder only accepts
`hello` and `pair_response`, the app only accepts `pair_challenge`, `pair_ok` and `pair_fail`,
and the app sends nothing but the bare `hello` and `pair_response`. No app name, pid, widget
or texture list reaches a peer that hasn't proven the code.

The code is 32 Crockford base32 characters (160 bits, shown as groups of 4) from the OS CSPRNG
(`BCryptGenRandom`, `getrandom`). Input is normalized: uppercased, `-` and whitespace removed,
`O` read as `0`, `I` and `L` as `1`.

The builder writes `{"code","endpoint","issued","pid","v":1}` atomically to a user-only file
named after the endpoint (`tcp-<port>`, `pipe-<name>`, `shm-<name>`):
`%LOCALAPPDATA%\ImForge\live\<key>.pair` on Windows, `$XDG_RUNTIME_DIR/imforge/<key>.pair` or
`~/.cache/imforge/<key>.pair` on POSIX. The app derives the same path from its `Connect()` URL
and reads it before every (re)connect. Explicit fallbacks, in this order:
`imforge::live::SetPairingCode()`, the `IMFORGE_PAIRING_CODE` environment variable, then the
file.

The handshake, with K = SHA256("imforge-pair-v1|" + normalized code) and MACs computed as
lowercase-hex HMAC-SHA256 over `label|nonce_a|nonce_b|session_id` (each a 32-char lowercase
hex field):

1. app to builder: `hello {v:2, pairing:true, nonce_a}`. A pairing hello with another `v` gets
   `pair_fail`.
2. builder to app: `pair_challenge {nonce_b, session_id, mac_b}` with label `imforge-builder`.
   The app checks `mac_b` in constant time and disconnects on a mismatch.
3. app to builder: `pair_response {session_id, mac_a}` with label `imforge-app`.
4. builder to app: `pair_ok {session_id}`, or `pair_fail {attempts_left, reason}` and the
   connection is closed.

The builder makes a fresh code when it starts listening and rotates it after a successful
pairing, after 10 minutes, after 5 failed attempts (then refuses connections for 3 s) or on
request. Only a wrong or malformed `pair_response` counts as a failed attempt. One connection
at a time; no `hello` within 5 s, or no `pair_response` within 5 s of the challenge, drops it.
The app never resends a code that paired, and retries an unproven one every 2 s, re-reading the
file. A `hello` without `pairing` is refused unless the builder's "Require pairing" preference
is off (legacy mode: unauthenticated, plain frames). The reasoning behind these rules is in
[SECURITY_MODEL.md](SECURITY_MODEL.md#pairing).

### Session channel

After `pair_ok` every message in both directions is sealed. Direction keys come from
HKDF-SHA256 (RFC 5869):

```
PRK     = HMAC(salt = nonce_a || nonce_b || session_id  (48 raw bytes), IKM = K)
key_b2a = Expand(PRK, "imforge-v1 b2a", 32)
key_a2b = Expand(PRK, "imforge-v1 a2b", 32)
```

The payload handed to `Transport::Send` is `MAC (32) || seq (uint64 big-endian, 8) || compact JSON`
with `MAC = HMAC-SHA256(dir_key, seq || JSON)` over the exact bytes, no canonicalization. The
transport length prefix is unchanged. `seq` starts at 1 per direction and the receiver requires
exactly the next value. MAC (constant time) and `seq` are checked before any JSON is parsed.

On any failure the receiver drops the connection, wipes the keys and logs a security event
(builder: log source `security`; app: log callback level 3 and a `Status()` starting with
"security:"), and the app pairs again with the next code. The first sealed app message is
`client_info`. Nothing is encrypted.

### Client API

```cpp
#define IMFORGE_LIVE            // without it every function compiles to an empty inline no-op
#include "imforge_client.h"
namespace imforge::live {
bool Connect(const char* url);            // app side; non-blocking, auto-reconnects
void Disconnect(); bool IsConnected(); const char* Status();
void NewFrame();                          // call after ImGui::NewFrame(): polls transport, applies messages
template<class T> BindingRoot Bind(const char* name, T& obj);   // requires IMFORGE_REFLECT(T,...)
void Draw(const char* panel, std::initializer_list<BindingRoot> roots);    // draws the latest panel definition
void RegisterBlock(const char* panel_dot_id, BlockFn); void RegisterHandler(const char* panel_dot_id_dot_event, HandlerFn);
void RegisterWidget(const char* type, WidgetDrawFn); void RegisterTexture(const char* name, ImTextureID);
void SetLogCallback(void (*fn)(int level, const char* msg));
bool IsPaired();                          // handshake completed on the current link
bool SetPairingCode(const char* code);    // fallback when the app can't read the builder's pairing file
}
```

`tools/amalgamate.py` builds `dist/imforge_client_single.h` (json, value, core, widgets, expr,
interp, transports, protocol, pairing, client and `imb_runtime`) for drop-in use.

## Runtime

`imb_runtime.h` (namespace `imb`) is the only ImForge header generated code may need, and only
when a panel uses motion or effects. Details and examples are in [ANIMATIONS.md](ANIMATIONS.md).

```cpp
namespace imb {
enum class Ease { Linear, InQuad, OutQuad, InOutQuad, InCubic, OutCubic, InOutCubic, OutBack, InBack, OutElastic, OutBounce, OutExpo };
float EaseApply(Ease, float t);
// per-id animation state keyed by ImGuiID in a function-local static table; dropped after 2 s unused
float Transition(ImGuiID id, bool on, float duration, Ease ease = Ease::OutCubic);          // 0..1, animates toward on ? 1 : 0
float AnimateTo(ImGuiID id, float target, float duration, Ease ease = Ease::OutCubic);     // continuous value
float Spring(ImGuiID id, float target, float stiffness = 170.f, float damping = 26.f);
ImVec4 Lerp(const ImVec4& a, const ImVec4& b, float t); ImU32 LerpCol(ImU32 a, ImU32 b, float t); ImVec2 Lerp(const ImVec2&, const ImVec2&, float);
enum class Preset { None, Fade, SlideUp, SlideDown, SlideLeft, SlideRight, FadeSlideUp, FadeSlideDown, ScalePop, Stagger };
bool BeginEnter(ImGuiID id, bool visible, float duration, Preset preset, float stagger_delay = 0.f);  // false when fully hidden; pushes Alpha + cursor offset
void EndEnter();
void BeginTabCrossfade(ImGuiID id, int current_tab, float duration); void EndTabCrossfade();
// DrawList effects, backend-agnostic
void GradientRect(ImDrawList*, const ImVec2& min, const ImVec2& max, ImU32 c0, ImU32 c1, bool vertical, float rounding = 0);
void RadialGradient(ImDrawList*, const ImVec2& center, float radius, ImU32 inner, ImU32 outer, int segments = 48);
void Shadow(ImDrawList*, const ImVec2& min, const ImVec2& max, float rounding, ImU32 color, float size, const ImVec2& offset = ImVec2(0,2));
void Glow(ImDrawList*, const ImVec2& min, const ImVec2& max, float rounding, ImU32 color, float size);
enum class BorderStyle { Solid, Rainbow, Sweep, Pulse };
void AnimatedBorder(ImDrawList*, const ImVec2& min, const ImVec2& max, float rounding, float thickness, BorderStyle, ImU32 color, float speed = 1.f);
struct ParticleStyle { int count = 40; float speed = 20.f; float size = 2.f; ImU32 color = 0x40FFFFFF; bool connect = false; float connect_dist = 80.f; };
void Particles(ImDrawList*, ImGuiID id, const ImVec2& min, const ImVec2& max, const ParticleStyle&);
void NoiseBackground(ImDrawList*, const ImVec2& min, const ImVec2& max, ImU32 color, float density, ImGuiID seed);
void CursorHighlight(ImDrawList*, const ImVec2& min, const ImVec2& max, float rounding, ImU32 color, float radius);
float DeltaTime(); void NewFrame();   // optional; ImGui::GetIO().DeltaTime is used when not called
}
```

### Shader fx

```cpp
namespace imb::fx {
// each backend header defines: bool Init(const <Backend>InitDesc&); void Shutdown();
void PrepareFrame(const FrameDesc& fd);   // FrameDesc per backend (back buffer / cmd list / render target info)
void Blur(ImDrawList* dl, const ImVec2& min, const ImVec2& max, float radius, float rounding = 0, ImU32 tint = 0xFFFFFFFF);
bool IsAvailable();
}
#define IMB_FX_AVAILABLE 1   // defined by any backend header
```

Call `PrepareFrame()` after the scene is rendered and before `ImGui_ImplXXX_RenderDrawData`. It
copies the back buffer into a private texture and builds a blurred mip chain; `Blur()` then only
samples that texture from a draw callback. Each backend header is self-contained: shaders are
compiled at run time with D3DCompile on DX9/11/12, GLSL on GL3, and embedded SPIR-V on Vulkan.
Rounded corners come from scissor plus a rounded mask in the pixel shader. Nothing in `tools/` is
needed at run time.

## Binding tool

`src/binding/` builds the `imforge_binding` library and the `imforge-reflect` CLI:

```
imforge-reflect --compile-commands <path> | --include <dir>... --types Player,Settings [--header <h>...]
                --out-json types.json --out-header imforge_reflect.h
```

It uses libclang when `IMFORGE_WITH_LIBCLANG` finds it (`LLVM_DIR`, `libclang` on PATH,
`C:\Program Files\LLVM`, `/usr/lib/llvm-*`). Plugin and response-file flags in
`compile_commands.json` are dropped before anything reaches libclang. Output `types.json`:

```json
{ "schemaVersion": 1, "kind": "types",
  "structs": { "Player": { "header": "game/player.h", "fields": [
      { "name": "health", "type": "float", "ctype": "Float" },
      { "name": "pos", "type": "ImVec2", "ctype": "Vec2" },
      { "name": "inv", "type": "std::vector<Item>", "ctype": "Vector", "elem": "Item" } ] } },
  "enums": { "Mode": { "values": [["Off", 0], ["Aim", 1]] } } }
```

The builder side is
`struct TypeDatabase { bool Load(const Json&); bool TypeOfPath(const std::vector<Param>&, const std::string& path, CType&); std::vector<std::string> Complete(const std::vector<Param>&, const std::string& prefix); }`,
which implements `expr::TypeEnv`. Without libclang the builder works from a hand-written
`types.json` or from `IMFORGE_REFLECT` macros; the dev glue can send the registry to the builder
over the live link (message `types`). See [BINDING.md](BINDING.md).

## Builder

The editor runs on SDL3 + OpenGL 3 by default (Windows and Linux), or Win32 + DX11 with
`IMFORGE_BUILDER_BACKEND=WIN32_DX11`, on a pinned ImGui docking tag (see
`cmake/Dependencies.cmake`).

`EditorContext` (`editor_context.h`) owns all editor state, and each editor panel is a class
with `void Draw(EditorContext&)` in `src/builder/panels/` (the list is in
[ARCHITECTURE.md](ARCHITECTURE.md#inside-the-builder)). Undo commands store JSON snapshots of the
affected panel or component, `UndoStack::Push(label, before, after)`; pushes with the same
label within 300 ms are merged, so a drag is one step.

`project_io.h` has `LoadProject(dir, Project&, Error*)`, `SaveProject(const Project&, Error*)`
(writes every file deterministically) and `ExportCode(Project&, Error*)` (codegen plus
round-trip merge into `codegen.outputDir`). `.imblib` import and export live in `imblib.h`. All
file writes stay inside the project directory; see [SECURITY_MODEL.md](SECURITY_MODEL.md).

Fonts are fetched at configure time: Inter for the UI, JetBrains Mono for the code view and
Font Awesome 6 Free Solid for icons (with `IconsFontAwesome6.h`). Shortcuts: Ctrl+N/O/S,
Ctrl+Z/Y, Del, Ctrl+D duplicate, Ctrl+E export, F5 push to the live app, Ctrl+Shift+P picker
mode, arrows to nudge on the canvas. The first launch opens the wizard, which walks through the
project file fields with the templates from `templates/`.

## Build

CMake 3.21+. Options:

| option | default | |
|---|---|---|
| `IMFORGE_BUILD_BUILDER` | ON | the editor |
| `IMFORGE_BUILD_TESTS` | ON | doctest suites |
| `IMFORGE_BUILD_EXAMPLES` | OFF | example apps (ON in CI) |
| `IMFORGE_WITH_LIBCLANG` | AUTO | AUTO, ON or OFF |
| `IMFORGE_IMGUI_TAG` | `v1.91.9b-docking` | Dear ImGui tag to fetch |
| `IMFORGE_BUILDER_BACKEND` | `SDL3_GL3` | or `WIN32_DX11` |
| `IMFORGE_WITH_FX` | ON | shader fx backends |
| `IMFORGE_PACKAGING` | ON | install rules, amalgamation target, CPack |

Targets: `imforge::headers` (INTERFACE, `include/`), `imgui` (static, core plus the backends in
use), `imforge_codegen`, `imforge_roundtrip`, `imforge_binding`, `imforge-reflect`,
`imforge_builder`, one test executable per module via `imforge_add_test`, and the examples.
Dear ImGui, SDL3, doctest, miniz and the fonts come from FetchContent.
`compile_commands.json` is exported.

CI (`.github/workflows/ci.yml`) runs on ubuntu-latest and windows-latest against
`v1.85`, `v1.87`, `v1.89.9`, `v1.90.9`, `v1.91.9b-docking`, `v1.92.1`, `v1.93.0`,
`v1.93.0-docking` and `master`, building the headers and tests everywhere and the builder only
on the docking tag. [VERSION_MATRIX.md](VERSION_MATRIX.md) has the results.

## Versions and licensing

The project version (`0.1.0`) lives in `CMakeLists.txt` and `include/imforge/imforge_version.h`
(`IMFORGE_VERSION "0.1.0"`, `IMFORGE_VERSION_NUM 100`). ImGui versions are strings in JSON
(`"1.91.9"`) and `IMGUI_VERSION_NUM` ints in code (`19190`).

ImForge is MIT licensed. The bundled fonts are OFL (their license files ship with them),
IconFontCppHeaders is zlib and miniz is MIT.
