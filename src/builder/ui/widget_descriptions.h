#pragma once

#include <cstring>

namespace imforge::builder {

inline const char* WidgetDescription(const char* type) {
    struct Entry {
        const char* type;
        const char* text;
    };
    static const Entry kTable[] = {
        {"Window", "Top-level ImGui window. Panel root only; drawn as a child region in the canvas."},
        {"VStack", "Vertical flow: children laid out top to bottom with optional spacing and indent."},
        {"HStack", "Horizontal row: SameLine between children; fill widths share the remaining space."},
        {"Group", "BeginGroup/EndGroup: treats its children as one item for layout and SameLine."},
        {"Child", "Scrollable child region (BeginChild) with optional border, flags and size."},
        {"Grid", "Table layout (BeginTable): each child fills one cell, row-major."},
        {"TabBar", "Tab strip (BeginTabBar). Children must be Tab nodes."},
        {"Tab", "One tab page (BeginTabItem) with a label and optional close button."},
        {"CollapsingHeader", "Collapsible section header; children are shown when open."},
        {"TreeNode", "Expandable tree item (TreeNode/TreePop) that indents its children."},
        {"Popup", "Popup window opened by an openPopup action targeting this node id."},
        {"Modal", "Blocking modal popup (BeginPopupModal) with a title and optional close button."},
        {"MenuBar", "Menu bar of the enclosing Window (requires menuBar = true)."},
        {"Menu", "Drop-down menu; children are MenuItem, Menu or Separator."},
        {"Canvas", "Free-form area; children are positioned by layout.anchor and layout.offset."},
        {"Component", "Instance of a project component, expanded from its definition."},
        {"Slot", "Placeholder inside a component definition filled by the instance's slot content."},
        {"Code", "Raw C++ emitted verbatim; shown as a placeholder in the canvas."},
        {"Text", "Static or interpolated text with optional color, wrapping, bullet or disabled look."},
        {"SeparatorText", "Horizontal separator with an inline caption (ImGui >= 1.89.5)."},
        {"Separator", "Horizontal separator line."},
        {"Spacing", "Vertical spacing; count repeats ImGui::Spacing()."},
        {"NewLine", "Moves to the next line (undoes a SameLine)."},
        {"Dummy", "Invisible item of a given size used for spacing or alignment."},
        {"Button", "Push button that fires onClick; supports repeat and an explicit size."},
        {"SmallButton", "Compact button without frame padding."},
        {"ArrowButton", "Square button showing an arrow in the given direction."},
        {"Checkbox", "Boolean toggle bound to a bool value."},
        {"RadioButton", "Radio option writing its value into the bound int when selected."},
        {"Combo", "Drop-down list; bound int or enum selects among items."},
        {"ListBox", "Scrollable list of items bound to an int or enum selection."},
        {"SliderFloat", "Horizontal float slider (SliderFloat2/3/4 with components > 1)."},
        {"SliderInt", "Horizontal integer slider (SliderInt2/3/4 with components > 1)."},
        {"VSliderFloat", "Vertical float slider with an explicit size."},
        {"VSliderInt", "Vertical integer slider with an explicit size."},
        {"DragFloat", "Float field edited by dragging; speed, min/max and format."},
        {"DragInt", "Integer field edited by dragging; speed, min/max and format."},
        {"InputText", "Single-line text input with optional hint; bound to a string or char buffer."},
        {"InputTextMultiline", "Multi-line text editor with an explicit size."},
        {"InputInt", "Integer input with +/- step buttons."},
        {"InputFloat", "Float input with optional step buttons and format."},
        {"ColorEdit", "Inline RGBA color editor (ColorEdit3 when alpha is off)."},
        {"ColorPicker", "Full color picker widget."},
        {"ColorButton", "Color swatch button that fires onClick."},
        {"ProgressBar", "Progress bar from a fraction or bound float with an optional overlay."},
        {"Image", "Texture image with uv coordinates, tint and border."},
        {"ImageButton", "Clickable texture image."},
        {"Selectable", "Selectable row or cell that fires onClick; optional bound selected flag."},
        {"MenuItem", "Menu entry with optional shortcut text and bound selected flag."},
        {"PlotLines", "Line plot of a bound float array."},
        {"PlotHistogram", "Histogram of a bound float array."},
    };
    if (!type) return "";
    for (const Entry& e : kTable)
        if (std::strcmp(e.type, type) == 0) return e.text;
    return "";
}

}  // namespace imforge::builder
