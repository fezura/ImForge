# Widget SDK

Anything you can draw with ImGui or `ImDrawList` can become an ImForge widget. It shows up in
the palette, gets typed properties in the properties panel, binds to data, fires events,
previews on the canvas, and the code generator emits it as a call to your function. You
describe it in a `.imw.json` manifest; the builder itself doesn't change.

## Manifest

```json
{
  "schemaVersion": 1, "kind": "widget", "name": "Toggle", "category": "Essentials",
  "include": "imb_essentials.h", "minImGuiVersion": 18500,
  "props": [
    { "name": "label",   "type": "string", "default": "Toggle" },
    { "name": "size",    "type": "size",   "default": { "w": "auto", "h": "auto" } },
    { "name": "onColor", "type": "color",  "default": "#2ecc71ff" }
  ],
  "bindings": [ { "name": "value", "types": ["bool"], "required": true } ],
  "events": [ "onChange" ],
  "container": false,
  "call": "imb::ess::Toggle({label}, {value}, {size}, {onColor})",
  "returns": "changed",
  "preview": [
    { "shape": "rect",   "rounding": 10, "fill": "{onColor}", "w": 40, "h": 20 },
    { "shape": "circle", "fill": "#ffffffff", "r": 8, "x": 30, "y": 10 }
  ]
}
```

| Field | Meaning |
|---|---|
| `name` | widget type, unique across stock widgets and loaded packs |
| `category` | palette group |
| `include` | header the generated code has to include |
| `minImGuiVersion` | required `IMGUI_VERSION_NUM`; below it the builder greys the widget out |
| `props` | typed properties: `bool`, `int`, `float`, `string`, `color`, `vec2`, `vec4`, `size`, `enum` (with `values`), `flags` (with `enum`), `stringList`, `expr`, `code`; optional `min`, `max`, `hint` |
| `bindings` | named slots with the types they accept (`bool`, `int`, `float`, `float[N]`, `vec2`, `vec4`, `string`, `char[N]`, `enum`, `texture`, `float[]`, `vector<float>`) |
| `events` | events the widget can fire (`onClick`, `onChange`, `onOpen`, `onClose`, or your own) |
| `container` | whether it takes children |
| `call` | C++ call template for the code generator |
| `callEnd` | containers only: statement emitted after the children (like `ImGui::EndChild()`) |
| `returns` | what the return value means: `changed`, `clicked`, `open` or `void`; decides which events fire |
| `preview` | DrawList shapes for the canvas when no app is attached |

### Call templates

Each `{name}` in `call` is replaced by the C++ expression for that prop or binding, and an
optional `:modifier` picks a different form:

| Placeholder | Becomes |
|---|---|
| `string` prop | a string literal, or a formatted local buffer when it uses interpolation; `{label:fmt}` gives the format string and `{label:args}` the `, arg, ...` list, for printf-style APIs |
| `bool`, `int`, `float`, `expr` prop | a literal or expression |
| `color` prop | `ImVec4(r, g, b, a)`; `{onColor:u32}` gives `IM_COL32(...)` |
| `size`, `vec2` prop | `ImVec2(...)`, DPI-scaled |
| `vec4` prop | `ImVec4(...)` |
| `enum` prop | the value name, prefixed with the manifest's `enumPrefix` if there is one |
| `flags` prop | `A | B | C` |
| `stringList` prop | a zero-separated string literal; `:array` and `:count` give a `const char*[]` and its size |
| binding | a pointer expression (`&player.health`), arrays decay; `:value` for the value itself, `:count` for the element count |
| `{children}` | where child code goes, in containers |
| `{id}` | the node id as a string literal |

The interpreter reads the same manifest to resolve values at run time and hands them to your
registered draw function.

## Implementation

Your header provides the real function that generated code calls, plus a thin adapter for the
interpreter in development builds:

```cpp
namespace imb::ess {
inline bool Toggle(const char* label, bool* v, ImVec2 size = ImVec2(0, 0), ImVec4 on = ImVec4(0.18f, 0.8f, 0.44f, 1));
}

// adapter for the live interpreter (dev builds only)
inline bool DrawToggleInterp(imforge::DrawCtx& ctx) {
    return imb::ess::Toggle(ctx.GetString("label").c_str(), ctx.BindBool("value"),
                            ctx.GetSize(), ctx.GetColor("onColor", ImVec4(0.18f, 0.8f, 0.44f, 1)));
}
```

Register the adapter once in your app with
`imforge::live::RegisterWidget("Toggle", &DrawToggleInterp)`. Until you do, the attached app
draws a placeholder for that widget.

## Preview shapes

`rect` (x, y, w, h, rounding, fill, stroke, thickness), `circle` (x, y, r, fill, stroke),
`line` (x1, y1, x2, y2, stroke, thickness) and `text` (x, y, text, color). Coordinates are
design pixels relative to the widget's top-left corner, and `{prop}` references pull in the
current values. Without a preview the canvas draws a labelled placeholder box of the widget's
size. When an app is attached the canvas shows the app's real rendering instead.

## Packaging

```sh
python tools/pack_imblib.py packs/essentials dist/essentials.imblib
```

`pack_imblib.py` reads the pack's `manifest.json`, checks it and every widget manifest it lists
(required fields, `returns`, includes that are actually in the pack), and writes a reproducible
zip. `python tools/validate_json.py` checks the files against `schemas/`. Import an `.imblib` in
the builder with *File > Import .imblib...*; it's copied into the project's `libs/` folder so the
project stays self-contained. Component libraries use the same container, see
[COMPONENTS.md](COMPONENTS.md).

## Things a good widget does

- works from ImGui 1.85 up, or declares the right `minImGuiVersion`
- keeps internal state under `ImGui::GetID`/`PushID`, never in statics keyed by label
- scales with `ImGui::GetFontSize()` so DPI scaling works
- compiles with exceptions and RTTI off
- ships a `preview`, so it looks right on the canvas without an attached app

The essentials pack in `packs/essentials` is the reference.
