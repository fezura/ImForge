# display pack

Read-mostly Dear ImGui widgets (namespace `imb::disp`): progress rings, gauges, small charts,
badges, avatars, stat cards, toasts and the like. Everything is drawn with `ImDrawList`, sized from
the font (so DPI scaling just works) and colored from the current ImGui style unless you pass a
color. Each widget submits a normal item (`Dummy` or `InvisibleButton`), so `IsItemHovered`,
tooltips, `SameLine` and `BeginDisabled` behave as usual. Dear ImGui 1.85 and later, master and
docking.

```
packs/display/
  imb_display.h                 umbrella header, include this one
  imb_disp_core.h               Style/GetStyle, Status, helpers
  imb_disp_progress.h           RingProgress, RadialGauge, LoadingBar, SkeletonBlock
  imb_disp_charts.h             Sparkline, MiniBars, FrameGraph, FrameSamples
  imb_disp_cards.h              StatCard, EmptyState, TooltipCard
  imb_disp_badges.h             Badge, BadgeCount, BadgeDot, Avatar, StatusDot, Kbd, Divider
  imb_disp_nav.h                ProgressSteps, TimelineScrubber, Breadcrumbs, KeyOverlay
  imb_disp_toast.h              PushToast / ToastStack
  imforge_adapters_display.h    interpreter glue for the builder and live client
  widgets/*.imw.json            one manifest per widget
  manifest.json
```

The headers include `"imforge/imb_runtime.h"`, so put the repo's `include/` on the include path.
If `imforge/imb_motion.h` is there too and the motion presets are registered (include
`imb_motion_presets.h` somewhere), toasts slide with those presets instead of the built-in slide.

## Quick tour

```cpp
#include "imb_display.h"
namespace disp = imb::disp;

disp::RingProgress("Download", job.progress);          // < 0 spins
disp::RadialGauge("km/h", speed, 0, 240, 160, 210);    // green, yellow above 160, red above 210
disp::Sparkline("##ping", ping_history.data(), (int)ping_history.size());
disp::FrameGraph("Frame time");                        // records io.DeltaTime on its own

disp::StatCardStyle cs;
cs.show_delta = true;
cs.delta = 12.5f;
disp::StatCard("Revenue", revenue, cs);                // counts up the first time it shows

ImGui::Button("Inbox");
disp::BadgeStyle on_corner;
on_corner.attach = true;
disp::BadgeCount("##unread", unread, on_corner);       // bumps when the number changes

if (disp::Breadcrumbs("##path", "Home\0Configs\0Legit\0", &clicked)) GoTo(clicked);
disp::TimelineScrubber("##replay", &replay_time, replay_length, markers, marker_count);

disp::PushToast("Config saved", "legit.json", disp::ToastKind::Success);
disp::ToastStack();                                    // once per frame, anywhere
```

Values that change (ring, gauge needle, bars, stat card numbers, loading bar) glide to the new
value with `imb::AnimateTo` / `imb::Spring`, and sweep in from zero the first time a widget is
seen. Turn that off with `imb::disp::GetStyle().intro = false`. Durations and spring constants are
in `Style` too, next to the colors. A color of 0 always means "use the ImGui style".

Most widgets take an optional style struct as the last argument (`RingStyle`, `GaugeStyle`,
`SparklineStyle`, `MiniBarsStyle`, `FrameGraphStyle`, `LoadingBarStyle`, `SkeletonStyle`,
`BadgeStyle`, `AvatarStyle`, `StatusDotStyle`, `StatCardStyle`, `StepsStyle`, `ScrubberStyle`,
`BreadcrumbStyle`, `KeyOverlayStyle`, `DividerStyle`, `KbdStyle`, `EmptyStateStyle`,
`TooltipCardStyle`, `ToastStackStyle`). There's also a flat overload per widget with plain
arguments; that's the one the manifests' `call` templates use.

## Notes

`FrameGraph` takes a `FrameSamples` ring buffer you push into, a plain `float` array (oldest
first, with an optional ring offset like `ImGui::PlotLines`), or nothing: `FrameGraph(label)`
keeps its own history (8 graphs at most) and samples `io.DeltaTime` unless you fed it with
`FrameGraphPush(label, ms)` that frame. Stats show the average, fps and the 1% low.

`TooltipCard(title, text)` goes right after the item it describes and shows after a short hover
delay. For anything fancier use `BeginTooltipCard` / `EndTooltipCard` and put regular ImGui items
in between. `TooltipCardInline` draws the same card in the layout, which is what the builder
canvas shows.

`KeyOverlay` reads `ImGuiKey_W/A/S/D/Space` and the mouse buttons on 1.87+. On 1.85/1.86 it
looks at `io.KeysDown['W']` and friends, which matches the Win32 and GLFW backends. Pass
`KeyOverlayStyle::state` to drive it from recorded input instead.

Toasts live in a fixed list of 16, the oldest is dropped first. Hovering one pauses its timer.
A progress toast (`SetToastProgress`) stays until it reaches 1. The close button reacts to
clicks on the foreground draw list, so the click also reaches whatever window is underneath.

`StatusDot` without a label keys its color transition by position in the window; give it a
`##id` label if you move it around.

The manifests follow the usual [widget manifest](../../docs/WIDGET_SDK.md) format. Array
bindings (`values`, `samples`, `spark`, `markers`) accept `float[N]` and `std::vector<float>`;
when generated code leaves one unbound the widget still compiles and draws empty, and
`FrameGraph` falls back to live frame times.
