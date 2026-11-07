# essentials pack

Header-only Dear ImGui widgets (namespace `imb::ess`) with a modern cheat-menu / tool look.
They're drawn with `ImDrawList` and animated through `imb_runtime.h`, but behave like normal
ImGui items: each widget is an `InvisibleButton` or `Dummy`, so `IsItemHovered`, tooltips,
`PushID` scoping and `BeginDisabled` work as usual. Works with Dear ImGui 1.85 and later,
master and docking.

```
packs/essentials/
  imb_essentials.h        umbrella header, include this one
  imb_ess_core.h          Style/GetStyle, KeyName, Spinner, HotkeyLabel, GradientHeader, GlowButton
  imb_ess_controls.h      Toggle, KeybindPicker, ColorSwatchRow
  imb_ess_layout.h        AnimatedTabBar, AnimatedSideTabs, SectionCard/EndSectionCard
  imb_ess_toast.h         PushToast / DrawToasts
  widgets/*.imw.json      one manifest per widget
  manifest.json           the .imblib manifest
```

`imb_essentials.h` includes `"imforge/imb_runtime.h"`, so put the repo's `include/` directory
(or an extracted imblib directory that has an `imforge/` folder) on the include path. Define
`IMB_ESS_NO_RUNTIME` to build without the runtime; you get plain fallbacks (linear transitions,
multi-color gradients, ring glows).

```
g++ -std=c++17 -fno-exceptions -fno-rtti -Wall -Wextra -Ipacks/essentials -Iinclude -I<imgui> ...
python tools/pack_imblib.py packs/essentials dist/essentials.imblib
```

## Typical screens

A cheat menu: `AnimatedSideTabs` on the left (icon + label, sliding pill), `GradientHeader` as
the title bar, a `SectionCard` per feature group with `Toggle`s, `KeybindPicker`s and
`ColorSwatchRow`s, a `GlowButton` for "Save config", `HotkeyLabel("Toggle menu", cfg.menu_key)`
in the footer, and `DrawToasts()` once per frame for "Config saved" style feedback.

A tool window: `AnimatedTabBar` across the top (underline indicator), auto-height
`SectionCard`s, a `Spinner` next to running jobs and a `ToastHost` node for background
notifications.

## API

```cpp
namespace imb::ess {
struct Style { float rounding, card_rounding, anim_duration = 0.15f, glow_size, toast_width;
               ImU32 accent, track, knob, text, text_dim, border, card_bg, card_header, keycap_bg,
               toast_bg, toast_info, toast_success, toast_warning, toast_error; };
Style& GetStyle();                       // 0 colors/sizes fall back to the ImGui style / font size

struct ToggleStyle { ImU32 on_color, off_color, knob_color; float width, height; bool show_label = true;
                     bool disabled = false; float disabled_alpha = 0.5f; };
bool Toggle(const char* label, bool* v, const ToggleStyle& = {});
bool Toggle(const char* label, bool* v, ImU32 on_color, ImU32 off_color = 0);
bool Toggle(const char* label, bool* v, const ImVec4& on_color, const ImVec4& off_color = {});

struct KeybindStyle { ImVec2 size; ImU32 bg, bg_hover, bg_capture, text; bool show_label = true; bool allow_mouse = true; };
bool KeybindPicker(const char* label, int* key, const KeybindStyle& = {});   // true when *key changed
bool IsCapturingKeybind();
const char* KeyName(int key);            // "None", "Mouse1".."Mouse5", "F1", "A", "Key 123"

bool ColorSwatchRow(const char* label, ImVec4* colors, int count, int* selected, float swatch_size = 0);
void HotkeyLabel(const char* text, int key);
void Spinner(const char* id, float radius, float thickness, ImU32 color);
void GradientHeader(const char* label, ImU32 c0, ImU32 c1, float height = 0, bool centered = false);
void GradientHeader(const char* label, const ImVec4& c0, const ImVec4& c1, float height = 0, bool centered = false);
bool GlowButton(const char* label, const ImVec2& size = {}, ImU32 color = 0);
bool GlowButton(const char* label, const ImVec2& size, const ImVec4& color);

struct TabStyle { float height, width, padding, indicator_thickness, rounding = -1; bool pill = false, stretch = true;
                  ImU32 bg, indicator, text, text_active, hover; };
bool AnimatedTabBar(const char* id, const char* const* labels, int count, int* index, const TabStyle& = {});
bool AnimatedTabBar(const char* id, const char* labels_zero_separated, int* index, const TabStyle& = {});
bool AnimatedSideTabs(const char* id, const char* const* labels, const char* const* icons, int count, int* index, const TabStyle& = {});
bool AnimatedSideTabs(const char* id, const char* labels_zero_separated, const char* icons_zero_separated, int* index, const TabStyle& = {});

void SectionCard(const char* title, const ImVec2& size = {}, bool* open = nullptr);   // always pair with
void EndSectionCard();                                                                 // EndSectionCard()

enum class ToastType { Info, Success, Warning, Error };
enum class ToastCorner { TopLeft, TopRight, BottomLeft, BottomRight };
void PushToast(const char* text, ToastType type = ToastType::Info, float seconds = 3.f);  // from anywhere
void DrawToasts(ToastCorner corner = ToastCorner::BottomRight);                          // once per frame
int ToastCount(); void ClearToasts();
}
```

Keys (`KeybindPicker`, `HotkeyLabel`, `KeyName`) are encoded as `0` for none, `-1..-5` for
Mouse1..Mouse5, and anything `> 0` is an `ImGuiKey` value on ImGui 1.87+ or a native
`io.KeysDown` index on 1.85/1.86 (where names are only known for the legacy `io.KeyMap` keys and
ASCII letters and digits). While capturing, Escape clears the binding and any other key or mouse
button is taken.

`SectionCard` sizes work like ImGui child windows, with the same mapping as ImForge's
[sizing rules](../../docs/SPEC.md#sizing): width `0` fills, height `0` is auto (ImGui 1.90+; on
older versions it fills the remaining height), `-FLT_MIN` fills, other negative values fill
minus that much. With `open` the header toggles it, and children drawn while collapsed are
clipped away. Always call `EndSectionCard()`.

Toggles, buttons, tabs, swatch rings and card headers animate with `imb::Transition` /
`imb::AnimateTo` (`Style::anim_duration`). Toasts slide and fade in and out and show a
remaining-time bar; at most 8 are kept.

## Manifests and codegen

The `widgets/*.imw.json` files are regular [widget manifests](../../docs/SPEC.md#widget-manifests),
with prop, binding and event names matching the C++ signatures above. Colors are passed as
`ImU32` (`IM_COL32`); `Toggle`, `GradientHeader` and `GlowButton` also have `ImVec4` overloads.
A color of `#00000000` means "use the style default".

`stringList` props (`labels`, `icons`) are passed zero-separated, the way `ImGui::Combo` takes
them (`"Aimbot\0Visuals\0Misc\0"`); the overloads above split them, up to 64 entries.
`ToastHost.corner` is an `enum` prop whose placeholder expands to the bare value name
(`imb::ess::ToastCorner::{corner}`). `HotkeyLabel.key` is an `expr` prop, so it can be a literal
key code or a bound variable.

`SectionCard` is a container and uses `callEnd`: codegen and the interpreter run `call`, then
the children, then `callEnd`, always, whatever `returns` says (the same pattern as
`ImGui::EndChild()`). `tools/pack_imblib.py` checks that `callEnd` is a string.

Preview shapes used here are `rect {x,y,w,h,rounding,fill,stroke}`,
`circle {x,y,r,fill,stroke,thickness}` and `text {x,y,text,color}`, with `{prop}` placeholders
substituted in string values.

## Limitations

- `ImGuiKey` names come from `ImGui::GetKeyName`; on 1.85/1.86 unknown native indices print as
  `Key N`.
- `KeyName` returns a pointer to a static buffer for formatted names.
- The zero-separated overloads of `AnimatedTabBar`/`AnimatedSideTabs` take at most 64 entries.
- `SectionCard` auto height needs ImGui 1.90+ (`ImGuiChildFlags_AutoResizeY`).
- `ColorSwatchRow` with its picker popup open leaves the popup as ImGui's "last item", same as
  `ImGui::ColorEdit4`.
