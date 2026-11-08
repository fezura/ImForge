# Tutorial: a cheat/mod-style menu

We'll build a side-tabbed menu with the `essentials` widget pack, bind it to a `Settings`
struct, blur the scene behind it, watch it live inside a DirectX 11 app while editing, and
finally export it as plain C++ that the app compiles without ImForge. It takes about a
quarter of an hour.

Before you start:

* Build the builder (`cmake --preset windows-msvc`, see [Install and build](USER_GUIDE.md#install-and-build)).
* Build the DX11 example with `-DIMFORGE_BUILD_EXAMPLES=ON` (target `imforge_example_dx11_win32`).
  It's the host app for this tutorial.
* Package the essentials pack once: `python tools/pack_imblib.py packs/essentials dist/essentials.imblib`.

## 1. New project from the template

Start the builder. The New Project wizard opens by itself when no project is open (otherwise
File > New Project..., Ctrl+N). On the Project step name it `MyMenu`, pick a location and
click the Cheat / mod menu template card.

The remaining steps are pre-filled from the template: ImGui target `1.91.9` / `master`,
Platform Win32 + DirectX 11, and Features with animations, DrawList effects, shader effects,
icon fonts and DPI scaling turned on. On the Features step, tick `essentials` under the
component libraries (it's listed because `dist/essentials.imblib` sits next to the builder;
press Rescan if you packaged it after starting). Tick Skip for now on Bindings, keep Live
attach on TCP port 7777, leave Code style and the template's theme alone, and press Finish.
The project folder now has `imforge.json`, `panels/Menu.imb.json`, `themes/Dark.theme.json`
and `types.json`.

If you skipped the library in the wizard, use File > Import .imblib... and pick
`dist/essentials.imblib`. Either way the Palette gets an Essentials category with `Toggle`,
`KeybindPicker`, `ColorSwatchRow`, `HotkeyLabel`, `Spinner`, `GradientHeader`, `GlowButton`,
`AnimatedTabBar`, `AnimatedSideTabs`, `SectionCard` and `ToastHost`.

## 2. Describe your data

The template binds to a four-field `Settings`. Swap it for something closer to a real menu.
In your app:

```cpp
struct EspSettings { bool enabled; ImVec4 color; float max_distance; };
struct AimSettings { bool enabled; float fov; float smooth; int key; };
struct Settings    { EspSettings esp; AimSettings aim; int menu_key; bool stream_proof; };

IMFORGE_REFLECT(EspSettings, enabled, color, max_distance);
IMFORGE_REFLECT(AimSettings, enabled, fov, smooth, key);
IMFORGE_REFLECT(Settings, esp, aim, menu_key, stream_proof);
```

Reflect nested structs before the struct that contains them. The builder needs the same
fields in `types.json`. Either run the scanner

```sh
imforge-reflect --include src --header src/settings.h --types Settings \
                --out-json types.json --out-header generated/imforge_reflect.h
```

or edit `types.json` by hand (the format is in [BINDING.md](BINDING.md)):

```json
{ "schemaVersion": 1, "kind": "types",
  "structs": {
    "EspSettings": { "fields": [ { "name": "enabled", "type": "bool", "ctype": "Bool" },
                                 { "name": "color", "type": "ImVec4", "ctype": "Vec4" },
                                 { "name": "max_distance", "type": "float", "ctype": "Float" } ] },
    "AimSettings": { "fields": [ { "name": "enabled", "type": "bool", "ctype": "Bool" },
                                 { "name": "fov", "type": "float", "ctype": "Float" },
                                 { "name": "smooth", "type": "float", "ctype": "Float" },
                                 { "name": "key", "type": "int", "ctype": "I32" } ] },
    "Settings":    { "fields": [ { "name": "esp", "type": "EspSettings", "ctype": "Struct" },
                                 { "name": "aim", "type": "AimSettings", "ctype": "Struct" },
                                 { "name": "menu_key", "type": "int", "ctype": "I32" },
                                 { "name": "stream_proof", "type": "bool", "ctype": "Bool" } ],
                     "header": "settings.h" } },
  "enums": {} }
```

The `Menu` panel already has the parameter `cfg: Settings`, so binding paths are
`cfg.esp.enabled`, `cfg.aim.fov` and so on. We also need somewhere to keep the selected side
tab. Open Project > Panels > Panel Settings..., and under State press + Add state variable:
name `tab`, type `int`, default `0`.

## 3. Layout

Select the `Menu` window in Hierarchy and delete the template's body below the header
(keep `header` and `sep`). Then build the tree below, either by dragging Palette items onto
Hierarchy rows (a frame means into, a line means before/after) or onto the Canvas, or by
right-clicking a row and using Add child / Add sibling:

```
window (Window, title "MyMenu")
├─ header (HStack)              from the template: title text, spacer, Panic button
├─ sep (Separator)
├─ body (HStack, spacing 12)
│  ├─ side (AnimatedSideTabs)   labels: Aimbot, Visuals, Misc
│  │                            icons: fa-crosshairs, fa-eye, fa-gear
│  │                            bind index: state.tab
│  └─ pages (VStack, size w=fill)
│     ├─ page_aim (VStack)      visibleIf: state.tab == 0
│     │  └─ card_aim (SectionCard, title "Aimbot")
│     │     ├─ aim_on   (Toggle, label "Enabled", bind value: cfg.aim.enabled)
│     │     ├─ aim_fov  (SliderFloat, label "FOV", min 1, max 30, format "%.1f", bind value: cfg.aim.fov,
│     │     │            enabledIf: cfg.aim.enabled)
│     │     ├─ aim_smooth (SliderFloat, label "Smooth", min 1, max 20, bind value: cfg.aim.smooth)
│     │     └─ aim_key  (KeybindPicker, label "Aim key", bind key: cfg.aim.key)
│     ├─ page_vis (VStack)      visibleIf: state.tab == 1
│     │  └─ card_esp (SectionCard, title "ESP")
│     │     ├─ esp_on    (Toggle, bind value: cfg.esp.enabled)
│     │     ├─ esp_color (ColorEdit, label "Color", bind value: cfg.esp.color)
│     │     └─ esp_dist  (SliderFloat, label "Max distance", min 50, max 1000, format "%.0f m",
│     │                   bind value: cfg.esp.max_distance, enabledIf: cfg.esp.enabled)
│     └─ page_misc (VStack)     visibleIf: state.tab == 2
│        └─ card_misc (SectionCard, title "Misc")
│           ├─ menu_key (KeybindPicker, label "Menu key", bind key: cfg.menu_key)
│           └─ stream   (Toggle, label "Stream proof", bind value: cfg.stream_proof)
├─ footer (HStack, align Center)
│  ├─ hotkey (HotkeyLabel, text "Toggle menu", key: cfg.menu_key)
│  ├─ footer_spacer (Dummy, size w=fill h=1)
│  └─ btn_save (GlowButton, label "{icon:fa-floppy-disk} Save config", size w=130)
└─ toasts (ToastHost, corner BottomRight)
```

A few things you'll notice while placing them:

* `AnimatedSideTabs` takes `labels` and `icons` as string lists. The icons are Font Awesome 6
  names, and they render once the project font has `mergeIcons: fontawesome6` (the template
  already sets that).
* `visibleIf` and `enabledIf` are typed expressions. The editor marks errors and completes
  paths as you type `state.` or `cfg.`.
* `SectionCard` is a container (its manifest has `callEnd`), so it takes children like a
  `Child` does and sizes its height to fit on ImGui 1.90+.
* Binding slots are type checked: putting `cfg.aim.key` on a `Toggle` is flagged right there,
  and Diagnostics lists every path that doesn't resolve.
* `HotkeyLabel.key` is an expression prop, so both a bound variable (`cfg.menu_key`) and a
  literal key code work.

Once an app is attached (step 6) you can press F5 at any point to see the layout for real.
Until then the Canvas shows the manifest previews of the essentials widgets.

## 4. Blur, shadow, theme

Select `window` and add two effects in the Effects section: `blur` with radius `12`,
rounding `10`, tint `#ffffffe6`, and `shadow` with size `24`, color `#000000a0`. Set the
window's style overrides `WindowRounding` to `10` and `WindowPadding` to `14,14`. The Canvas
only shows a dimmed placeholder for the blur, since the real thing needs the app's back
buffer.

In the Theme Editor press New, start from `ImGui Dark`, rename it `Menu`, and change
`Button`, `ButtonHovered`, `FrameBg`, `CheckMark` and `SliderGrab` to your accent color.
Then in Style Classes add a class `accent` with colors `Button`/`ButtonHovered` and
`FrameRounding = 6`, and give it to `btn_save` under `style.classes`. The essentials widgets
read `imb::ess::GetStyle()`, whose zero fields fall back to the ImGui style, so your theme
colors carry over to the toggles and cards as well.

## 5. Motion and the save handler

Give `card_aim`, `card_esp` and `card_misc` the enter preset `FadeSlideUp`, duration `0.25`,
stagger `0.03`. Each page sits behind a `visibleIf`, so switching side tabs replays the enter
animation of the new page.

On `btn_save` add a transition `prop: scale`, `on: active`, `to: 0.96`, duration `0.08`. The
Anim Timeline's Press scale button does exactly that, and Force `active: on` lets you
preview it.

For `btn_save`'s `onClick` event tick handler. No no-code action is needed, the handler body
will push a toast. After export it's a function you own:

```cpp
static void Menu_btn_save_OnClick(MenuState& state, Settings& cfg) {
    // @imb:user id=btn_save.onClick
    SaveConfig(cfg);
    imb::ess::PushToast("Config saved", imb::ess::ToastType::Success);
    // @imb:end id=btn_save.onClick
}
```

`ToastHost` emits `imb::ess::DrawToasts(imb::ess::ToastCorner::BottomRight)` at the end of
the window, which is what makes the toast show up.

## 6. See it live in the DX11 app

Copy `examples/client/dx11_win32/main.cpp` next to your settings header. It's the stock Dear
ImGui `example_win32_directx11` plus the lines marked `ImForge:`. Swap the demo types and the
demo frame for yours:

```cpp
#define IMFORGE_LIVE                                  // development builds only
#include "imforge/imforge_client.h"
#include "settings.h"                                 // the IMFORGE_REFLECT lines above
#include "imb_essentials.h"                           // packs/essentials

static Settings g_settings;

static void OnSaveLive(imforge::DrawCtx&) {
    imb::ess::PushToast("Config saved", imb::ess::ToastType::Success);
}

// after ImGui_ImplDX11_Init(...):
imforge::live::Connect("tcp://127.0.0.1:7777");
imforge::live::SetFontTextureRebuild(ImGui_ImplDX11_InvalidateDeviceObjects);   // project fonts
imforge::live::RegisterHandler("Menu.btn_save.onClick", &OnSaveLive);
imforge::live::RegisterWidget("Toggle", &imb::ess::DrawToggleInterp);   // one per essentials widget you use,
                                                                        // or include the pack's registration header

// every frame, before ImGui_ImplDX11_NewFrame():
imforge::live::PreNewFrame();

// every frame, after ImGui::NewFrame():
imforge::live::NewFrame();
imforge::live::Draw("Menu", { imforge::live::Bind("cfg", g_settings) });
```

Keep the `imb::fx::Dx11InitDesc` / `imb::fx::PrepareFrame(fd)` lines from the example, they
give the live menu its blur. In the builder press Start in the Live panel (transport `tcp`,
port `7777`) and run the app. Pairing happens on its own, and the Live panel then reads
`Client connected: app pid ..., ImGui 1.91.9, dx11_win32/win32`. From now on every edit in
the builder reaches the app within 150 ms. Press Ctrl+Shift+P and click something in the app
to select that widget in the builder.

If a card shows a placeholder box instead of a `Toggle`, the app hasn't registered that
widget type. The Live panel's Troubleshooting tab names it, and the Stats tab lists the
widgets the app did register.

## 7. Export and wire it up

Press Ctrl+E and then Export. With the template's codegen settings (`namespace menu`, split
header, snake case variables) you get `generated/Menu.h`, `generated/Menu.cpp`,
`generated/imforge_styles.h`, `generated/imforge_theme_Menu.h`, `generated/imforge_fonts.h`,
`generated/imforge_reflect.h` and `generated/imforge_dev.h`. Open `Menu.cpp`, find the
`@imb:user id=btn_save.onClick` region and paste in the handler body from step 5. Whatever
`#include` lines you need (for `SaveConfig`) go inside `@imb:user id=includes`.

In the app:

```cpp
#include "generated/Menu.h"
#include "generated/imforge_theme_Menu.h"
#include "generated/imforge_fonts.h"

static menu::MenuState g_menu;                        // owns `open` and `tab`

// init: after ImGui::CreateContext()
menu::LoadFonts(ImGui::GetIO());
menu::ApplyTheme_Menu(ImGui::GetStyle());

// frame
if (ImGui::IsKeyPressed((ImGuiKey)g_settings.menu_key)) g_menu.open = !g_menu.open;
if (g_menu.open) menu::DrawMenu(g_menu, g_settings);
```

`KeybindPicker` stores an `ImGuiKey` for keyboard keys (ImGui 1.87+) and `-1..-5` for mouse
buttons, so guard that cast if you allow mouse bindings. The blur still needs the renderer
hook from the example, unchanged:

```cpp
// once, after ImGui_ImplDX11_Init
imb::fx::Dx11InitDesc d; d.device = g_pd3dDevice; d.context = g_pd3dDeviceContext;
imb::fx::Init(d);

// each frame: render your scene, then
imb::fx::FrameDesc fd; fd.back_buffer_rtv = g_mainRenderTargetView;
imb::fx::PrepareFrame(fd);
ImGui::Render();
ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
```

Generated code only calls `imb::fx::Blur` when the included backend header defines
`IMB_FX_AVAILABLE`; without it the window just has no blur. For a release build remove
`#define IMFORGE_LIVE` (or limit it to Debug like the Live panel's CMake snippet does). The
`imforge::live::*` lines then compile to nothing and `imforge_dev.h` isn't included anymore.

You need `include/` (runtime and client headers), `packs/essentials/` and your project's
`generated/` on the include path. Copy client snippet in the Live panel gives you the C++ and
a CMake fragment for this exact project.

## Where to go next

Keep editing and re-exporting. Handler bodies and anything inside `@imb:user` regions are
kept, literal edits you made to generated lines are read back, and the rest shows up in the
Conflicts panel ([CODEGEN.md](CODEGEN.md)).

Try turning `card_aim` into a component with a `title` parameter and a `content` slot
([COMPONENTS.md](COMPONENTS.md)) and sharing it as an `.imblib`.

The `CheatMenu` sample in `examples/projects/CheatMenu` is the full-size version of this
menu, with an overlay HUD panel, a `Code` node, popups and the `copyClipboard` action.
