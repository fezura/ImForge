# Animations and effects

Dear ImGui has no animation system, so ImForge ships a small one: `imb_runtime.h`, a single
header that needs nothing but ImGui. Generated code only includes it when a panel uses motion
or effects, otherwise the output stays plain ImGui. The interpreter uses the same header, so
what you see in the builder and in the attached app is what ships.

## Transitions

Any animatable property can move between two states. The state (`on`) is one of:

| `on` | Driven by |
|---|---|
| `hover` | item hovered |
| `active` | item held / active |
| `open` | popup, tab, tree node or collapsing header open |
| `visible` | the node's `visibleIf` result |
| `disabled` | the node's `enabledIf` result |

Animatable `prop` values are `color.<ImGuiCol>` (e.g. `color.Button`), `alpha`, `offset.x`,
`offset.y`, `scale`, `rounding`, `border`, `size.w` and `size.h`.

```json
"anim": { "transitions": [
  { "prop": "color.Button", "on": "hover",  "to": "#27ae60ff", "duration": 0.15, "ease": "OutCubic" },
  { "prop": "offset.y",     "on": "active", "to": 1,           "duration": 0.05 },
  { "prop": "scale",        "on": "hover",  "to": 1.03, "ease": { "spring": { "stiffness": 220, "damping": 18 } } }
] }
```

Easings: `Linear`, `InQuad`, `OutQuad`, `InOutQuad`, `InCubic`, `OutCubic`, `InOutCubic`,
`InBack`, `OutBack`, `OutElastic`, `OutBounce`, `OutExpo`, or a spring with `stiffness` and
`damping`. You set them in the properties panel, and the timeline panel scrubs and previews
them.

Generated code looks like this:

```cpp
float t = imb::Transition(ImGui::GetID("btn_play"), hovered, 0.15f, imb::Ease::OutCubic);
ImGui::PushStyleColor(ImGuiCol_Button, imb::Lerp(base, ImVec4(0.15f, 0.68f, 0.38f, 1), t));
```

`hovered` is the previous frame's item state, stored by id. You can't see the one-frame lag.

## Enter and exit presets

```json
"anim": { "enter": { "preset": "FadeSlideUp", "duration": 0.25, "stagger": 0.03 },
          "exit":  { "preset": "Fade",        "duration": 0.15 } }
```

Presets: `Fade`, `SlideUp`, `SlideDown`, `SlideLeft`, `SlideRight`, `FadeSlideUp`,
`FadeSlideDown`, `ScalePop` and `Stagger` (children appear one after another, `stagger` seconds
apart). An exit animation keeps the node drawn until it finishes; it starts when `visibleIf`
turns false. Tab content cross-fades with `TabBar.anim.crossfade`.

```cpp
if (imb::BeginEnter(id, visible, 0.25f, imb::Preset::FadeSlideUp)) {
    ...
    imb::EndEnter();
}
```

## DrawList effects

These work on every backend because they're drawn with `ImDrawList`. You add them per node in
`fx`:

| `type` | Parameters | Notes |
|---|---|---|
| `gradient` | `from`, `to`, `vertical`, `rounding` | background gradient behind the item or window |
| `radialGradient` | `inner`, `outer`, `radius` | |
| `shadow` | `color`, `size`, `offset`, `rounding` | soft drop shadow |
| `glow` | `color`, `size` | outer glow |
| `border` | `style` (`Solid`, `Rainbow`, `Sweep`, `Pulse`), `thickness`, `color`, `speed` | animated outline |
| `particles` | `count`, `speed`, `size`, `color`, `connect`, `connectDist` | background particles, optionally connected |
| `noise` | `color`, `density` | static grain |
| `cursorHighlight` | `color`, `radius` | radial highlight following the mouse |
| `blur` | `radius`, `rounding`, `tint`, `saturation`, `brightness`, `noise` | shader blur, see below |

Effects run in the order listed. Backgrounds are drawn before the item, borders and glow after
it.

## Shader blur

Blurring what's behind a window needs the rendered scene, which `ImDrawList` can't read. So blur
is implemented per renderer, in `include/imforge/fx/` (namespace `imb::fx`):

| Header | Init |
|---|---|
| `imb_fx_dx9.h` | `imb::fx::Init({ device })` |
| `imb_fx_dx11.h` | `imb::fx::Init({ device, context })` |
| `imb_fx_dx12.h` | `imb::fx::Init({ device, queue, srv_heap, rtv_format, frames_in_flight })` |
| `imb_fx_gl3.h` | `imb::fx::Init({ getProcAddress })` |
| `imb_fx_vulkan.h` | `imb::fx::Init({ instance, physical_device, device, queue, render_pass, image_count })` |

The frame looks the same on every backend:

```
render your scene
imb::fx::PrepareFrame(frame)        // copies the back buffer, builds a blurred mip chain
ImGui::Render();
ImGui_ImplXXX_RenderDrawData(...)   // Blur() draw callbacks sample the prepared texture
```

Because the capture happens before ImGui draws, the blur shows your scene behind the window and
not other ImGui windows underneath. For a menu over a game that's what you want anyway. Rounded
corners are clipped in the pixel shader. If no backend header is included, generated code
compiles without blur (`IMB_FX_AVAILABLE` stays undefined) and the builder marks the effect as
unavailable for the project's renderer.

If you're writing a mod or overlay you already have the device; call `Init` once with it. DX12
and Vulkan also need the frame's command list or render pass info in `PrepareFrame`. The
examples in `examples/client` and `examples/fx` show where each call goes.

## Cost

Animation state sits in a small table keyed by `ImGuiID` and is dropped after 2 s without use.
A few hundred animated items cost microseconds per frame. Particles keep simulation state per
id and don't allocate per frame, but keep `count` modest in overlays. The blur is two or three
full-screen passes at reduced resolution, and it's skipped on frames where no `Blur` call was
recorded.
