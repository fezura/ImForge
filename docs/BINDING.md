# Binding widgets to your data

A binding connects a widget slot (`value`, `open`, `selected`, `texture`, ...) to a member of
your C++ data. In generated code it's a plain pointer or reference. In development builds the
interpreter resolves it through a reflection table.

## Panel parameters

Each panel declares typed parameters, which become reference parameters of the generated draw
function:

```json
"params": [ { "name": "player", "type": "Player" }, { "name": "cfg", "type": "Settings" } ]
```
```cpp
void DrawMainMenu(MainMenuState& state, Player& player, Settings& cfg);
```

No hidden globals: whatever a panel reads or writes shows up in its signature.

## Binding paths

A path is `root(.field | [index])*`, where `root` is a parameter name or `state`:

| Path | Resolves to |
|---|---|
| `player.health` | `player.health` |
| `cfg.esp.enabled` | a nested struct member |
| `inv.items[2].name` | an element of `std::vector<Item>` or `Item[N]` |
| `inv.items.size` | the element count (read-only, usable in expressions) |
| `state.selectedTab` | a panel state variable |

The builder checks types. A `SliderFloat` takes `float`, `float[N]`, `ImVec2` or `ImVec4`, a
`Checkbox` takes `bool`, a `Combo` takes `int` or an enum (whose names become the items). The
properties panel autocompletes every path under the panel's parameters and flags mismatches
inline, with a suggested cast or widget.

Supported member types: `bool`, all integer widths, `float`, `double`, `std::string`,
`char[N]`, enums (scoped or not), `ImVec2`, `ImVec4`, `float[2..4]`, `int[N]`, nested structs,
`std::vector<T>` and `T[N]` of any of these, and `ImTextureID`. Pointers, getters/setters,
private members, `std::array` and `std::optional` aren't supported yet; accessor bindings are
planned for v1.1.

## Where the type information comes from

### imforge-reflect

If you have LLVM installed, this is the easy way:

```sh
imforge-reflect --compile-commands build/compile_commands.json \
                --types Player,Settings \
                --out-json generated/types.json \
                --out-header generated/imforge_reflect.h
```

`imforge-reflect` parses your real headers with your real include paths and defines (from
`compile_commands.json`, which CMake writes with `CMAKE_EXPORT_COMPILE_COMMANDS=ON`), follows
nested types and vectors, and writes two files: `types.json`, which the builder uses for
autocomplete and type checks, and `imforge_reflect.h` with the `IMFORGE_REFLECT` /
`IMFORGE_REFLECT_ENUM` macros for your dev build.

Without `compile_commands.json`, pass `--include <dir>` and `--header <file>` instead. When
libclang is found the builder can run the scanner itself.

### Writing the macros by hand

```cpp
#include "imforge_value.h"
IMFORGE_REFLECT(Item, name, count, price);
IMFORGE_REFLECT(Player, health, ammo, pos, name, items, mode);
IMFORGE_REFLECT_ENUM(Mode, Off, Aim, Silent);
```

The macros use `offsetof` and the compiler's own type deduction, so the layout is always right
for the compiler you build with. In development builds the client can send the registered table
to the builder over the live link, which gives you the same autocomplete as `types.json`.

## What a bound widget turns into

| Binding type | Generated call |
|---|---|
| `float` | `ImGui::SliderFloat("Health", &player.health, 0.f, 100.f)` |
| `float[3]` on `ColorEdit` | `ImGui::ColorEdit3("Tint", player.tint)` |
| `ImVec4` on `ColorEdit` | `ImGui::ColorEdit4("Tint", &player.tint.x)` |
| `std::string` on `InputText` | `ImGui::InputText("Name", &player.name)` (needs `imgui_stdlib.h`) |
| `char[32]` on `InputText` | `ImGui::InputText("Name", player.name, IM_ARRAYSIZE(player.name))` |
| enum on `Combo` | `ImGui::Combo("Mode", (int*)&cfg.mode, "Off\0Aim\0Silent\0")`, with the enum's underlying type checked |
| `std::vector<float>` on `PlotLines` | `ImGui::PlotLines("FPS", v.data(), (int)v.size())` |

## Development builds

Pass the roots when drawing: `imforge::live::Draw("MainMenu", { Bind("player", g_player),
Bind("cfg", g_settings) })`. `Bind` only compiles for types registered with `IMFORGE_REFLECT`.
That's on purpose.
