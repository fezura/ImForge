# Designs

A design is a complete look for a project in one file: a theme, a set of style classes, font
choices, a few window effects and the motions that go with it. ImForge comes with 36 of them in
`presets/designs/`, and applying one from the Library is a single undo step, so it's cheap to
try a few.

## What's in a design

A `.imdesign.json` file has these parts:

```json
{ "schemaVersion": 1, "kind": "design", "name": "Nord", "title": "Nord",
  "description": "Nord's arctic blues on slate gray, ...", "tags": ["dark", "cool", "calm", "editor"],
  "credit": "Nord palette (MIT)",
  "theme": { "colors": { "WindowBg": "#2e3440ff", "Button": "#3b4252ff", ... },
             "vars": { "WindowRounding": 6, "FramePadding": { "x": 8, "y": 5 }, ... } },
  "styleClasses": { "accent-button": { ... }, "card": { ... }, "muted": { ... }, ... },
  "fonts": [ { "role": "ui", "family": "Inter", "size": 15 },
             { "role": "heading", "family": "Inter", "weight": "SemiBold", "size": 18 },
             { "role": "mono", "family": "JetBrains Mono", "size": 13 } ],
  "fx": { "windowBlur": 0, "windowShadow": 0.2, "accentGlow": 0 },
  "motions": { "window": "FadeInUp", "window.exit": "FadeOutDown", "button.hover": "Brighten",
               "button.press": "Sink", "list": { "preset": "StaggerFadeLeft", "stagger": 0.04 } },
  "widgetStyle": { "accent": "#88c0d0ff", "accent2": "#81a1c1ff", "success": "#a3be8cff",
                   "warning": "#ebcb8bff", "danger": "#bf616aff" } }
```

The theme is a full `ImGuiStyle`: every color and the style vars, the same format as a project
theme. Every bundled design defines the same eight style classes (`accent-button`,
`ghost-button`, `card`, `heading`, `muted`, `success`, `warning`, `danger`), so a panel built
with those classes can switch designs and still look right.

Fonts are listed by role. `ui` becomes the project's default font and the other roles become
fonts named after the role (`Heading`, `Mono`, ...), which nodes can pick with `font`. The
family names say what the design was drawn with, but the font files aren't shipped: unless an
entry has a `file`, the builder uses the bundled Inter (Regular or Bold, depending on
`weight`) or JetBrains Mono for anything with "mono" or "code" in its name. Point `file` at
a TTF in your project, or change the font in the Fonts panel afterwards.

`fx` holds three numbers from 0 to 1. `windowShadow` puts a soft shadow behind each panel's
window, `windowBlur` adds the shader blur, and `accentGlow` adds a glow in the accent color to
nodes with the `accent-button` (or `accent`) class. A value of 0 removes the effect.

`motions` maps a target, and optionally a trigger, to a preset. The targets are `window` (the
panel's root window), `button` (`Button`, `SmallButton` and any widget type ending in
`Button`), `list` (a `VStack`, `ListBox`, `Child` or `Grid` with at least two children), or a
node type in lower case such as `checkbox` or `popup`. The part after the dot is the trigger,
`enter` when there's none. A value is a preset name or an object with `preset` (or `custom`),
`durationScale`, `delay`, `stagger` and `iterations`. A `list` motion always staggers, 0.05 s
apart unless the design says otherwise. See [MOTION.md](MOTION.md) for what the presets do.

`widgetStyle` lists the accent and status colors. The glow uses `accent`, and the Library cards
use these for their swatches.

## Applying one

Open the Library (Ctrl+L, View > Library or the Library button in the status bar) and switch to
Designs. Each card draws a small window in the design's colors. Click one to select it, then
choose how much of it to apply in the combo next to Apply:

- Theme only adds the design's theme to the project (or updates a theme with the same name)
  and makes it active.
- Theme + style classes, the default, also merges the style classes by name, replacing
  classes you already have with the same name.
- Everything also sets the fonts, the window effects and the default motions on windows,
  buttons and lists.

Double-clicking a card applies it with the current mode. The status bar says what changed, and
Ctrl+Z puts everything back in one step. Locked nodes are left alone. With Preview on canvas
switched on, the Canvas draws with the selected design's theme while the project stays
unchanged, which is handy for flipping through designs before you commit to one.

## Making your own

Copy one of the files in `presets/designs/`, change `name` (a unique PascalCase name, also used
for the theme) and `title`, and edit the rest. Only `kind`, `name` and `theme` are required;
the builder skips the parts a file doesn't have. Put the file in `presets/designs/` next to the
builder (or in the source tree when you run it from a build folder) and it shows up in the
Library the next time it starts.

The easiest way to get the theme part is to build it in the Theme Editor and copy the colors
and vars from `themes/<Name>.theme.json`. `schemas/design.schema.json` describes the format and
`python tools/validate_json.py presets/designs` checks your file.

## The bundled designs

| Design | | Look |
|---|---|---|
| `BloodMoon` | dark | Deep crimson on black with an ember orange for checks and links. |
| `CarbonOrange` | dark | Carbon black with safety orange, like a race car dash. |
| `CatppuccinLatte` | light | Catppuccin Latte, the light flavor: pastel accents on a cool off white, mauve primary buttons and gentle shadows. |
| `CatppuccinMocha` | dark | Catppuccin Mocha: soft pastels on a cozy dark base, with borderless panels and rounded corners. |
| `Dracula` | dark | The free Dracula colors: purple, pink and cyan over a deep blue gray. |
| `EsportsRed` | dark | Square corners, a hard red title strip and a bar on top of the active tab. |
| `EverforestDark` | dark | Everforest's soft greens and warm beige on a mossy gray, made for long and comfortable sessions. |
| `GlassDark` | dark | Translucent dark panels with white hairline edges and big radii, meant to float over a game scene. |
| `GlassLight` | light | Frosted white panels over a pale backdrop, with a soft bevel on inputs and centered titles. |
| `GoldLuxury` | dark | Muted gold on warm black, roomy spacing and centered titles. |
| `HighContrast` | dark | Pure black and white with yellow and cyan for focus and selection, thick 2 px borders and large type. |
| `IceFrost` | dark | Pale cyan on cold navy with soft corners and faint light borders. |
| `KanagawaWave` | dark | Kanagawa Wave: indigo ink, parchment text and muted woodblock print colors. |
| `LayeredDesk` | dark | A neutral desktop look with stacked gray surfaces, thin borders and a blue line over the active tab. |
| `MidnightTool` | dark | A neutral dark theme for editors and inspectors, with one calm blue accent and teal for secondary data. |
| `MilitaryOliveHUD` | dark | Olive and amber on a dark field, with no rounding and a 1px border on everything. |
| `MinimalMono` | dark | Grays only, with white as the single accent. |
| `NeonCyber` | dark | Cyan on near black with thin glowing outlines and tight spacing, for overlays you read at a glance. |
| `NeuDark` | dark | Soft, borderless surfaces in slate, where buttons sit raised and fields sit sunk in. |
| `NeuLight` | light | The light take on the soft clay look, with beveled fields and buttons. |
| `Nord` | dark | Nord's arctic blues on slate gray, with frost colored accents and framed inputs. |
| `OneDark` | dark | The One Dark editor palette: neutral gray blue, soft white text and the familiar syntax colors as accents. |
| `PaperSepia` | light | Warm paper, brown and green ink and serif type, like an old field manual. |
| `PastelCute` | light | Bubblegum pink and sky blue on soft white, with pill shaped controls, bouncy motion and lots of breathing room. |
| `RetroTerminal` | dark | Green phosphor on black with square edges and a border on everything. |
| `RosePine` | dark | Rose Pine's muted dusk: soft iris and rose over deep plum, with roomy spacing and no hard borders. |
| `SleekPurpleGamer` | dark | Soft rounded panels in deep violet with lilac highlights. |
| `SoftDesktop` | light | A polished light desktop look: white fields, hairline borders and segmented tabs on a soft gray window. |
| `SolarizedDark` | dark | Ethan Schoonover's Solarized on its dark base, with framed inputs and a compact layout. |
| `SolarizedLight` | light | Solarized on its light paper base, with blue accents and framed inputs. |
| `SynthwaveSunset` | dark | Warm orange and pink over dark plum, like an 80s horizon at dusk. |
| `TokyoNight` | dark | Tokyo Night's deep indigo with electric blue and violet accents, like a city after dark. |
| `TonalSurface` | dark | Surfaces get lighter as they rise, accent buttons and tabs are pill shaped, and there are no borders at all. |
| `ToxicGreen` | dark | Acid green on swamp black with hard outlines and a thick hazard bar over the active tab. |
| `Vaporwave` | dark | Hot pink and cyan over a purple dusk, with centered titles and round slider grabs. |
| `WarmRetro` | dark | Warm browns, cream text and burnt orange in the spirit of Gruvbox, mixed with its own colors. |

## Palette credits

Eleven designs are ports of existing color schemes, used under their MIT licenses with the
upstream copyright notices kept in [THIRD_PARTY.md](../THIRD_PARTY.md):

- `CatppuccinMocha` and `CatppuccinLatte`: [Catppuccin](https://github.com/catppuccin/palette)
- `Dracula`: the free [Dracula](https://github.com/dracula/dracula-theme) colors (not Dracula PRO)
- `EverforestDark`: [Everforest](https://github.com/sainnhe/everforest)
- `KanagawaWave`: [Kanagawa](https://github.com/rebelot/kanagawa.nvim)
- `Nord`: [Nord](https://github.com/nordtheme/nord)
- `OneDark`: [onedark.vim](https://github.com/joshdick/onedark.vim), after Atom's One Dark
- `RosePine`: [Rosé Pine](https://github.com/rose-pine/neovim)
- `SolarizedDark` and `SolarizedLight`: [Solarized](https://github.com/altercation/solarized)
- `TokyoNight`: [Tokyo Night](https://github.com/enkia/tokyo-night-vscode-theme)

The names belong to those projects, and using them here doesn't mean the authors endorse
ImForge. The other designs, `WarmRetro` included, use their own colors.
