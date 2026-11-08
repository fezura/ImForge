# Widget packs

Besides the stock ImGui widgets, ImForge comes with four packs of extra widgets, 74 in total.
They're plain header-only C++ (Dear ImGui 1.85 and later, master and docking, no exceptions or
RTTI), drawn with `ImDrawList` and animated through `imb_runtime.h`. Each widget is a normal
ImGui item, so `IsItemHovered`, tooltips, `PushID` and `BeginDisabled` work the way you'd
expect, and sizes follow the font size so DPI scaling needs nothing extra. Colors come from the
current ImGui style unless you pass your own.

| Pack | Folder | Namespace | Include | Widgets |
|---|---|---|---|---|
| essentials | `packs/essentials` | `imb::ess` | `imb_essentials.h` | 11 |
| controls | `packs/controls` | `imb::ctl` | `imb_controls.h` | 27 |
| display | `packs/display` | `imb::disp` | `imb_display.h` | 20 |
| gaming | `packs/gaming` | `imb::game` | `imb_gaming.h` | 16 |

To use one by hand, put the pack folder and ImForge's `include/` on the include path (the
gaming pack also needs `packs/essentials`) and include the umbrella header. Each pack's README
has the full API and its limitations.

## In the builder

The builder loads every bundled pack at startup. Pack widgets show up in the Palette grouped by
pack, and in the Widgets tab of the Library (Ctrl+L) as cards with a live preview; click a card
to add it to the open document or drag it onto the Canvas or Hierarchy. Canvas and the live
client draw the real widgets through each pack's adapter header (`imforge_adapters_<pack>.h`,
see [packs/ADAPTERS.md](../packs/ADAPTERS.md)). For the live client in your app, put the pack
folders on the include path and call `imforge::live::RegisterBundledPacks()` once before
`Connect()`; it registers every adapter header it can find.

Export calls the pack functions directly and adds the pack's include, so a shipped app needs
the pack headers and nothing else. The manifests that describe each widget's props, bindings
and events are in `packs/<pack>/widgets/*.imw.json`, in the format described in
[WIDGET_SDK.md](WIDGET_SDK.md). `python tools/pack_imblib.py packs/<pack> dist/<pack>.imblib`
packs one up as an `.imblib` for other projects.

## essentials

The widgets from 0.1, with a cheat menu or tool window look.

| Widget | What it is |
|---|---|
| `Toggle` | animated on/off switch |
| `KeybindPicker` | click, then press a key or mouse button to bind it; Escape clears |
| `ColorSwatchRow` | row of color swatches, click to select, with a picker popup |
| `HotkeyLabel` | text with a keycap badge for its key |
| `Spinner` | loading spinner |
| `GradientHeader` | gradient title strip |
| `AnimatedTabBar` | tabs across the top with a sliding underline |
| `AnimatedSideTabs` | vertical icon and label tabs with a sliding pill |
| `GlowButton` | button that glows on hover |
| `SectionCard` | titled card container, optionally collapsible (pairs with `EndSectionCard`) |
| `ToastHost` | draws the toasts queued with `PushToast` in a corner |

```cpp
#include "imb_essentials.h"

imb::ess::AnimatedSideTabs("tabs", "Aim\0Visuals\0Misc\0", "A\0V\0M\0", &tab);
ImGui::SameLine();
imb::ess::SectionCard("Visuals");
imb::ess::Toggle("Enabled", &cfg.enabled);
imb::ess::KeybindPicker("Toggle key", &cfg.toggle_key);
if (imb::ess::GlowButton("Save config")) imb::ess::PushToast("Config saved", imb::ess::ToastType::Success);
imb::ess::EndSectionCard();
imb::ess::DrawToasts();   // once per frame
```

## controls

Input widgets. Every one returns true when it changed its value (or, for buttons and pickers,
when something was clicked or picked), and most react to the arrow keys while focused.

| Widget | What it is |
|---|---|
| `SegmentedControl` | joined segments with a sliding thumb, one active |
| `PillTabs` | row of pill tabs, the filled pill slides to the active one |
| `ToggleButtonGroup` | joined buttons that toggle independently (bool array or bit mask) |
| `Chips` | toggle chips that wrap to the available width |
| `RangeSlider` | two thumbs on one track for a float range |
| `RangeSliderInt` | the same for ints |
| `Knob` | 270 degree dial; drag up or right, Shift for fine steps, double-click resets |
| `TickSlider` | slider with tick marks, optional snapping and a value bubble |
| `Stepper` | minus, value, plus; holding a button repeats faster |
| `SwitchToggle` | switch in three looks: `Ios`, `Square` and `Icons` |
| `IconToggle` | square button that lights up, with an icon or the label |
| `TriCheckbox` | off, on and mixed, for "select all" rows |
| `RatingStars` | 0 to N stars, whole or half steps |
| `SearchField` | pill text field with a magnifier, placeholder and clear button |
| `TagInput` | removable tags plus an input; Enter or comma adds one |
| `KeyChordPicker` | captures Ctrl, Shift, Alt, Super plus a key or mouse button |
| `DragNumber` | scrub or type a number with a unit suffix, several units with their own speed |
| `Vec2Edit` | two number fields with colored X and Y tags |
| `Vec3Edit` | the same with X, Y and Z |
| `ColorPalette` | swatch grid that picks an index and/or writes the color |
| `HueStrip` | hue bar with a marker |
| `SatValBox` | saturation and value square for a given hue |
| `ColorPickerMini` | hue strip, saturation box, alpha strip, preview and hex input in one |
| `GradientEditor` | gradient bar with stops; click to add, drag to move, drag off to remove |
| `SplitButton` | main action plus a dropdown arrow with alternatives |
| `IconDropdown` | combo with an icon per item |
| `CommandPalette` | Ctrl+P style popup with fuzzy filtering |

```cpp
#include "imb_controls.h"

static int quality = 1;
imb::ctl::SegmentedControl("Quality", &quality, "Low\0Medium\0High\0");

static float range[2] = {20, 80};
imb::ctl::RangeSlider("Distance", range, 0.0f, 500.0f, "%.0f m");

static int shortcut = 0;
if (imb::ctl::KeyChordPicker("Toggle menu", &shortcut)) save_config();
if (imb::ctl::IsChordPressed(shortcut)) menu_open = !menu_open;
```

## display

Read-mostly widgets: progress, small charts, badges, cards and notifications. Values that
change glide to the new value, and most widgets sweep in from zero the first time they show
(`imb::disp::GetStyle().intro = false` turns that off).

| Widget | What it is |
|---|---|
| `RingProgress` | circular progress ring; a negative value spins |
| `RadialGauge` | needle gauge with warning and danger ranges |
| `Sparkline` | tiny line chart, hover shows the sample |
| `MiniBars` | small labeled bar chart |
| `FrameGraph` | frame time graph with average, fps and 1% low; can sample `io.DeltaTime` itself |
| `LoadingBar` | progress bar, indeterminate or segmented |
| `SkeletonBlock` | shimmering placeholder (rect, line or circle) while content loads |
| `Badge` | pill, count or dot badge, optionally pinned to the previous item's corner |
| `Avatar` | round avatar with initials or a texture and a status dot |
| `StatusDot` | online, away, busy or offline dot with an optional label |
| `StatCard` | KPI card with a value that counts up, a delta and a sparkline |
| `ProgressSteps` | numbered step indicator |
| `TimelineScrubber` | time bar with markers; drag to scrub |
| `TooltipCard` | tooltip drawn as a card with a title and header color |
| `ToastStack` | stacked toasts from `PushToast`, with kinds and progress |
| `Breadcrumbs` | clickable path; the last item is the current location |
| `KeyOverlay` | W, A, S, D, Space and mouse buttons lighting up as they're pressed |
| `Divider` | separator with an optional label, as a line, fade, dashes or dots |
| `Kbd` | key caps for a shortcut like "Ctrl+Shift+P" |
| `EmptyState` | icon, title, hint and an optional button for an empty list |

```cpp
#include "imb_display.h"
namespace disp = imb::disp;

disp::RingProgress("Download", job.progress);          // < 0 spins
disp::RadialGauge("km/h", speed, 0, 240, 160, 210);    // yellow above 160, red above 210
disp::Sparkline("##ping", ping.data(), (int)ping.size());
disp::FrameGraph("Frame time");                        // keeps its own history

disp::PushToast("Config saved", "legit.json", disp::ToastKind::Success);
disp::ToastStack();                                    // once per frame
```

## gaming

Widgets for the settings menus and overlays you see around games. These are UI previews and
editors only: they draw values you hand them and edit values you own. Nothing in the pack reads
game memory, injects, hooks or aims at anything. The radar draws the points you pass in, the box
preview draws a mock figure over a fake scene, and the crosshair is a picture of your settings.

| Widget | What it is |
|---|---|
| `EspBoxPreview` | preview of box overlay settings (full, corner or 3D box, health bar, name, distance) on a mock figure |
| `CrosshairPreview` | picture of a crosshair config; click to cycle backdrops for contrast |
| `CrosshairEditor` | crosshair preview with sliders, toggles, colors and a few presets |
| `FovCirclePreview` | circle of a given radius on a scaled screen; drag the ring to change it |
| `RadarMiniMap` | round or square radar of the points you pass, with zoom, rings and sweep |
| `ProfileSlots` | config slots with select, load, save and delete; it only reports the action |
| `HotkeyList` | action names with a keybind badge each; click a badge to rebind |
| `Watermark` | app name, extra text, fps and clock over a flowing gradient bar |
| `FpsPingCounter` | fps and ping readout, optionally with a small graph |
| `PlayerListRow` | selectable row with name, team color, health, flags and ping |
| `TeamColorPicker` | two team swatches, a swap button and preset pairs |
| `IconRailNav` | vertical icon-only navigation with tooltips |
| `CategoryCard` | icon, title, description and a switch; clicking the card flips it |
| `HealthArmorBar` | health and armor bars with a damage trail |
| `KillFeedEntry` | one kill feed line that fades out after its lifetime |
| `NotificationBanner` | banner with text and an action button, inline or over the top of the screen |

`Watermark` and `FpsPingCounter` take a corner: `Corner_None` lays them out inline, any other
corner floats them over the main viewport. The gaming pack builds on essentials for its colors
and the keybind badge, so it needs `packs/essentials` on the include path too.

```cpp
#include "imb_gaming.h"

static int page = 0;
static imb::game::Crosshair ch;
static imb::game::EspPreview esp;

imb::game::IconRailNav("rail", "H\0V\0S\0", "Home\0Visuals\0Settings\0", &page);
ImGui::SameLine();
ImGui::BeginGroup();
if (page == 1) {
    imb::game::CategoryCard("Boxes", "Box, name and health bar", "B", &esp.box);
    imb::game::EspBoxPreview("esp", &esp);
    imb::game::CrosshairEditor("Crosshair", &ch);
}
ImGui::EndGroup();
imb::game::Watermark("MyTool", nullptr, imb::game::Corner_TopRight, true, true);
```
