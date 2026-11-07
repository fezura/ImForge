# ToolDock sample project

The tool-developer sample: two dockable editor panels for an SDL3 + OpenGL 3 app on the ImGui
docking branch. Next to `CheatMenu` it's plain and dense (property grids, menus, plots, a log)
with only light animation and no shader effects.

## Files

- `imforge.json`: sdl3/gl3, `docking: true`, codegen namespace `tools`, binding types
  `Transform`/`Material`/`Stats`/`LogBuffer`, live attach over a named pipe
  (`pipe://ToolDock`), Inter and JetBrains Mono fonts.
- `panels/Inspector.imb.json`: a property editor with params `obj: Transform`,
  `mat: Material`. Two-column `Grid`s (`columnWidths: [110, "fill"]`) inside
  `CollapsingHeader`s with `InputText`, `DragFloat` (1 and 2 components), `InputInt`,
  `Checkbox`, `ColorEdit` (including an HDR `float[3]`), `SliderFloat` and `Combo`; a
  `ColorButton` that opens a `Popup` with a `ColorPicker`; a `TreeNode` with a `Selectable` and
  a `copyClipboard` action.
- `panels/LogView.imb.json`: a log window with `menuBar: true`. `MenuBar` > `Menu` >
  `MenuItem`s bound to state (`autoScroll`, `paused`, `showStats`) and `set` actions, a
  collapsible stats `Child` with `PlotLines`/`PlotHistogram` bound to `stats.frame_times`, an
  `InputText` filter kept in `state.filter`, and the log body as a locked `Code` node drawn with
  the `Mono` font.
- `themes/Graphite.theme.json`: neutral grey with an orange accent, tighter padding and
  `DockingSeparatorSize`.
- `types.json`: reflection data for `Transform`, `Material`, `Stats` (`float[120]` frame times)
  and `LogBuffer`.

## Notes

`LogView` takes `log: LogBuffer` as a param even though no widget binds to it: the `Code` node
calls `log.Filtered(...)` directly. That's the intended way to mix hand-written drawing with
generated code.

`Inspector` state (`filter`, `locked`, `showGizmo`, `lastSection`) lives in the generated
`tools::InspectorState` struct, and `onOpen` actions on the headers write `state.lastSection`.

Generated entry points: `tools::DrawInspector(tools::InspectorState&, Transform&, Material&)`
and `tools::DrawLogView(tools::LogViewState&, Stats&, LogBuffer&)`.

Validate with `python tools/validate_json.py`.
