# Components

A component is a widget tree you define once, with typed parameters and optional slots, and
use as many times as you like. Instances follow the definition. In generated code a component
is a function and an instance is a call.

## Defining one

`components/LabeledSlider.imc.json`:

```json
{
  "schemaVersion": 1, "kind": "component", "name": "LabeledSlider",
  "params": [
    { "name": "label", "type": "string",         "default": "Value" },
    { "name": "value", "type": "binding<float>" },
    { "name": "min",   "type": "float",          "default": 0 },
    { "name": "max",   "type": "float",          "default": 1 }
  ],
  "slots": [ "content" ],
  "root": { "id": "row", "type": "HStack", "children": [
    { "id": "lbl", "type": "Text",        "props": { "text": { "param": "label" } } },
    { "id": "sl",  "type": "SliderFloat", "props": { "label": "##v", "min": { "param": "min" }, "max": { "param": "max" } },
                                          "bind":  { "value": { "param": "value" } } },
    { "id": "slot_content", "type": "Slot", "props": { "name": "content" } }
  ] }
}
```

Inside the definition any property or binding can be `{"param": "name"}`. Parameter types are
`string`, `int`, `float`, `bool`, `color`, `vec2`, `vec4`, `enum:<A,B,C>` and `binding<T>` with
`T` one of `bool|int|float|string|vec2|vec4|color|any`.

In the builder, use **New...** in the Components panel, or select some widgets and click
**From selection...** to move them into a new component and leave an instance in their place.
The component editor is the normal canvas, with parameters shown as mock values.

## Using one

```json
{ "id": "hp", "type": "Component", "component": "LabeledSlider",
  "props": { "label": "Health", "value": "player.health", "min": 0, "max": 100 },
  "slots": { "content": [ { "id": "hint", "type": "Text", "props": { "text": "0-100" } } ] } }
```

Slot children are regular nodes and you edit them in place on the canvas.

## Generated code

```cpp
// imforge_components.h
void LabeledSlider(const char* label, float* value, float min, float max,
                   const std::function<void()>& content = {});   // slots become callables (C++14/17/20)
```
```cpp
// in DrawMainMenu
ImGui::PushID("hp");
ui::LabeledSlider("Health", &player.health, 0.f, 100.f, [&] { ImGui::Text("0-100"); });
ImGui::PopID();
```

Every instance is wrapped in `PushID(instance id)`, so two instances with the same labels never
collide. Components with animations derive their `ImGuiID` seed from that id too.

## Overrides

For now you customise an instance through its parameters only. If you need to change something
the definition doesn't expose, **Unpack** the instance: it's replaced by a copy of the
definition's widgets and stops following the definition. Deeper per-instance overrides are
planned for v1.1.

## Sharing with .imblib

A library is a zip with a manifest:

```json
{ "schemaVersion": 1, "kind": "imblib", "name": "menu-kit", "version": "1.2.0",
  "minImGuiVersion": 18500,
  "components": [ "components/LabeledSlider.imc.json", "components/Card.imc.json" ],
  "widgets":    [ "widgets/Toggle.imw.json" ],
  "headers":    [ "imb_essentials.h" ],
  "dependencies": [ { "name": "essentials", "version": ">=1.0" } ] }
```

**Export...** in the Components panel picks components, plus the widget manifests and headers
they rely on. **Import...** (or *File > Import .imblib...*) copies the file into `libs/` and
registers what's in it; name clashes are reported before anything is added. Archives from other
people are checked before anything is extracted, see
[SECURITY_MODEL.md](SECURITY_MODEL.md#imblib-import). `tools/pack_imblib.py` builds the same
archive from the command line, for CI or for publishing a pack.
