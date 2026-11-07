# CheatMenu sample project

A mod/cheat-style menu for a Win32 + DirectX 11 game, targeting ImGui 1.91.9 (docking branch)
with every ImForge feature switched on. It touches most of the [file formats](../../../docs/SPEC.md#file-formats)
and [stock widgets](../../../docs/SPEC.md#stock-widgets), and the interpreter, codegen and
round-trip tests use it as input.

## Files

- `imforge.json`: win32/dx11, all features on, codegen namespace `menu`, binding types
  `Player`/`Settings`, live attach over TCP 7777, four fonts (Inter with Font Awesome 6 merged),
  three style classes (`danger`, `accent`, `muted`).
- `panels/MainMenu.imb.json`: the menu window. Params `player: Player`, `cfg: Settings`; state
  `open`, `selectedTab`, `fps`, `showAdvanced`, `themeIndex`, `configName`, `status`,
  `aboutOpen`.
- `panels/Overlay.imb.json`: a full-screen HUD, a `NoTitleBar|NoBackground|NoInputs` window
  with a `Canvas` of anchored texts, a health bar and a hand-written FOV circle.
- `components/Card.imc.json`: a bordered `Child` with a colored title row and a `content` slot.
  Params `title: string`, `accent: color`.
- `components/LabeledSlider.imc.json`: an `HStack` of label + `SliderFloat` bound through a
  `binding<float>` param, plus a `content` slot.
- `themes/Dark.theme.json`, `themes/Neon.theme.json`: full `ImGuiCol` tables (1.91.9, docking
  colors included) and about 30 style vars.
- `types.json`: hand-written [reflection data](../../../docs/SPEC.md#binding-tool) for
  `Player`, `Settings` (with nested `EspSettings`, `AimSettings`, `MiscSettings`) and the `Mode`
  enum.

## What MainMenu shows off

The header is an `HStack` with interpolated text (`{player.name}`, `FPS {state.fps:%.0f}`), an
icon glyph (`{icon:fa-crosshairs}`), a `Dummy` with `fill` width as a spacer, a *Panic* button
that runs several `set` actions plus a handler, and an *Advanced* button that toggles
`state.showAdvanced` and calls its handler.

Below that is a `TabBar` (`Aimbot`, `Visuals`, `Misc`, `Settings`). The tab bodies are
`HStack`/`Grid` layouts of `Card` instances whose `content` slots hold `Checkbox`, `Combo`,
`ColorEdit`, `ColorPicker`, `SliderFloat`, `ProgressBar` and `LabeledSlider` instances, all
bound to `cfg.*` paths from `types.json`. A `Combo` bound to the `Mode` enum has no `items`; the
names come from reflection.

The advanced cards use `visibleIf` (`state.showAdvanced && cfg.esp.enabled`), there's an enum
comparison (`player.mode == Mode::Rage`), and dependent controls use `enabledIf`.

For looks: style overrides and classes, hover/active transitions on buttons, `FadeSlideUp`
enter presets with `stagger` on every tab body, `ScalePop` on the low-health warning, and on
the window a shader `blur`, a drop `shadow` and an animated `Sweep` border.

Every action type is used somewhere: `set`, `toggle`, `openPopup`/`closePopup` (the About
`Modal`), `setTab` (jump to Visuals) and `copyClipboard` (a share code built with
interpolation). There's a `Code` node (`ping_block`) that's emitted as a locked block, and a
footer `Canvas` with a status text anchored `Left` and a watermark anchored `TopRight`.

## Generated code

Export writes `generated/MainMenu.h/.cpp`, `generated/Overlay.h/.cpp`,
`generated/imforge_components.h/.cpp`, `generated/imforge_styles.h`, `generated/imforge_fonts.h`,
`generated/imforge_reflect.h` and `generated/imforge_dev.h`, all in `namespace menu`. The entry
points are `menu::DrawMainMenu(menu::MainMenuState&, Player&, Settings&)` and
`menu::DrawOverlay(menu::OverlayState&, Player&, Settings&)`.

## Checking it

```
python tools/validate_json.py
```

checks this folder against `schemas/` plus the rules a schema can't express: unique ids, Tab
under TabBar, component names, binding paths against `types.json`, state variables, colors.
