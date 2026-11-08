# Showcase sample project

One project that puts every widget from the controls, display and gaming packs on screen, plus most
of the essentials pack, and runs 120-odd of the bundled motion presets on them. It's the thing to open
when you want to see what 0.2 can do before building your own panels.

It targets ImGui 1.91.9 (docking branch), Win32 + DirectX 11, with animations and DrawList effects on.
Codegen namespace is `showcase`.

## Panels

Each panel is drawn in a different design. The three themes come straight from
`presets/designs/` (NeonCyber, Dracula, IceFrost) and the project also has a style class per design
(`design_neoncyber`, `design_dracula`, `design_icefrost`) holding that design's full color table and
style vars. The root window of each panel uses its class, so all three look right side by side on the
canvas and in exported code, whatever `activeTheme` is set to.

`Controls` (NeonCyber) is the controls gallery. A gradient header and a typewriter headline sit above
an animated tab bar with three pages: Inputs (switches in all three kinds, icon toggles, a tri-state
box, tick, range and int-range sliders, knobs, a stepper, unit-aware drag, vec2/vec3 editors, search,
tags and two kinds of key pickers), Pickers (palette, swatch row, mini picker, hue strip, sat/value box,
gradient editor) and Choices (segmented control, pill tabs, button group, chips, icon dropdown, stars,
split button and a glow button that opens the command palette). Below the pages is a grid of ten
buttons, each wired to a different hover and press preset and named after them, so you can feel the
difference.

`Monitor` (Dracula) covers the display pack: stat cards with sparklines, a mini bar chart, the frame
time graph, a radial gauge and rings, progress steps, a timeline scrubber, three loading bar styles,
skeleton blocks, badges, an avatar, status dots, key caps and the WASD overlay, dividers, a tooltip
card, an empty state and the toast stack. The middle row is a strip of sixteen `IconLabel` instances,
each running a different loop preset (Spin, Radar, Pendulum, Orbit, ColorCycle and so on).

`Arena` (IceFrost) is the gaming pack: icon rail, watermark, FPS/ping counter, a success banner that
leaves with BackOutUp when you close it, category cards, ESP box, crosshair and FOV previews, the radar,
health/armor bar, kill feed, player rows, team colors, profile slots, hotkey list and the crosshair
editor.

## Motions

Every node uses `anim.motions`. Roughly what is where:

- Entrances: each card and column has its own (BackInLeft, RotateInDownLeft, RollIn, SwingIn, DropIn,
  Unfold, GlowIn, ZoomInUp, FadeInBottomRight...). Lists and grids use the stagger presets
  (StaggerFadeUp, StaggerScaleIn, StaggerPop, StaggerUnfold, StaggerSlideRight, StaggerFadeDown) with a
  `stagger` of 0.03 to 0.07 s.
- Exits: window exits (FadeOutDownBig, ShrinkAway, PopOut), the banner (BackOutUp) and the second kill
  feed row (FadeOutRight), which plays when `state.showSecondKill` goes false.
- Hover and press: buttons, cards and knobs. The button grid in Controls shows twenty of them.
- Loops: status dots (Ripple, Hovering, Jitter), the LIVE badge (PulseGlow), the spinner header and the
  loop strip in Monitor.
- Attention and change on events: most inputs carry a motion with `"on": "onChange"`, so flipping a
  switch plays HeartBeat, Jello, Swing or Buzz, and moving a slider plays CountBump, ValuePop or
  NudgeUp. Deleting a profile plays ErrorShake.
- Text: Typewriter on the Controls headline, GlitchIn plus NeonFlicker on the design name, TextGlowIn,
  RevealLeft and TrackingIn elsewhere.

Motions on a `Child` only move what is drawn in the parent window (glow and shadow), not the child's
own contents, so the cards here put their entrance on the stack inside the card and use
SpotlightHover on the card itself.

## Components

- `components/Panel.imc.json`: a card (`Child` with the design's `card` class) with a `title` param and
  a `content` slot. Monitor uses it for the three chart cards.
- `components/IconLabel.imc.json`: an icon over a small label, params `icon`, `label` and `color`. The
  loop strip is sixteen instances of it, with the motion set on the instance node.

## Binding

`types.json` describes `ControlsDemo`, `MonitorData` and `ArenaConfig` (header `showcase_types.h`).
The panels take them as params `ctl`, `mon` and `arena`. Scalar values that only drive the preview
(FPS, health, KPI numbers) are panel state with sensible defaults so the canvas looks alive without an
app attached; chart series are `std::vector<float>` fields and show sample data on the canvas until
you bind them.

## Checking it

```
python tools/validate_json.py examples/projects/Showcase packs --strict
imforge-fmt --check examples/projects/Showcase
```
