# controls pack

Input widgets for Dear ImGui (namespace `imb::ctl`), header-only, ImGui 1.85 up to current
master and docking. Everything is drawn with `ImDrawList` and animated through `imb_runtime.h`
(transitions and springs), but each widget is a normal ImGui item: ids come from the label,
`IsItemHovered()`/tooltips/`BeginDisabled()` work, keyboard nav reaches them, and most of them
react to the arrow keys while focused. Sizes follow the font size, colors come from the current
ImGui style unless you set them.

```
#include "imb_controls.h"     // add packs/controls and the repo's include/ to the include path
```

Every widget returns `true` when it changed the value (or, for buttons and the palette, when
something was clicked or picked). Pointers may be null, the widget then keeps a local value.

## Widgets

- `SegmentedControl` joined segments with a sliding thumb, one active, arrows move it
- `PillTabs` row of pill tabs, the filled pill slides to the active one
- `ToggleButtonGroup` joined buttons that toggle independently (bool array or bit mask)
- `Chips` toggle chips that wrap to the available width, bit mask or bools
- `RangeSlider` two thumbs on one track (float, int, `float[2]`, `ImVec2`), lo never passes hi
- `Knob` 270 degree dial, drag up or right, Shift for fine steps, double click resets
- `TickSlider` slider with tick marks, optional snapping, value bubble while dragging
- `Stepper` minus / value / plus, hold a button to repeat faster, int or float
- `SwitchToggle` switch in three looks: `Ios`, `Square`, `Icons` (check and cross in the knob)
- `IconToggle` square button that lights up, shows an icon glyph or the label text
- `TriCheckbox` off / on / mixed checkbox for "select all" rows
- `RatingStars` 0..N stars, whole or half steps, hover preview, click again to clear
- `SearchField` pill text field with magnifier, placeholder, clear button, Esc clears, optional Ctrl+F
- `TagInput` removable tags plus an input; Enter or comma adds, Backspace removes the last one
- `KeyChordPicker` captures Ctrl/Shift/Alt/Super plus a key or mouse button
- `DragNumber` scrub to change, click to type, unit suffix, several units with their own speed
- `Vec2Edit` / `Vec3Edit` number fields with colored X/Y/Z tags
- `ColorPalette` swatch grid, picks an index and/or writes the color
- `HueStrip` hue bar with a marker, horizontal or vertical
- `SatValBox` saturation/value square for a given hue
- `ColorPickerMini` the two above plus alpha strip, preview and hex input
- `GradientEditor` gradient bar with stops: click to add, drag to move, drag off or right click to remove
- `SplitButton` main action plus a dropdown arrow with alternatives
- `IconDropdown` combo with an icon per item and an animated popup
- `CommandPalette` Ctrl+P style popup with fuzzy filtering, arrows and Enter

## A few examples

```cpp
static int quality = 1;
imb::ctl::SegmentedControl("Quality", &quality, "Low\0Medium\0High\0");

static float range[2] = {20, 80};
imb::ctl::RangeSlider("Distance", range, 0.0f, 500.0f, "%.0f m");

static int shortcut = 0;
if (imb::ctl::KeyChordPicker("Toggle menu", &shortcut)) save_config();
if (imb::ctl::IsChordPressed(shortcut)) menu_open = !menu_open;

static const imb::ctl::DragUnit units[] = {{"m", 1.0f, 0.01f, "%.2f"}, {"cm", 0.01f, 1.0f, "%.0f"}};
static int unit = 0;
imb::ctl::DragNumber("Offset", &offset_m, units, 2, &unit);    // offset_m stays in meters

static bool palette = false;
static int action = -1;
const char* actions[] = {"New file", "Open file", "Save", "Settings"};
if (imb::ctl::CommandPalette("##cmd", &palette, actions, 4, &action)) run(action);
```

Most widgets have a `...Style` struct as the last argument for widths, colors and behavior
flags; zeroed colors mean "take it from the ImGui style". `imb::ctl::GetStyle()` holds the pack
wide defaults: accent, track, thumb and text colors, axis colors for the vector editors, the star
color, rounding and animation timing.

Keys and chords are ints: the key is an `ImGuiKey` on 1.87+ or the backend's `io.KeysDown`
index on 1.85/1.86, mouse buttons are `kMouseKey + button`, and modifiers add `kModCtrl`,
`kModShift`, `kModAlt`, `kModSuper`. `ChordName()` formats them ("Ctrl+Shift+S").

Gradients are `GradientStop {pos, color}` arrays or a `std::vector`; bindings use
`GradientEditorPacked`, a flat float array of `[pos, r, g, b, a]` per stop where `pos < 0`
marks an unused slot. `GradientSample()` evaluates one.

## Builder and codegen

`widgets/*.imw.json` are the manifests (category "Controls"), `imforge_adapters_controls.h`
registers the interpreter draw functions (`imforge::RegisterPackWidgets_controls`). List props
go through `{items:array}` / `{items:count}`, so empty entries are fine. `SearchField` and
`TagInput` bind `std::string`; tags are kept as one comma separated string.

## Limitations

- before 1.87 letter shortcuts (Ctrl+F, Ctrl+P) assume the backend puts ASCII codes in
  `io.KeysDown`, which the Win32 and GLFW backends do, SDL does not
- `KeyName()` / `ChordName()` return a static buffer
- the command palette keeps one filter state, so only one palette can be open at a time
- `SearchField` and `TagInput` std::string overloads cap the text at 255 / 1023 bytes, and the
  vector overloads of `TagInput` / `GradientEditor` keep what fits (1023 bytes, 32 stops) once edited
- input lines of `TagInput` and the hex field of `ColorPickerMini` come from a pool of 32
  buffers, more of them on screen at once start sharing
- the color and gradient widgets use ImGui's own `ColorEdit4`/`ColorPicker4` for exact edits
