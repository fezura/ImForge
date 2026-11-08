# Motions

A motion is a keyframe animation attached to a widget: it fades, moves, scales, rotates,
skews, tints or clips whatever the widget drew that frame. ImForge ships 212 of them as
presets, and you can make your own in the Keyframes tab of the Anim Timeline. Everything
runs on `include/imforge/imb_motion.h`, a header that needs only Dear ImGui and
`imb_runtime.h`, so the builder canvas, the live app and the exported code all play the same
thing.

The older hover/press transitions, enter/exit presets and effects still work and are described
in [ANIMATIONS.md](ANIMATIONS.md). You can use both on one node. To see a lot of the presets at
once, open `examples/projects/Showcase` in the builder; its three panels run more than a
hundred of them on the pack widgets.

## Adding one in the builder

Pick a node, open the Motions section in Properties and press Add motion. The picker has a
search box, category tabs and a small preview of each animation. Or open the Library
(Ctrl+L), go to Animations and drag a card onto a node in the Canvas or Hierarchy. A node can
have up to 8 motions; extra ones are ignored. In the project file they live under `anim`:

```json
"anim": { "motions": [
    { "preset": "ZoomIn" },
    { "preset": "Lift", "trigger": "hover", "durationScale": 0.5 },
    { "preset": "HeadShake", "trigger": "attention", "on": "onClick" },
    { "custom": "MyWiggle", "trigger": "attention", "on": "onChange" }
] }
```

`preset` names a built-in animation, `custom` one of the project's own. The other fields are
`trigger` (default `enter`), `durationScale`, `delay`, `iterations`, `stagger` and `on`.

## Triggers

| Trigger | Plays |
|---|---|
| `enter` | when the widget first shows up, and again each time it comes back |
| `exit` | when `visibleIf` turns false; the widget stays drawn until the motion ends |
| `hover` | forward while hovered, backward when the mouse leaves |
| `press` | forward while held down, backward on release |
| `focus` | forward while the widget is focused or active |
| `loop` | all the time, for as long as the widget is shown |
| `change` | restarts when its `on` event fires (or `TriggerMotion` is called) |
| `attention` | same as `change`, meant for "look here" effects |

Hover, press and focus hold the end pose while the state is on, so a hover preset like `Lift`
ends at the lifted position, not back at rest. If a node has no exit motion it disappears the
moment `visibleIf` turns false, as before. Focus is checked on the group around the widget,
which ImGui only reports while something inside it is active, so plain keyboard navigation
onto a child doesn't count.

## Timing

Timing works like CSS animations. Each animation has a duration, a delay, an iteration count
(0 means forever), a direction (`normal`, `reverse`, `alternate`, `alternate-reverse`) and a
fill mode (`none`, `forwards`, `backwards`, `both`). A motion on a node can change some of
that without touching the animation:

- `durationScale` stretches the active part, so 2 plays half as fast. The delay is not scaled.
- `delay` is added to the animation's own delay.
- `iterations` overrides the count; -1 keeps the animation's own, 0 repeats forever. A `loop`
  motion with -1 always repeats forever.

Time comes from `io.DeltaTime`. Call `imb::NewFrame(dt)` after `ImGui::NewFrame()` if you
want to drive it yourself, for example to slow everything down while debugging.

## What gets animated

A track animates one property: `opacity`, `translateX`, `translateY`, `scale`, `scaleX`,
`scaleY`, `rotate`, `skewX`, `skewY`, `tint`, `reveal`, `textReveal`, `glow` or `shadow`.
Rotate and skew are in degrees, translate in pixels at 100% (scaled by `FontSize / 13`), and
the origin for scale, rotate and skew is a fraction of the widget rectangle (0.5, 0.5 is the
center). `tint` multiplies vertex colors. `reveal` clips the widget to a rectangle that grows
from the edge set by `revealDir`. `glow` and `shadow` draw behind the widget, using
`MotionOptions::glow_color` (the theme's `CheckMark` color by default) and a translucent
black.

When several motions play at once their poses are combined: opacity, scale and tint multiply,
translate, rotate and skew add up, the smallest reveal wins and the biggest glow or shadow
wins.

The transform happens after the widget has drawn: `EndMotion` moves the vertices it added to
the window's draw list. That has a few consequences worth knowing:

- It's visual only. Hit testing and layout use the untransformed position, so a button that
  slides in can be clicked where it will end up.
- Clip rectangles aren't transformed, so a big scale can get cut off at the window edge.
- Child windows opened inside a motion draw to their own draw list and don't move. That
  includes a `Child` node's own content: a motion on the `Child` only moves what the parent
  window draws for it (its glow or shadow), so put entrances on a stack inside the child.
- A motion that starts in one table cell and ends in another (a draw list channel switch)
  isn't transformed.
- On a `Window`, `Popup`, `Modal`, `Tab` or `Menu` the motion animates the content, not the
  window frame or title bar. Motions on a `MenuBar` are not played.

## Stagger

Give a container's motion a `stagger` (in seconds) and it plays on each direct child instead
of the container, each one `stagger` later than the one before. That's how list rows cascade
in. The stagger presets (`StaggerFadeUp`, `StaggerPop`, ...) are short entrances tuned for
this, but any entrance works. Applying a design puts its list motion on `VStack`, `ListBox`,
`Child` and `Grid` nodes with at least two children and uses a stagger of 0.05 s if the
design doesn't give one.

In your own code, set `MotionBinding::stagger` and pass the child's index in
`MotionOptions::stagger_index`.

## Text reveal

Text presets such as `Typewriter`, `TypewriterSmooth` and `TextRiseIn` animate `textReveal`,
the fraction of characters shown. `Text` and `SeparatorText` nodes honor it, in the builder
and in exported code. The cut counts UTF-8 characters, so icons and accented letters don't
break. For your own widgets, read the value with `imb::MotionTextReveal(id)` inside the scope
and draw that much of the string.

## Custom animations

Custom animations belong to the project and are stored as
`animations/<Name>.imanim.json`. The easiest way to make one is in the builder. New
animation (in the Library or the Keyframes tab) or New custom animation (in the motion picker)
starts you off with a small fade and rise. From preset in the Keyframes tab, Duplicate as
custom in the Library, or Edit keyframes on a preset copies the built-in animation into the
project so you can change it.
The [user guide](USER_GUIDE.md#keyframes) walks through the editor.

The file format is the same one the presets use:

```json
{ "schemaVersion": 1, "kind": "animation", "name": "MyWiggle", "category": "attention",
  "description": "Small wiggle for a changed value.", "duration": 0.5, "delay": 0,
  "iterations": 2, "direction": "alternate", "fill": "both",
  "origin": { "x": 0.5, "y": 1 }, "revealDir": "left", "trigger": "attention", "ease": "InOutSine",
  "tracks": {
    "rotate": [ { "t": 0, "v": 0 }, { "t": 0.5, "v": 8, "ease": "cubic-bezier(0.3,0,0.7,1)" }, { "t": 1, "v": 0 } ],
    "scale":  [ { "t": 0, "v": 1, "ease": "spring(170,26)" }, { "t": 1, "v": 1.1 } ],
    "tint":   [ { "t": 0, "v": "#ffffffff" }, { "t": 1, "v": "#ff8080ff" } ]
  } }
```

Each keyframe has a time `t` from 0 to 1 and a value `v` (a hex color for `tint`). An `ease`
on a keyframe applies to the segment that starts there; segments without one use the
animation's `ease`. Easings are any of the named curves (`Linear`, `InQuad`, `OutQuad`,
`InOutQuad`, the Cubic, Quart, Quint, Sine, Expo, Circ, Back, Elastic and Bounce families in
`In`, `Out` and `InOut` versions), `cubic-bezier(x1,y1,x2,y2)` with CSS semantics,
`steps(n)`, or `spring(stiffness,damping)`.

A few rules keep animations from leaving widgets in odd states: every track starts at `t` 0
and ends at `t` 1, entrances end at the resting pose and exits start from it, hover and press
start at rest, and loops end where they start (rotation may differ by whole turns). Scale is
limited to 0 to 3, rotation to plus or minus 720 degrees, skew to 85 degrees and a track to 64
keyframes. The Keyframes tab tells you when one of these is broken, and
`schemas/animation.schema.json` describes the file.

Custom animations are sent to the live app with the project, so editing keyframes updates the
running app too. A custom animation with the same name as a preset replaces it in that project.

## Using motions in your own code

You don't need the builder for any of this. Include the two headers and wrap a widget in
`BeginMotion`/`EndMotion`:

```cpp
#include "imb_motion.h"
#include "imb_motion_presets.h"   // imb::presets::<Name>() for all 212 presets

void DrawToolbar(bool show_hint) {
    static const imb::MotionBinding play[] = {
        {imb::presets::ZoomIn(), imb::Trigger::Enter},
        {imb::presets::Lift(), imb::Trigger::Hover, 0.5f},
        {imb::presets::HeadShake(), imb::Trigger::Attention},
    };
    const ImGuiID play_id = ImGui::GetID("play##m");
    if (imb::BeginMotion(play_id, play, 3)) {
        if (ImGui::Button("Play")) imb::TriggerMotion(play_id, imb::Trigger::Attention);
        imb::EndMotion();
    }

    static const imb::MotionBinding hint[] = {
        {imb::presets::FadeInUp(), imb::Trigger::Enter},
        {imb::presets::FadeOutDown(), imb::Trigger::Exit},
    };
    imb::MotionOptions opt;
    opt.visible = show_hint;   // keep calling BeginMotion while hidden so the exit can play
    if (imb::BeginMotion(ImGui::GetID("hint##m"), hint, 2, opt)) {
        ImGui::TextUnformatted("Press F1 for help");
        imb::EndMotion();
    }
}
```

`BeginMotion` wraps a `BeginGroup`. It returns false once the widget is hidden and its exit
motion is done; then skip drawing and don't call `EndMotion`. Scopes nest, up to 32 deep. The
fields of `MotionBinding` are, in order, the animation, the trigger, `duration_scale`,
`delay`, `iterations` and `stagger`. `MotionOptions` also has `force_hover` and `force_press`
(the builder uses them to preview hover and press motions).

Other calls you might want:

```cpp
const imb::AnimDef* a = imb::FindAnim("Pulse");   // look up by name, nullptr if unknown
imb::RegisterAnim(&my_def);                        // your own AnimDef, replaces the same name
imb::UnregisterAnim("MyWiggle");
bool busy = imb::IsMotionPlaying(play_id);
imb::ResetMotions();                               // everything replays its enter motion
```

The registry only stores pointers, so an `AnimDef` you register has to outlive its use. When
`imb_motion_presets.h` is included, the presets register themselves on the first registry
call; a custom animation you registered before that keeps its name.

If you made the animation in the builder, export the project and use the generated
`imforge_animations.h`: it has the keyframe tables, an accessor per animation
(`imforge_anim::MyWiggle()`) and `imforge_anim::RegisterAnimations()`.

## Exported code

When a panel uses motions, export adds `#include "imb_motion.h"` and
`#include "imb_motion_presets.h"` and copies both headers next to `imb_runtime.h`. Each node
with motions gets a static `imb::MotionBinding` table and a `BeginMotion`/`EndMotion` scope,
`visibleIf` feeds `MotionOptions::visible` instead of an `if` so exits can play, `on` events
call `imb::TriggerMotion`, and text reveal goes through a small `ImbTextReveal` helper.
Turning off the animations feature in Project Settings keeps the motions in the project but
leaves them out of the code. More in [CODEGEN.md](CODEGEN.md#motions).

## Presets

All 212 built-in animations, by category. The names marked \* are ports of animate.css 4.1.0
keyframes (MIT, see [THIRD_PARTY.md](../THIRD_PARTY.md)). The source is
`presets/animations/*.imanims.json`; `tools/gen_motion_presets.py` validates it and writes
`include/imforge/imb_motion_presets.h`, and `--check` tells you when the header is out of date.

### Entrance (57)

| Name | Description |
|---|---|
| `BackInDown` \* | Drops in at reduced size, then scales up to full. |
| `BackInLeft` \* | Slides in from the left small, then scales up to full. |
| `BackInRight` \* | Slides in from the right small, then scales up to full. |
| `BackInUp` \* | Rises at reduced size, then scales up to full. |
| `BlurIn` | Comes into focus out of a soft glow. |
| `BounceIn` \* | Pops in with a springy overshoot. |
| `BounceInDown` \* | Bounces down from above and settles. |
| `BounceInLeft` \* | Bounces in from the left and settles. |
| `BounceInRight` \* | Bounces in from the right and settles. |
| `BounceInUp` \* | Bounces up from below and settles. |
| `ClipRevealCenter` | Opens out from the middle with a clip wipe. |
| `ClipRevealDown` | Wipes in starting at the bottom edge. |
| `ClipRevealLeft` | Wipes in starting at the left edge. |
| `ClipRevealRight` | Wipes in starting at the right edge. |
| `ClipRevealUp` | Wipes in starting at the top edge. |
| `DropIn` | Drops in from above and bounces to rest. |
| `FadeIn` \* | Fades in from fully transparent. |
| `FadeInBottomLeft` \* | Fades in diagonally from the bottom left. |
| `FadeInBottomRight` \* | Fades in diagonally from the bottom right. |
| `FadeInDown` \* | Fades in while dropping into place. |
| `FadeInDownBig` \* | Fades in on a long drop from well above. |
| `FadeInLeft` \* | Fades in while sliding over from the left. |
| `FadeInLeftBig` \* | Fades in on a long slide from far left. |
| `FadeInRight` \* | Fades in while sliding over from the right. |
| `FadeInRightBig` \* | Fades in on a long slide from far right. |
| `FadeInTopLeft` \* | Fades in diagonally from the top left. |
| `FadeInTopRight` \* | Fades in diagonally from the top right. |
| `FadeInUp` \* | Fades in while rising into place. |
| `FadeInUpBig` \* | Fades in on a long rise from well below. |
| `FlipInX` \* | Flips in around the horizontal axis. |
| `FlipInY` \* | Flips in around the vertical axis. |
| `GlowIn` | Fades in lit up, then the glow dies down. |
| `JackInTheBox` \* | Springs up from its bottom edge with a little wobble. |
| `LightSpeedInLeft` \* | Streaks in from the left and wobbles straight. |
| `LightSpeedInRight` \* | Streaks in from the right and wobbles straight. |
| `PopIn` | Pops in on a quick spring. |
| `PuffIn` | Shrinks down from double size while fading in. |
| `RollIn` \* | Rolls in from the left like a wheel. |
| `RotateIn` \* | Spins into place while fading in. |
| `RotateInDownLeft` \* | Swings down into place around its bottom left corner. |
| `RotateInDownRight` \* | Swings down into place around its bottom right corner. |
| `RotateInUpLeft` \* | Swings up into place around its bottom left corner. |
| `RotateInUpRight` \* | Swings up into place around its bottom right corner. |
| `ScaleUpCenter` | Scales up from half size around the center. |
| `SlideInDown` \* | Slides down into place with no fade. |
| `SlideInLeft` \* | Slides in from the left with no fade. |
| `SlideInRight` \* | Slides in from the right with no fade. |
| `SlideInUp` \* | Slides up into place with no fade. |
| `SwingIn` | Swings in from its top edge like a hanging sign. |
| `TiltIn` | Rises in with a slight tilt that straightens out. |
| `Unfold` | Opens as a thin line that grows tall, then wide. |
| `ZoomIn` \* | Grows from a small size while fading in. |
| `ZoomInDown` \* | Zooms down from above and overshoots a touch. |
| `ZoomInLeft` \* | Zooms in from the left and overshoots a touch. |
| `ZoomInRight` \* | Zooms in from the right and overshoots a touch. |
| `ZoomInSoft` | A gentle zoom from just under full size. |
| `ZoomInUp` \* | Zooms up from below and overshoots a touch. |

### Exit (44)

| Name | Description |
|---|---|
| `BackOutDown` \* | Shrinks back, then slides out downward. |
| `BackOutLeft` \* | Shrinks back, then slides out to the left. |
| `BackOutRight` \* | Shrinks back, then slides out to the right. |
| `BackOutUp` \* | Shrinks back, then slides out upward. |
| `BounceOut` \* | Squeezes, swells once, then shrinks away. |
| `BounceOutDown` \* | Hops, crouches, then shoots downward. |
| `BounceOutLeft` \* | Leans back, then shoots off to the left. |
| `BounceOutRight` \* | Leans back, then shoots off to the right. |
| `BounceOutUp` \* | Hops, crouches, then shoots upward. |
| `ClipHideCenter` | Closes in from both sides to the middle. |
| `ClipHideDown` | Wipes away toward the bottom edge. |
| `ClipHideLeft` | Wipes away toward the left edge. |
| `ClipHideRight` | Wipes away toward the right edge. |
| `ClipHideUp` | Wipes away toward the top edge. |
| `Collapse` | Squashes up into its top edge. |
| `FadeOut` \* | Fades out in place. |
| `FadeOutDown` \* | Fades out while drifting down a little. |
| `FadeOutDownBig` \* | Fades out and travels a long way down. |
| `FadeOutLeft` \* | Fades out while drifting left a little. |
| `FadeOutLeftBig` \* | Fades out and travels a long way left. |
| `FadeOutRight` \* | Fades out while drifting right a little. |
| `FadeOutRightBig` \* | Fades out and travels a long way right. |
| `FadeOutUp` \* | Fades out while drifting up a little. |
| `FadeOutUpBig` \* | Fades out and travels a long way up. |
| `FlipOutX` | Folds shut on the horizontal axis like a card turning away. |
| `FlipOutY` | Folds shut on the vertical axis like a card turning away. |
| `GlowOut` | Flares up with a glow, then fades out. |
| `Hinge` \* | Swings loose from its top left corner, then drops. |
| `LightSpeedOutLeft` \* | Skews and zips off to the left. |
| `LightSpeedOutRight` \* | Skews and zips off to the right. |
| `PopOut` | Swells for a beat, then pops out of sight. |
| `PuffOut` | Grows and fades like a puff of smoke. |
| `RollOut` \* | Rolls off to the right like a wheel. |
| `RotateOut` \* | Spins away around its center as it fades. |
| `ShrinkAway` | Shrinks a touch and fades, good for dismissing cards. |
| `SlideOutDown` \* | Slides downward and fades only at the very end. |
| `SlideOutLeft` \* | Slides to the left and fades only at the very end. |
| `SlideOutRight` \* | Slides to the right and fades only at the very end. |
| `SlideOutUp` \* | Slides upward and fades only at the very end. |
| `ZoomOut` \* | Shrinks toward the center and fades away. |
| `ZoomOutDown` \* | Pulls back a bit, then shrinks and flies down. |
| `ZoomOutLeft` \* | Pulls back a bit, then shrinks and flies off to the left. |
| `ZoomOutRight` \* | Pulls back a bit, then shrinks and flies off to the right. |
| `ZoomOutUp` \* | Pulls back a bit, then shrinks and flies up. |

### Attention (26)

| Name | Description |
|---|---|
| `Alert` | Flashes a warm orange tint twice. |
| `Beckon` | Leans forward and back as if waving you over. |
| `Blink` | Two hard blinks, no fading. |
| `Bob` | Floats up and down once, then rests. |
| `Bounce` \* | Hops up twice and lands with a small squash. |
| `Buzz` | Fast tiny jitter, like a phone on vibrate. |
| `Flash` \* | Fades out and back in twice. |
| `Glimmer` | Dims for a moment and comes back with a glow. |
| `HeadShake` \* | A short no-no shake that dies out fast. |
| `HeartBeat` \* | Two quick beats, like a heart. |
| `Jello` \* | Wobbly skew that settles like jelly. |
| `Nudge` | Pokes to the right twice, like it wants a click. |
| `Ping` | Pops out with a soft glow that fades. |
| `Pulse` \* | Swells a little and settles back. |
| `RubberBand` \* | Stretches wide, then snaps back with a few wobbles. |
| `ShakeX` \* | Shakes side to side, good for a rejected action. |
| `ShakeY` \* | Shakes up and down a few times. |
| `Shimmy` | Shuffles side to side with a lean. |
| `Spotlight` | A glow swells behind the item and fades away. |
| `Swing` \* | Swings from the top edge like a hanging sign. |
| `Tada` \* | Shrinks, then grows with a little rattle. |
| `Throb` | Slow deep swell with a shadow that grows and shrinks. |
| `Twist` | Twists one way, snaps back past center and settles. |
| `Vibrate` | Rattles in place on both axes. |
| `Wiggle` | A quick little left and right rotation. |
| `Wobble` \* | Rocks sideways with a slight tilt, losing energy each swing. |

### Hover (16)

| Name | Description |
|---|---|
| `Brighten` | Adds a faint glow and a hint of scale so the item looks lit. |
| `GlowUp` | Brings up the accent glow and lifts by a pixel. |
| `Grow` | Scales up a little while the cursor is over it. |
| `HoverFloat` | Floats up two pixels over a light shadow. |
| `HoverShrink` | Pulls in a touch, nice for icons in a tight toolbar. |
| `Lift` | Rises a few pixels and drops a soft shadow under it. |
| `NudgeRight` | Slides three pixels to the right, good for links and next buttons. |
| `PopHover` | Springs up to a slightly bigger size with a small bounce. |
| `PressReady` | Settles down a little, as if waiting for the click. |
| `Raise` | Lifts and grows at the same time, for cards that should feel picked up. |
| `Slant` | Leans forward with a slight skew. |
| `SpotlightHover` | Turns on a strong accent glow behind the item. |
| `TiltLeft` | Tilts three degrees counterclockwise. |
| `TiltRight` | Tilts three degrees clockwise. |
| `Widen` | Stretches a bit wider, which suits pill buttons. |
| `WobbleReady` | Tips two degrees with a small overshoot. |

### Press (12)

| Name | Description |
|---|---|
| `ClickBounce` | Dips past the pressed size, then eases back a little. |
| `Dim` | Fades to 80 percent while held. |
| `Flatten` | Squashes down toward the bottom edge. |
| `GlowPress` | Lights up with a glow while held. |
| `PressDown` | Moves down a pixel and shrinks slightly. |
| `PushIn` | Shrinks and darkens a touch, as if pushed into the surface. |
| `Recess` | Darkens and sinks a little, like a pressed key. |
| `Shrink` | Shrinks to 90 percent. |
| `Sink` | Drops two pixels with no shadow, like a flat key. |
| `SoftPress` | A slow, gentle shrink for large cards. |
| `Squish` | Squashes flat and a bit wider while held. |
| `Tilt` | Tips a couple of degrees and shrinks a bit. |

### Loop (24)

| Name | Description |
|---|---|
| `Breathe` | Swells and settles slowly, like calm breathing. |
| `ColorCycle` | Rolls through soft hues, best on white or light content. |
| `CursorBlink` | Hard on and off blink with the rhythm of a text cursor. |
| `Drift` | Wanders a few pixels around its spot, very slowly. |
| `Hovering` | Floats above the surface, the shadow growing as it rises. |
| `Jitter` | Shakes by about a pixel in an uneven pattern. |
| `LoopBob` | Bobs up and down at a quick, bouncy pace. |
| `LoopFloat` | Drifts up and down on a slow sine wave. |
| `LoopHeartbeat` \* | Beats twice and rests, like a heart. |
| `LoopThrob` | A short, firm pulse that repeats every second. |
| `LoopWiggle` | Wiggles for a moment, then waits before the next round. |
| `NeonFlicker` | Glows like a neon tube that stutters off every so often. |
| `Orbit` | Traces a small circle a few pixels across. |
| `Pendulum` | Swings from its top edge like a hanging sign. |
| `PulseGlow` | Fades the accent glow in and out. |
| `Radar` | Sweeps a full turn and pings the glow at the start of each pass. |
| `Ripple` | Swells out behind a glow ring and fades back, over and over. |
| `Rock` | Rocks back and forth on its base. |
| `Shimmer` | Runs a cool then warm tint across it with a soft glow in between. |
| `Spin` | Turns a full circle at a steady pace, made for loading icons. |
| `SpinReverse` | Spins counterclockwise at loader speed. |
| `SpinSlow` | A slow full turn for badges, gears and other decoration. |
| `Sway` | Bends side to side from the bottom, like a plant in a breeze. |
| `Twinkle` | Dims and shrinks a touch now and then, like a distant star. |

### Text (14)

| Name | Description |
|---|---|
| `BlurInText` | Comes into focus out of a soft glowing haze. |
| `DecodeIn` | Decodes like a terminal, green blocks flickering before the text turns white. |
| `FadeInText` | Plain fade in for labels and paragraphs. |
| `GlitchIn` | Jumps in with a few glitchy offsets and color flashes, then sits clean. |
| `Headline` | Starts big and settles to size as it fades in, for titles. |
| `RevealCenter` | Opens from the middle outward. |
| `RevealLeft` | Wipes the text in from the left edge. |
| `SlideUpText` | Rises into place from a few pixels below while fading in. |
| `TextGlowIn` | Types on behind a glow halo that fades once the line is done. |
| `TextRiseIn` | Types on while it rises and fades in. |
| `TrackingIn` | Expands sideways as it fades in, like letter spacing opening up. |
| `Typewriter` | Types the text out in twenty even steps. |
| `TypewriterSmooth` | Reveals characters at a steady rate with no stepping. |
| `WaveIn` | Rises in with a lean that rocks back and straightens as it lands. |

### Change (10)

| Name | Description |
|---|---|
| `CountBump` | Hops up and grows a little when the number changes. |
| `ErrorShake` | Shakes sideways with a red tint. |
| `FlashHighlight` | Flashes a yellow tint and fades back. |
| `NudgeDown` | Dips a few pixels, good for a value going down. |
| `NudgeUp` | Bumps up a few pixels, good for a value going up. |
| `Refresh` | Dips in opacity while the new value swaps in. |
| `SuccessGlow` | Tints green with a soft glow, then fades out. |
| `Tick` | A small clockwise tick that settles back. |
| `ValueFlip` | Folds flat and opens again, like a flip counter. |
| `ValuePop` | Pops bigger for a moment and springs back. |

### Stagger (9)

| Name | Description |
|---|---|
| `StaggerFadeDown` | Fades each item down into place when given a stagger like 0.04 s. |
| `StaggerFadeIn` | Plain fade for list rows, played with a stagger of about 0.04 s. |
| `StaggerFadeLeft` | Each item drifts left as it fades in with a stagger of about 0.04 s. |
| `StaggerFadeUp` | Fades each item up into place when given a stagger like 0.04 s. |
| `StaggerPop` | Pops items in with a small overshoot, staggered by about 0.04 s. |
| `StaggerReveal` | Wipes items in from the left, staggered by about 0.05 s. |
| `StaggerScaleIn` | Grows items in slightly from the center with a 0.04 s stagger. |
| `StaggerSlideRight` | Slides items in from the left one by one with a 0.04 s stagger. |
| `StaggerUnfold` | Unfolds rows down from their top edge, staggered by about 0.03 s. |
