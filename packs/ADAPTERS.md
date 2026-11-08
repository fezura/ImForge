# Pack adapters

A pack widget's manifest tells codegen how to call it, but the builder canvas and the live client
don't run generated code, they run the interpreter. An adapter is the bit of glue that lets the
interpreter draw the real widget: one `WidgetDrawFn` per manifest type that reads the node's props
and bindings from `DrawCtx` and calls the pack's C++ function.

Each pack ships one header, `packs/<pack>/imforge_adapters_<pack>.h`, next to its widget headers.
It's only compiled where `imforge_interp.h` exists (builder, live client, tests), never by
exported projects. `packs/essentials/imforge_adapters_essentials.h` is the reference, copy its
shape.

```cpp
#pragma once
#include "imforge/imforge_pack_adapter.h"
#include "imb_controls.h"

namespace imforge {
namespace adapters_controls {

inline bool Knob(DrawCtx& ctx) {
    std::string label = pack::Label(ctx);
    float* v = pack::BindFloat(ctx, "value");
    bool changed = imb::ctl::Knob(label.c_str(), v, pack::Float(ctx, "min"), pack::Float(ctx, "max"),
                                  pack::Px(ctx, "radius", 18), pack::Color(ctx, "color"));
    return pack::Fire(ctx, "onChange", changed);
}

inline bool Panel(DrawCtx& ctx) {               // container: begin
    imb::ctl::BeginPanel(pack::Label(ctx, "title").c_str(), pack::Size(ctx));
    return true;
}
inline void EndPanel(DrawCtx&) { imb::ctl::EndPanel(); }

}  // namespace adapters_controls

inline void RegisterPackWidgets_controls(Interpreter& in) {
    namespace a = adapters_controls;
    static const pack::Entry kEntries[] = {
        {"Knob", &a::Knob, nullptr},
        {"Panel", &a::Panel, &a::EndPanel},
    };
    pack::RegisterAll(in, kEntries);
}

}  // namespace imforge
```

The rules, roughly in the order you'll hit them:

The register function is `imforge::RegisterPackWidgets_<pack>(imforge::Interpreter&)`, inline, in
namespace `imforge`, with `<pack>` exactly the folder name. The builder and the client find it by
that name, so don't rename it. Put the draw functions in `imforge::adapters_<pack>` so packs
don't collide.

Register every type listed in the pack's `manifest.json`, with the type string equal to the
manifest `name`. Type names share one namespace across all packs and the stock widgets, so they
must be unique (the catalog refuses a stock name, and a second pack with the same name silently
replaces the first).

Containers (manifest `"container": true` with a `callEnd`) register a begin and an end function.
The interpreter calls begin, draws the children, then calls end, every frame, whatever begin
returned. That's the `BeginChild`/`EndChild` contract, so a widget that skips its children when
collapsed must still be fine with the end call. Containers that really are `if (Begin()) { ...
End(); }` style don't fit this; draw the children yourself in begin with `ctx.DrawChildren()` and
register no end function.

Read props through `DrawCtx`. Missing props fall back to the manifest `default` automatically, so
`pack::Color(ctx, "color")` with no default argument already gives you the manifest value.
`pack::Px` multiplies by the DPI scale; use it for radii, thicknesses and heights, not for counts.
`pack::Size` gives ImGui-style sizes (0 auto, negative fills), `pack::SizeResolved` turns fill into
real pixels for widgets that can't take negatives. Enums come back as the index into the
manifest's `values`, stringLists as `pack::ZeroList` (`"a\0b\0\0"`) or `pack::List`. `expr` props
go through `pack::ExprInt` / `pack::ExprFloat`, which evaluate a bare string like `cfg.menu_key`.

Bindings: `pack::BindBool/BindInt/BindFloat/BindFloats/BindString` for required slots never
return null. They hand back the bound variable, the builder's mock value in design mode, or
per-node local storage when a live panel left the slot unbound, so the widget always draws and
reacts. `pack::OptBool/OptInt/OptFloat` return null when the node doesn't bind the slot, which
matches what generated code passes for an unbound optional binding. Arrays (`vec4[N]`) use
`pack::BindVec4Array`, which clamps the count to what's bound and seeds a sample palette
otherwise. `pack::SeedMock(ctx, slot, Value(true))` picks a friendlier design-mode start value;
call it before taking any other binding pointer in that widget.

Events: return value of the widget goes through `pack::Fire(ctx, "onChange", changed)` (or
`"onClick"`), using the event names from the manifest. In design mode `FireEvent` does nothing, so
you don't need to guard it.

Labels: `pack::Label(ctx)` returns `"text###<node id>"`. The interpreter already pushes the node id,
so this is only about keeping the widget's ImGui id stable when the text is interpolated. If your
widget prints the label, cut it at the first `##` (essentials uses `detail::LabelEnd`).

Widgets that submit no item (overlay hosts, foreground-only drawing) should call
`pack::DesignPlaceholder(ctx, "Name")` so the node has something selectable on the canvas.

Keep adapters thin. If you find yourself computing layout in an adapter, that logic belongs in the
widget. Every adapter header must pass the usual header check
(`-fno-exceptions -fno-rtti -Wall -Wextra`, ImGui 1.85 and 1.91.9b), with `include/`,
`include/imforge/` and the pack folder on the include path.

## Where they get registered

The builder loads every `packs/*/manifest.json` at startup (from `packs/` next to the exe, or the
source tree) and registers each adapter whose header exists, see `src/builder/bundled_packs.cpp`.
Adding a pack there is one `#if __has_include` block. Apps using the live client call
`imforge::live::RegisterBundledPacks()` once after creating the ImGui context; it does the same
with whichever adapter headers are on the include path.

## Tests

`tests/pack_adapters_tests.cpp` draws a panel containing every essentials type through the
interpreter and checks that items are submitted and bindings change on simulated clicks. It also
includes `packs/<pack>/imforge_adapters_<pack>.h` for controls, display and gaming when the file
exists and fails if any manifest type has no adapter (or a `callEnd` container has no end
function), so a new pack is covered as soon as its header lands. Behavior tests for your own
widgets go in the pack's own test file, written the same way.
