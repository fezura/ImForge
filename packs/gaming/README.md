# gaming pack

Widgets for the settings menus and overlays you see around games: a crosshair editor, a box
overlay preview, a radar, config profile slots, hotkey tables, watermarks, player rows and so on.
Namespace `imb::game`, header-only, Dear ImGui 1.85 and later (master and docking).

To be clear about scope: these are UI previews and editors. They draw values you hand them
and edit values you own. Nothing here reads game memory, injects, hooks or aims at anything.
The radar draws the points you pass in, the box preview draws a mock silhouette over a fake
scene, and the crosshair is a picture of your settings.

```
packs/gaming/
  imb_gaming.h                umbrella header, include this one
  imb_game_core.h             Style/GetStyle, shared drawing helpers
  imb_game_esp.h              EspBoxPreview, FovCirclePreview
  imb_game_crosshair.h        Crosshair, DrawCrosshair, CrosshairPreview, CrosshairEditor
  imb_game_radar.h            RadarMiniMap
  imb_game_config.h           ProfileSlots, HotkeyList, TeamColorPicker
  imb_game_nav.h              IconRailNav, CategoryCard
  imb_game_overlay.h          Watermark, FpsPingCounter, KillFeedEntry, NotificationBanner
  imb_game_players.h          PlayerListRow, HealthArmorBar
  imforge_adapters_gaming.h   interpreter glue for the builder and live client
  widgets/*.imw.json          one manifest per widget
```

It builds on the essentials pack (colors, rounding and accent come from `imb::ess::GetStyle()`,
`HotkeyList` uses its `KeybindPicker`), so put both pack folders and the repo's `include/` on the
include path:

```
g++ -std=c++17 -fno-exceptions -fno-rtti -Wall -Wextra -Ipacks/gaming -Ipacks/essentials -Iinclude -I<imgui> ...
python tools/pack_imblib.py packs/gaming dist/gaming.imblib
```

## A config screen

```cpp
#include "imb_gaming.h"

static int page = 0;
static imb::game::Crosshair ch;
static imb::game::EspPreview esp;
static int keys[3] = {};
static const char* actions[] = {"Toggle menu", "Panic key", "Screenshot"};

imb::game::IconRailNav("rail", "H\0V\0S\0", "Home\0Visuals\0Settings\0", &page);
ImGui::SameLine();
ImGui::BeginGroup();
if (page == 1) {
    imb::game::CategoryCard("Boxes", "Box, name and health bar", "B", &esp.box);
    imb::game::EspBoxPreview("esp", &esp);
    imb::game::CrosshairEditor("Crosshair", &ch);
} else if (page == 2) {
    static int sel = 0;
    if (imb::game::ProfileSlots("profiles", "Default\0Ranked\0Casual\0", &sel, 0) == imb::game::ProfileAction_Save)
        SaveProfile(sel);
    imb::game::HotkeyList("keys", actions, keys, 3);
}
ImGui::EndGroup();
imb::game::Watermark("MyTool", nullptr, imb::game::Corner_TopRight, true, true);
```

## Notes

Everything is drawn with `ImDrawList` and sized from the font, so DPI scaling works without extra setup. Each
widget is one item (an `InvisibleButton`, `Dummy` or group), so `IsItemHovered`, `PushID`,
`BeginDisabled` and tooltips behave as usual. Composite widgets hit-test their own parts instead
of overlapping items, which keeps them identical from 1.85 to master.

State pointers can be null. `IconRailNav`, `ProfileSlots`, `HotkeyList`, `TeamColorPicker`,
`FovCirclePreview` and `NotificationBanner` then keep their value in the window's ImGui storage,
which is what generated code relies on for unbound slots. The `*Fields` functions
(`EspBoxPreviewFields`, `CrosshairPreviewFields`, `CrosshairEditorFields`) take one pointer per
setting, matching how the builder binds them; the struct versions are nicer to call by hand.

Overlays (`Watermark`, `FpsPingCounter`) take a corner. `Corner_None` lays them out inline, any
other corner floats them over the main viewport through the foreground draw list. The adapters
always draw them inline on the builder canvas. `NotificationBanner` pushes content down when
inline, or opens a small borderless window at the top of the viewport with `overlay`.

`KillFeedEntry` wants the age of the event in seconds and returns false once it's expired, so a
feed is a loop that drops entries when that happens. `HealthArmorBar` keeps the damage trail in
ImGui storage under its id; feed it the raw values every frame.

Animations go through `imb_runtime.h` like essentials. With `IMB_ESS_NO_RUNTIME` defined you get
the essentials fallbacks (linear transitions, simpler glows).

Tests that need the rect of a sub-item (a slider inside `CrosshairEditor`, a button in
`ProfileSlots`) define `IMB_GAME_ITEM_HOOK(name)` before including the pack. It's a no-op otherwise.

## Limitations

- `RadarMiniMapPacked` and the zero separated overloads take at most 64 points or names
  (32 for profile slots and rail icons).
- `Watermark` reads the local clock through `localtime_s` / `localtime_r`.
- Overlay corners use the main viewport; with multi-viewports enabled they won't follow other
  platform windows.
