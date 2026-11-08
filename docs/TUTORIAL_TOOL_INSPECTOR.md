# Tutorial: a dockable inspector for your tool

This one is for people building editors and debug tools rather than game menus. It uses the
`ToolDock` sample (`examples/projects/ToolDock`): two dockable panels for an SDL3 + OpenGL 3
app on the Dear ImGui docking branch, with dense property grids instead of cards, a
frame-time plot, menus bound to panel state and a hand-written log body. We'll open the
sample, attach an app over a named pipe, make a few edits, and export `tools::DrawInspector`
and `tools::DrawLogView`.

## 1. Open the sample

Use File > Open... and pick `examples/projects/ToolDock`. Then look at Project Settings
(Project > Settings...) to see how it differs from a menu project:

| Setting | Value | Why |
|---|---|---|
| Target | ImGui `1.91.9`, branch `docking` | docking needs the docking branch; the builder hides docking features otherwise |
| Platform | `sdl3` + `gl3` | selects the GL3 shader-fx backend and the example client |
| Features | `docking: true`, `animations`, `iconFonts`, `dpiScaling`; no DrawList or shader fx | a tool shouldn't blur anything |
| Codegen | namespace `tools`, split header, `functionCase: pascal`, `variableCase: snake`, `stdString: true`, `baseFontSize: 15` | `stdString` makes `InputText` bind to `std::string` |
| Binding | types `Transform`, `Material`, `Stats`, `LogBuffer`; `types.json` in the project | hand-written reflection data, see step 3 |
| Live | transport `pipe`, name `ToolDock` | the URL your app connects to is `pipe://ToolDock` |
| Fonts | `Default` and `Bold` (Inter 15 px, Font Awesome merged), `Mono` (JetBrains Mono 14 px) | the log body uses `Mono` |

There are two panels: `Inspector` (parameters `obj: Transform`, `mat: Material`) and
`LogView` (parameters `stats: Stats`, `log: LogBuffer`). Switch between them with
Project > Panels.

## 2. What docking changes

Nothing in the panel file. A panel's root is an ordinary `Window` and generated code calls
`ImGui::Begin`, so the window docks wherever your app lets it. The app turns docking on and
provides a dock space:

```cpp
ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
// each frame, before drawing the panels
ImGui::DockSpaceOverViewport();
```

With `docking` on, the `Window.flags` editor offers the docking-branch flags (`NoDocking`,
...) and the Theme Editor lists the docking colors (`DockingPreview`, `DockingEmptyBg`) and
`DockingSeparatorSize`, which the `Graphite` theme sets. On a non-docking target those
entries show as "(not in this ImGui)" and are version-guarded in the generated theme
function. The Canvas always draws the window as a child region, docked or not.

## 3. Reflection data

`types.json` describes the four structs, and your app has to agree with it, so declare the
same fields and reflect them:

```cpp
struct Transform { std::string name; ImVec2 position; float rotation; ImVec2 scale; int layer; bool visible; };
struct Material  { std::string name; ImVec4 albedo; float emissive[3]; float roughness, metallic;
                   int shader; bool double_sided; std::string texture; };
struct Stats     { float frame_times[120]; float frame_time_ms, fps; int draw_calls, triangles;
                   float memory_mb; int log_count; std::string last_log; };
struct LogBuffer { int capacity, count, dropped;  /* plus whatever Filtered() needs */ };

IMFORGE_REFLECT(Transform, name, position, rotation, scale, layer, visible);
IMFORGE_REFLECT(Material, name, albedo, emissive, roughness, metallic, shader, double_sided, texture);
IMFORGE_REFLECT(Stats, frame_times, frame_time_ms, fps, draw_calls, triangles, memory_mb, log_count, last_log);
IMFORGE_REFLECT(LogBuffer, capacity, count, dropped);
```

`LogView` never binds to `LogBuffer`'s fields. It still takes `log` as a parameter because
its `Code` node calls `log.Filtered(...)` directly (step 7). Only members you bind or use in
expressions have to be in the table.

In a real project you'd generate `types.json` instead of writing it: `imforge-reflect
--compile-commands build/compile_commands.json --types Transform,Material,Stats,LogBuffer
--out-json types.json --out-header generated/imforge_reflect.h` ([BINDING.md](BINDING.md)).

## 4. The property grid

Select `transform_header > transform_grid` in `Inspector`. It's a `Grid` with
`columns: 2`, `columnWidths: [110, "fill"]` (a DPI-scaled pixel width, then the rest) and
`flags: SizingStretchProp | BordersInnerH | PadOuterX`.

Children fill the cells row by row: a `Text` label with the `label` style class, then the
editor widget, then the next label, and so on. To add a row for a new field, drag a `Text`
and a `DragFloat` from the Palette to the end of the grid. Set the text to `Mass`, and in the
drag's Properties section set `label` to `##mass` (the label is hidden because the grid
column already is one), `speed` to `0.1` and `format` to `"%.2f kg"`. Then add `mass` to both
the struct and `types.json` and bind `value` to `obj.mass`.

The generated code looks like this:

```cpp
if (ImGui::BeginTable("transform_grid", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_PadOuterX)) {
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 110 * imb_s);
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableNextColumn(); ImGui::PushID("name_lbl"); ui::Style_label_Push(); ImGui::TextUnformatted("Name"); ui::Style_label_Pop(); ImGui::PopID();
    ImGui::TableNextColumn(); ImGui::PushID("name_input"); if (ImGui::InputText("##name", &obj.name)) Inspector_name_input_OnChange(state, obj, mat); ImGui::PopID();
    ...
    ImGui::EndTable();
}
```

Every cell is wrapped in `PushID(node id)`, which is why the same `##` label in different
rows never collides. The `onChange` handler on `name_input` is where a real tool would mark
the scene dirty.

The two-component drags (`pos_drag`, `scale_drag`) are `DragFloat` nodes with
`components: 2` bound to an `ImVec2`, and the generator emits
`DragFloat2(..., &obj.position.x, ...)`. `rot_drag` uses `flags: WrapAround` and
`format "%.1f deg"`; `scale_drag` uses `AlwaysClamp` with `min 0.01`. Flags are
version-gated, so a flag the target version doesn't have is left out of the generated `|`
list and ignored by the live interpreter.

## 5. PlotLines bound to a vector

`LogView > stats_child > frame_plot` is a `PlotLines` whose `values` slot is bound to
`stats.frame_times`, a `float[120]` in the sample. A growing history is more natural as a
vector, so change the struct and the reflection entry:

```cpp
struct Stats { std::vector<float> frame_times; ... };
```

```json
{ "name": "frame_times", "type": "std::vector<float>", "ctype": "Vector", "elem": "float" }
```

The slot accepts `float[] | vector<float>`, so nothing changes in the panel. The generated
call becomes

```cpp
ImGui::PlotLines("##frames", stats.frame_times.data(), (int)stats.frame_times.size(), 0,
                 overlay, scale_min, scale_max, ImVec2(-FLT_MIN, 60 * imb_s));
```

with `overlay`, `scaleMin`, `scaleMax` and `size` taken from the node's props. In the live
app the interpreter reads `data()` and `size()` through the reflection table, so the plot
scrolls as your app pushes samples. `stats.frame_times.size` works in expressions too, e.g. a
`Text` with `"{stats.frame_times.size} samples"`.

## 6. Panel state and menus

`LogView` has the state variables `open`, `filter` (string), `autoScroll`, `paused`,
`showStats` (bool) and `minLevel` (int); you can see them in Panel Settings. They become
fields of the generated struct, renamed to the project's `variableCase`:

```cpp
struct LogViewState { bool open = true; std::string filter; bool auto_scroll = true;
                      bool paused = false; bool show_stats = true; int min_level = 0; };
```

The window has `menuBar: true` and a `MenuBar > Menu "View"` whose `MenuItem`s bind
`selected` to `state.autoScroll`, `state.paused` and `state.showStats`. ImGui's `MenuItem`
with a `bool*` toggles them, no action needed. The `Level` items use `set` actions on
`state.minLevel` (0, 1, 2), and the `Clear` item has `handler: true`, which becomes
`LogView_mi_clear_OnClick(LogViewState&, Stats&, LogBuffer&)`.

The stats child has `visibleIf: state.showStats` plus `FadeSlideDown` / `Fade` enter and exit
presets, so toggling the menu item animates it in and out, and a `Text` with
`visibleIf: state.paused` shows a "paused" hint. Over in `Inspector`, `lock_btn` flips
`state.locked` with a `toggle` action, `locked_hint` is visible while it's set, and the
collapsing headers' `onOpen` actions write `state.lastSection`.

The `InputText` filter nodes bind `value` to `state.filter`. With `stdString: true` that's
`ImGui::InputText("##filter", &state.filter, ImGuiInputTextFlags_AutoSelectAll)` through
`misc/cpp/imgui_stdlib.h`, so add `imgui_stdlib.cpp` to your build. With `stdString: false`
the generator uses a `char[maxLength]` buffer instead (`maxLength` is a prop on the node).

## 7. The hand-written log body

`LogView > log_body > log_lines` is a `Code` node (`preview: Placeholder`) whose text calls
into your code:

```cpp
for (const LogLine& line : log.Filtered(state.filter.c_str(), state.min_level)) {
    ImGui::PushStyleColor(ImGuiCol_Text, LevelColor(line.level));
    ImGui::TextUnformatted(line.text.c_str());
    ImGui::PopStyleColor();
}
if (state.auto_scroll && !state.paused && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) ImGui::SetScrollHereY(1.0f);
```

Its parent child region sets `font: Mono`. On export the body ends up in a locked function

```cpp
// @imb:locked id=log_lines h=...
static void LogView_log_lines_Block(LogViewState& state, Stats& stats, LogBuffer& log) { ... }
// @imb:end id=log_lines
```

that you can edit freely afterwards; re-exports keep it as is. Put the `#include` for
`LogLine` and `LevelColor` in the `@imb:user id=includes` slot.

The live interpreter can't run C++, so in dev builds the same function is compiled into
your app and registered. `generated/imforge_dev.h` has
`imforge::live::RegisterBlock("LogView.log_lines", &LogView_log_lines_Block_Live);` for
that. Until it's registered the live panel draws a placeholder box, and if you change the
block without rebuilding the app, the Live panel's Blocks & handlers tab shows
`stale: recompile` next to it.

## 8. Attach your tool over the pipe

Start from `examples/client/gl3_sdl3/main.cpp` (the stock SDL3 + OpenGL 3 example with the
ImForge lines marked) and replace the demo frame with your panels:

```cpp
#define IMFORGE_LIVE
#include "imforge/imforge_client.h"
#include "tool_types.h"                      // structs + IMFORGE_REFLECT from step 3

static Transform g_obj; static Material g_mat; static Stats g_stats; static LogBuffer g_log;

// once, after ImGui_ImplOpenGL3_Init
imforge::live::Connect("pipe://ToolDock");
imforge::live::SetFontTextureRebuild([] { ImGui_ImplOpenGL3_DestroyFontsTexture(); ImGui_ImplOpenGL3_CreateFontsTexture(); });
#include "generated/imforge_dev.h"           // registers LogView.log_lines and the handlers (after a first export)

// every frame, before ImGui_ImplOpenGL3_NewFrame()
imforge::live::PreNewFrame();

// every frame, after ImGui::NewFrame()
imforge::live::NewFrame();
ImGui::DockSpaceOverViewport();
imforge::live::Draw("Inspector", { imforge::live::Bind("obj", g_obj), imforge::live::Bind("mat", g_mat) });
imforge::live::Draw("LogView",   { imforge::live::Bind("stats", g_stats), imforge::live::Bind("log", g_log) });
```

`Bind` only compiles for types that have an `IMFORGE_REFLECT` table. The Live panel's
transport row already says `pipe` / `ToolDock`, so press Start (the endpoint is
`\\.\pipe\ToolDock` on Windows and `/tmp/imforge-ToolDock.sock` on Linux) and run your tool.
It pairs automatically, the status line turns green and both panels show up in your dock
space. Dock them wherever you like; the layout is your app's `imgui.ini`, not the panel's.

Now edit the grid from step 4 while attached. Rows appear in the tool as you add them, and
the type check in Properties catches a wrong path before the interpreter does. Press
Ctrl+Shift+P and click a field in your tool to jump to it in the builder.

## 9. Export

Ctrl+E and Export write `generated/Inspector.h/.cpp`, `generated/LogView.h/.cpp`,
`generated/imforge_styles.h` (the `label` and `warning` classes),
`generated/imforge_theme_Graphite.h`, `generated/imforge_fonts.h`,
`generated/imforge_reflect.h` and `generated/imforge_dev.h`. The entry points are

```cpp
void tools::DrawInspector(tools::InspectorState& state, Transform& obj, Material& mat);
void tools::DrawLogView(tools::LogViewState& state, Stats& stats, LogBuffer& log);
```

Replace the two `imforge::live::Draw` calls with them, keep one `InspectorState` and one
`LogViewState` per window instance, drop `IMFORGE_LIVE` and build. The output depends on Dear
ImGui and, because the sample uses enter/exit presets, on `imb_runtime.h`. Turn off
`animations` in the project features if you want files with no extra dependency.

To see round-trip at work, rename a label in `Inspector.cpp` and re-export: the new label is
read back into the panel. Change the structure of a generated block instead and the
Conflicts panel asks whether to keep your code (the node becomes locked) or regenerate
([CODEGEN.md](CODEGEN.md)).
