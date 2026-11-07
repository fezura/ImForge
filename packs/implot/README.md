# implot pack (planned for v1.1)

Nothing ships here yet. This file holds the plan so the catalog, codegen and interpreter can
keep these names free.

The pack will wrap [ImPlot](https://github.com/epezent/implot) as custom widgets
([`*.imw.json` manifests](../../docs/SPEC.md#widget-manifests)) with `"include": "implot.h"`,
plus a thin `imb_implot.h` glue header (namespace `imb::plot`) for the binding adapters.
`minImGuiVersion` 18500, ImPlot 0.14 or later. Your project links ImPlot itself; the pack only
adds manifests and the glue header.

## Planned widgets

| Widget | Props | Bindings | Events | Call |
|---|---|---|---|---|
| `Plot` (container) | `title`, `size`, `xLabel`, `yLabel`, `flags(PlotFlags)`, `xMin/xMax/yMin/yMax` (auto-fit when all 0) | | | `ImPlot::BeginPlot(...)`, `callEnd: ImPlot::EndPlot()` |
| `PlotLine` | `label`, `weight`, `color` | `xs`, `ys` (`float[N]`, `double[N]`, `vector<float>`, `vector<double>`); optional int `count` | | `ImPlot::PlotLine(...)` |
| `PlotScatter` | `label`, `markerSize`, `color` | `xs`, `ys` | | `ImPlot::PlotScatter(...)` |
| `PlotBars` | `label`, `barWidth`, `horizontal(bool)` | `values` | | `ImPlot::PlotBars(...)` |
| `PlotShaded` | `label`, `yRef` | `xs`, `ys` | | `ImPlot::PlotShaded(...)` |
| `PlotHeatmap` | `label`, `rows`, `cols`, `scaleMin`, `scaleMax`, `colormap(enum)` | `values` (`float[N]`) | | `ImPlot::PlotHeatmap(...)` |
| `PlotPie` | `labels(stringList)`, `normalize(bool)`, `center(vec2)`, `radius` | `values` | | `ImPlot::PlotPieChart(...)` |
| `PlotHistory` | `label`, `seconds`, `color` | `value` (`float`, sampled every frame into a ring buffer owned by `imb::plot`) | | `imb::plot::PlotHistory(...)` |
| `DragLineX/Y` | `label`, `color`, `thickness` | `value` (`double`) | `onChange` | `ImPlot::DragLineX(...)` |
| `PlotLegendToggle` | `label` | `visible` (`bool`) | `onChange` | `imb::plot::LegendToggle(...)` |

## Bindings

Series bindings accept `float[N]`, `double[N]`, `std::vector<float>`, `std::vector<double>` and
`ImVector<float>`. Codegen emits the pointer and count (`.data()`/`.size()` or `N`), stride 0.
Two-channel series (`xs` + `ys`) need equal counts; with `xs` unbound the index is used
(`ImPlot::PlotLine(label, ys, count)`).

The time-series helpers in `imb::plot` keep ring buffers per id in a function-local static
table, dropped after the same idle time as `imb_runtime.h` animation state.

Each manifest will carry `preview` shapes (an axes rectangle and a polyline), so the builder
canvas shows something plot-like without linking ImPlot into the builder.
