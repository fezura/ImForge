#pragma once

#include <cstddef>

#include "imforge_widgets_defs.h"

namespace imforge {
namespace detail {

inline constexpr const char* kEvOpenClose[] = {"onOpen", "onClose"};
inline constexpr const char* kEvChange[] = {"onChange"};
inline constexpr const char* kEvClick[] = {"onClick"};
inline constexpr const char* kEvChangeEnter[] = {"onChange", "onEnter"};

inline constexpr BindDef kBindOpenOpt[] = {{"open", "bool", false}};
inline constexpr BindDef kBindValueBool[] = {{"value", "bool", true}};
inline constexpr BindDef kBindValueInt[] = {{"value", "int", true}};
inline constexpr BindDef kBindValueIntEnum[] = {{"value", "int|enum", true}};
inline constexpr BindDef kBindValueFloatN[] = {{"value", "float|float[N]|vec2|vec4", true}};
inline constexpr BindDef kBindValueIntN[] = {{"value", "int|int[N]", true}};
inline constexpr BindDef kBindValueFloat[] = {{"value", "float", true}};
inline constexpr BindDef kBindValueFloatOpt[] = {{"value", "float", false}};
inline constexpr BindDef kBindValueString[] = {{"value", "string|char[N]", true}};
inline constexpr BindDef kBindValueColor[] = {{"value", "vec4|float[4]|float[3]", true}};
inline constexpr BindDef kBindTexture[] = {{"texture", "texture", true}};
inline constexpr BindDef kBindSelectedOpt[] = {{"selected", "bool", false}};
inline constexpr BindDef kBindValues[] = {{"values", "float[]|vector<float>", true}};

constexpr const char* kSizeAuto = R"({"w":"auto","h":"auto"})";
constexpr const char* kSizeFill = R"({"w":"fill","h":"fill"})";
constexpr const char* kCondEnum = "Always|Once|FirstUseEver|Appearing";

inline constexpr PropDef kWindowProps[] = {
    {"title", PropType::String, "\"Window\"", nullptr, 0, 0, 18500, "Window title"},
    {"flags", PropType::Flags, "[]", "WindowFlags", 0, 0, 18500, nullptr},
    {"initialSize", PropType::Vec2, R"({"x":400,"y":300})", nullptr, 0, 0, 18500,
     "SetNextWindowSize; null = none"},
    {"initialPos", PropType::Vec2, "null", nullptr, 0, 0, 18500, "SetNextWindowPos; null = none"},
    {"sizeCond", PropType::Enum, "\"FirstUseEver\"", kCondEnum, 0, 0, 18500,
     "ImGuiCond for initialSize/initialPos"},
    {"menuBar", PropType::Bool, "false", nullptr, 0, 0, 18500, "Adds ImGuiWindowFlags_MenuBar"},
};
inline constexpr PropDef kVStackProps[] = {
    {"spacing", PropType::Float, "null", nullptr, 0, 0, 18500, "null = style.ItemSpacing.y"},
    {"indent", PropType::Float, "0", nullptr, 0, 0, 18500, "Indent() applied to children"},
};
inline constexpr PropDef kHStackProps[] = {
    {"spacing", PropType::Float, "null", nullptr, 0, 0, 18500, "null = style.ItemSpacing.x"},
    {"align", PropType::Enum, "\"Start\"", "Start|Center|End", 0, 0, 18500, nullptr},
};
inline constexpr PropDef kChildProps[] = {
    {"border", PropType::Bool, "false", nullptr, 0, 0, 18500, nullptr},
    {"flags", PropType::Flags, "[]", "WindowFlags", 0, 0, 18500, nullptr},
    {"childFlags", PropType::Flags, "[]", "ChildFlags", 0, 0, 19000, "ImGui >= 1.90"},
    {"size", PropType::Size, kSizeFill, nullptr, 0, 0, 18500, nullptr},
};
inline constexpr PropDef kGridProps[] = {
    {"columns", PropType::Int, "2", nullptr, 1, 64, 18500, nullptr},
    {"flags", PropType::Flags, "[]", "TableFlags", 0, 0, 18500, nullptr},
    {"headers", PropType::StringList, "null", nullptr, 0, 0, 18500, "null = no header row"},
    {"columnWidths", PropType::SizeList, "null", nullptr, 0, 0, 18500,
     "SizeSpec per column; null = default"},
};
inline constexpr PropDef kTabBarProps[] = {
    {"flags", PropType::Flags, "[]", "TabBarFlags", 0, 0, 18500, nullptr},
};
inline constexpr PropDef kTabProps[] = {
    {"label", PropType::String, "\"Tab\"", nullptr, 0, 0, 18500, nullptr},
    {"flags", PropType::Flags, "[]", "TabItemFlags", 0, 0, 18500, nullptr},
    {"closable", PropType::Bool, "false", nullptr, 0, 0, 18500, "Show close button"},
};
inline constexpr PropDef kCollapsingHeaderProps[] = {
    {"label", PropType::String, "\"Header\"", nullptr, 0, 0, 18500, nullptr},
    {"defaultOpen", PropType::Bool, "false", nullptr, 0, 0, 18500, nullptr},
    {"flags", PropType::Flags, "[]", "TreeNodeFlags", 0, 0, 18500, nullptr},
};
inline constexpr PropDef kTreeNodeProps[] = {
    {"label", PropType::String, "\"Node\"", nullptr, 0, 0, 18500, nullptr},
    {"defaultOpen", PropType::Bool, "false", nullptr, 0, 0, 18500, nullptr},
    {"flags", PropType::Flags, "[]", "TreeNodeFlags", 0, 0, 18500, nullptr},
};
inline constexpr PropDef kPopupProps[] = {
    {"flags", PropType::Flags, "[]", "WindowFlags", 0, 0, 18500, nullptr},
};
inline constexpr PropDef kModalProps[] = {
    {"title", PropType::String, "\"Modal\"", nullptr, 0, 0, 18500, nullptr},
    {"flags", PropType::Flags, "[]", "WindowFlags", 0, 0, 18500, nullptr},
    {"closable", PropType::Bool, "false", nullptr, 0, 0, 18500, "Show close button"},
};
inline constexpr PropDef kMenuProps[] = {
    {"label", PropType::String, "\"Menu\"", nullptr, 0, 0, 18500, nullptr},
    {"enabled", PropType::Bool, "true", nullptr, 0, 0, 18500, nullptr},
};
inline constexpr PropDef kCanvasProps[] = {
    {"border", PropType::Bool, "false", nullptr, 0, 0, 18500, nullptr},
    {"size", PropType::Size, kSizeAuto, nullptr, 0, 0, 18500, nullptr},
};
inline constexpr PropDef kComponentProps[] = {
    {"component", PropType::String, "\"\"", nullptr, 0, 0, 18500, "Component definition name"},
};
inline constexpr PropDef kSlotProps[] = {
    {"name", PropType::String, "\"content\"", nullptr, 0, 0, 18500, "Slot name"},
};
inline constexpr PropDef kCodeProps[] = {
    {"code", PropType::Code, "\"\"", nullptr, 0, 0, 18500, "Raw C++ emitted verbatim"},
    {"preview", PropType::Enum, "\"Placeholder\"", "Placeholder|Live", 0, 0, 18500,
     "Canvas rendering when not attached"},
};
inline constexpr PropDef kTextProps[] = {
    {"text", PropType::String, "\"Text\"", nullptr, 0, 0, 18500, "Supports {expr} interpolation"},
    {"color", PropType::Color, "null", nullptr, 0, 0, 18500, "null = ImGuiCol_Text"},
    {"wrapped", PropType::Bool, "false", nullptr, 0, 0, 18500, nullptr},
    {"bullet", PropType::Bool, "false", nullptr, 0, 0, 18500, nullptr},
    {"disabled", PropType::Bool, "false", nullptr, 0, 0, 18500, nullptr},
};
inline constexpr PropDef kSeparatorTextProps[] = {
    {"text", PropType::String, "\"Section\"", nullptr, 0, 0, 18950, nullptr},
};
inline constexpr PropDef kSpacingProps[] = {
    {"count", PropType::Int, "1", nullptr, 1, 100, 18500, "Number of Spacing() calls"},
};
inline constexpr PropDef kDummyProps[] = {
    {"size", PropType::Size, kSizeAuto, nullptr, 0, 0, 18500, nullptr},
};
inline constexpr PropDef kButtonProps[] = {
    {"label", PropType::String, "\"Button\"", nullptr, 0, 0, 18500, nullptr},
    {"repeat", PropType::Bool, "false", nullptr, 0, 0, 18500, "PushButtonRepeat"},
    {"size", PropType::Size, kSizeAuto, nullptr, 0, 0, 18500, nullptr},
};
inline constexpr PropDef kSmallButtonProps[] = {
    {"label", PropType::String, "\"Button\"", nullptr, 0, 0, 18500, nullptr},
};
inline constexpr PropDef kArrowButtonProps[] = {
    {"dir", PropType::Enum, "\"Right\"", "Left|Right|Up|Down", 0, 0, 18500, nullptr},
};
inline constexpr PropDef kCheckboxProps[] = {
    {"label", PropType::String, "\"Checkbox\"", nullptr, 0, 0, 18500, nullptr},
};
inline constexpr PropDef kRadioButtonProps[] = {
    {"label", PropType::String, "\"Radio\"", nullptr, 0, 0, 18500, nullptr},
    {"value", PropType::Int, "0", nullptr, 0, 0, 18500, "Value written when selected"},
};
inline constexpr PropDef kComboProps[] = {
    {"label", PropType::String, "\"Combo\"", nullptr, 0, 0, 18500, nullptr},
    {"items", PropType::StringList, "[]", nullptr, 0, 0, 18500, "Empty + enum bind = enum names"},
    {"flags", PropType::Flags, "[]", "ComboFlags", 0, 0, 18500, nullptr},
};
inline constexpr PropDef kListBoxProps[] = {
    {"label", PropType::String, "\"ListBox\"", nullptr, 0, 0, 18500, nullptr},
    {"items", PropType::StringList, "[]", nullptr, 0, 0, 18500, nullptr},
    {"heightItems", PropType::Int, "-1", nullptr, -1, 1000, 18500, "-1 = ImGui default"},
};
inline constexpr PropDef kSliderFloatProps[] = {
    {"label", PropType::String, "\"Slider\"", nullptr, 0, 0, 18500, nullptr},
    {"min", PropType::Float, "0", nullptr, 0, 0, 18500, nullptr},
    {"max", PropType::Float, "1", nullptr, 0, 0, 18500, nullptr},
    {"format", PropType::String, "\"%.3f\"", nullptr, 0, 0, 18500, nullptr},
    {"flags", PropType::Flags, "[]", "SliderFlags", 0, 0, 18500, nullptr},
    {"components", PropType::Int, "1", nullptr, 1, 4, 18500, "SliderFloat2/3/4"},
};
inline constexpr PropDef kSliderIntProps[] = {
    {"label", PropType::String, "\"Slider\"", nullptr, 0, 0, 18500, nullptr},
    {"min", PropType::Int, "0", nullptr, 0, 0, 18500, nullptr},
    {"max", PropType::Int, "100", nullptr, 0, 0, 18500, nullptr},
    {"format", PropType::String, "\"%d\"", nullptr, 0, 0, 18500, nullptr},
    {"flags", PropType::Flags, "[]", "SliderFlags", 0, 0, 18500, nullptr},
    {"components", PropType::Int, "1", nullptr, 1, 4, 18500, "SliderInt2/3/4"},
};
inline constexpr PropDef kVSliderFloatProps[] = {
    {"label", PropType::String, "\"VSlider\"", nullptr, 0, 0, 18500, nullptr},
    {"min", PropType::Float, "0", nullptr, 0, 0, 18500, nullptr},
    {"max", PropType::Float, "1", nullptr, 0, 0, 18500, nullptr},
    {"format", PropType::String, "\"%.3f\"", nullptr, 0, 0, 18500, nullptr},
    {"flags", PropType::Flags, "[]", "SliderFlags", 0, 0, 18500, nullptr},
    {"size", PropType::Size, kSizeAuto, nullptr, 0, 0, 18500, nullptr},
};
inline constexpr PropDef kVSliderIntProps[] = {
    {"label", PropType::String, "\"VSlider\"", nullptr, 0, 0, 18500, nullptr},
    {"min", PropType::Int, "0", nullptr, 0, 0, 18500, nullptr},
    {"max", PropType::Int, "100", nullptr, 0, 0, 18500, nullptr},
    {"format", PropType::String, "\"%d\"", nullptr, 0, 0, 18500, nullptr},
    {"flags", PropType::Flags, "[]", "SliderFlags", 0, 0, 18500, nullptr},
    {"size", PropType::Size, kSizeAuto, nullptr, 0, 0, 18500, nullptr},
};
inline constexpr PropDef kDragFloatProps[] = {
    {"label", PropType::String, "\"Drag\"", nullptr, 0, 0, 18500, nullptr},
    {"speed", PropType::Float, "1", nullptr, 0, 0, 18500, nullptr},
    {"min", PropType::Float, "0", nullptr, 0, 0, 18500, "min == max == 0: no clamp"},
    {"max", PropType::Float, "0", nullptr, 0, 0, 18500, nullptr},
    {"format", PropType::String, "\"%.3f\"", nullptr, 0, 0, 18500, nullptr},
    {"flags", PropType::Flags, "[]", "SliderFlags", 0, 0, 18500, nullptr},
    {"components", PropType::Int, "1", nullptr, 1, 4, 18500, "DragFloat2/3/4"},
};
inline constexpr PropDef kDragIntProps[] = {
    {"label", PropType::String, "\"Drag\"", nullptr, 0, 0, 18500, nullptr},
    {"speed", PropType::Float, "1", nullptr, 0, 0, 18500, nullptr},
    {"min", PropType::Int, "0", nullptr, 0, 0, 18500, "min == max == 0: no clamp"},
    {"max", PropType::Int, "0", nullptr, 0, 0, 18500, nullptr},
    {"format", PropType::String, "\"%d\"", nullptr, 0, 0, 18500, nullptr},
    {"flags", PropType::Flags, "[]", "SliderFlags", 0, 0, 18500, nullptr},
    {"components", PropType::Int, "1", nullptr, 1, 4, 18500, "DragInt2/3/4"},
};
inline constexpr PropDef kInputTextProps[] = {
    {"label", PropType::String, "\"Input\"", nullptr, 0, 0, 18500, nullptr},
    {"hint", PropType::String, "\"\"", nullptr, 0, 0, 18500, "Non-empty = InputTextWithHint"},
    {"flags", PropType::Flags, "[]", "InputTextFlags", 0, 0, 18500, nullptr},
    {"maxLength", PropType::Int, "256", nullptr, 1, 1048576, 18500, "char[N] buffer size"},
};
inline constexpr PropDef kInputTextMultilineProps[] = {
    {"label", PropType::String, "\"Input\"", nullptr, 0, 0, 18500, nullptr},
    {"flags", PropType::Flags, "[]", "InputTextFlags", 0, 0, 18500, nullptr},
    {"maxLength", PropType::Int, "256", nullptr, 1, 1048576, 18500, "char[N] buffer size"},
    {"size", PropType::Size, kSizeAuto, nullptr, 0, 0, 18500, nullptr},
};
inline constexpr PropDef kInputIntProps[] = {
    {"label", PropType::String, "\"Input\"", nullptr, 0, 0, 18500, nullptr},
    {"step", PropType::Int, "1", nullptr, 0, 0, 18500, nullptr},
    {"stepFast", PropType::Int, "100", nullptr, 0, 0, 18500, nullptr},
    {"flags", PropType::Flags, "[]", "InputTextFlags", 0, 0, 18500, nullptr},
    {"components", PropType::Int, "1", nullptr, 1, 4, 18500, "InputInt2/3/4"},
};
inline constexpr PropDef kInputFloatProps[] = {
    {"label", PropType::String, "\"Input\"", nullptr, 0, 0, 18500, nullptr},
    {"step", PropType::Float, "0", nullptr, 0, 0, 18500, "0 = no step buttons"},
    {"stepFast", PropType::Float, "0", nullptr, 0, 0, 18500, nullptr},
    {"format", PropType::String, "\"%.3f\"", nullptr, 0, 0, 18500, nullptr},
    {"flags", PropType::Flags, "[]", "InputTextFlags", 0, 0, 18500, nullptr},
    {"components", PropType::Int, "1", nullptr, 1, 4, 18500, "InputFloat2/3/4"},
};
inline constexpr PropDef kColorEditProps[] = {
    {"label", PropType::String, "\"Color\"", nullptr, 0, 0, 18500, nullptr},
    {"alpha", PropType::Bool, "true", nullptr, 0, 0, 18500, "false = ColorEdit3"},
    {"flags", PropType::Flags, "[]", "ColorEditFlags", 0, 0, 18500, nullptr},
};
inline constexpr PropDef kColorPickerProps[] = {
    {"label", PropType::String, "\"Color\"", nullptr, 0, 0, 18500, nullptr},
    {"alpha", PropType::Bool, "true", nullptr, 0, 0, 18500, "false = ColorPicker3"},
    {"flags", PropType::Flags, "[]", "ColorEditFlags", 0, 0, 18500, nullptr},
};
inline constexpr PropDef kColorButtonProps[] = {
    {"desc", PropType::String, "\"color\"", nullptr, 0, 0, 18500, "Tooltip / id string"},
    {"color", PropType::Color, "\"#ffffffff\"", nullptr, 0, 0, 18500, nullptr},
    {"flags", PropType::Flags, "[]", "ColorEditFlags", 0, 0, 18500, nullptr},
    {"size", PropType::Size, kSizeAuto, nullptr, 0, 0, 18500, nullptr},
};
inline constexpr PropDef kProgressBarProps[] = {
    {"fraction", PropType::Float, "0", nullptr, 0, 1, 18500, "0..1, literal or expr"},
    {"overlay", PropType::String, "null", nullptr, 0, 0, 18500, "null = percentage text"},
    {"size", PropType::Size, kSizeAuto, nullptr, 0, 0, 18500, nullptr},
};
inline constexpr PropDef kImageProps[] = {
    {"uv0", PropType::Vec2, R"({"x":0,"y":0})", nullptr, 0, 0, 18500, nullptr},
    {"uv1", PropType::Vec2, R"({"x":1,"y":1})", nullptr, 0, 0, 18500, nullptr},
    {"tint", PropType::Color, "\"#ffffffff\"", nullptr, 0, 0, 18500, nullptr},
    {"border", PropType::Color, "\"#00000000\"", nullptr, 0, 0, 18500, "Transparent = none"},
    {"size", PropType::Size, kSizeAuto, nullptr, 0, 0, 18500, nullptr},
};
inline constexpr PropDef kImageButtonProps[] = {
    {"uv0", PropType::Vec2, R"({"x":0,"y":0})", nullptr, 0, 0, 18500, nullptr},
    {"uv1", PropType::Vec2, R"({"x":1,"y":1})", nullptr, 0, 0, 18500, nullptr},
    {"bg", PropType::Color, "\"#00000000\"", nullptr, 0, 0, 18500, nullptr},
    {"tint", PropType::Color, "\"#ffffffff\"", nullptr, 0, 0, 18500, nullptr},
    {"size", PropType::Size, kSizeAuto, nullptr, 0, 0, 18500, nullptr},
};
inline constexpr PropDef kSelectableProps[] = {
    {"label", PropType::String, "\"Selectable\"", nullptr, 0, 0, 18500, nullptr},
    {"flags", PropType::Flags, "[]", "SelectableFlags", 0, 0, 18500, nullptr},
    {"size", PropType::Size, kSizeAuto, nullptr, 0, 0, 18500, nullptr},
};
inline constexpr PropDef kMenuItemProps[] = {
    {"label", PropType::String, "\"Item\"", nullptr, 0, 0, 18500, nullptr},
    {"shortcut", PropType::String, "\"\"", nullptr, 0, 0, 18500, "Display only"},
    {"enabled", PropType::Bool, "true", nullptr, 0, 0, 18500, nullptr},
};
inline constexpr PropDef kPlotProps[] = {
    {"label", PropType::String, "\"\"", nullptr, 0, 0, 18500, nullptr},
    {"overlay", PropType::String, "null", nullptr, 0, 0, 18500, nullptr},
    {"scaleMin", PropType::Float, "null", nullptr, 0, 0, 18500, "null = auto (FLT_MAX)"},
    {"scaleMax", PropType::Float, "null", nullptr, 0, 0, 18500, "null = auto (FLT_MAX)"},
    {"size", PropType::Size, kSizeAuto, nullptr, 0, 0, 18500, nullptr},
};

constexpr const char* kCatContainers = "Containers";
constexpr const char* kCatText = "Text";
constexpr const char* kCatButtons = "Buttons";
constexpr const char* kCatInputs = "Inputs";
constexpr const char* kCatDisplay = "Display";

inline constexpr WidgetDef kStockWidgets[] = {
    MakeWidget("Window", kCatContainers, kWindowProps, kBindOpenOpt, kEvOpenClose, true, false,
               nullptr, 18500, "fa-window-maximize"),
    MakeWidget("VStack", kCatContainers, kVStackProps, nullptr, nullptr, true, false, nullptr,
               18500, "fa-grip-lines"),
    MakeWidget("HStack", kCatContainers, kHStackProps, nullptr, nullptr, true, false, nullptr,
               18500, "fa-grip-lines-vertical"),
    MakeWidget("Group", kCatContainers, nullptr, nullptr, nullptr, true, false, nullptr, 18500,
               "fa-object-group"),
    MakeWidget("Child", kCatContainers, kChildProps, nullptr, nullptr, true, true, nullptr, 18500,
               "fa-window-restore"),
    MakeWidget("Grid", kCatContainers, kGridProps, nullptr, nullptr, true, false, nullptr, 18500,
               "fa-table-cells"),
    MakeWidget("TabBar", kCatContainers, kTabBarProps, nullptr, kEvChange, true, false, "Tab",
               18500, "fa-folder"),
    MakeWidget("Tab", kCatContainers, kTabProps, kBindOpenOpt, kEvOpenClose, true, false, nullptr,
               18500, "fa-folder-open"),
    MakeWidget("CollapsingHeader", kCatContainers, kCollapsingHeaderProps, nullptr, kEvOpenClose,
               true, false, nullptr, 18500, "fa-square-caret-down"),
    MakeWidget("TreeNode", kCatContainers, kTreeNodeProps, nullptr, kEvOpenClose, true, false,
               nullptr, 18500, "fa-folder-tree"),
    MakeWidget("Popup", kCatContainers, kPopupProps, nullptr, kEvOpenClose, true, false, nullptr,
               18500, "fa-message"),
    MakeWidget("Modal", kCatContainers, kModalProps, kBindOpenOpt, kEvOpenClose, true, false,
               nullptr, 18500, "fa-clone"),
    MakeWidget("MenuBar", kCatContainers, nullptr, nullptr, nullptr, true, false, nullptr, 18500,
               "fa-bars"),
    MakeWidget("Menu", kCatContainers, kMenuProps, nullptr, nullptr, true, false,
               "MenuItem|Menu|Separator", 18500, "fa-list"),
    MakeWidget("Canvas", kCatContainers, kCanvasProps, nullptr, nullptr, true, true, nullptr,
               18500, "fa-vector-square"),
    MakeWidget("Component", kCatContainers, kComponentProps, nullptr, nullptr, true, false,
               nullptr, 18500, "fa-cube"),
    MakeWidget("Slot", kCatContainers, kSlotProps, nullptr, nullptr, false, false, nullptr, 18500,
               "fa-plug"),
    MakeWidget("Code", kCatContainers, kCodeProps, nullptr, nullptr, false, false, nullptr, 18500,
               "fa-code"),
    MakeWidget("Text", kCatText, kTextProps, nullptr, nullptr, false, false, nullptr, 18500,
               "fa-font"),
    MakeWidget("SeparatorText", kCatText, kSeparatorTextProps, nullptr, nullptr, false, false,
               nullptr, 18950, "fa-heading"),
    MakeWidget("Separator", kCatText, nullptr, nullptr, nullptr, false, false, nullptr, 18500,
               "fa-minus"),
    MakeWidget("Spacing", kCatText, kSpacingProps, nullptr, nullptr, false, false, nullptr, 18500,
               "fa-arrows-up-down"),
    MakeWidget("NewLine", kCatText, nullptr, nullptr, nullptr, false, false, nullptr, 18500,
               "fa-turn-down"),
    MakeWidget("Dummy", kCatText, kDummyProps, nullptr, nullptr, false, true, nullptr, 18500,
               "fa-border-none"),
    MakeWidget("Button", kCatButtons, kButtonProps, nullptr, kEvClick, false, true, nullptr,
               18500, "fa-square"),
    MakeWidget("SmallButton", kCatButtons, kSmallButtonProps, nullptr, kEvClick, false, false,
               nullptr, 18500, "fa-square-minus"),
    MakeWidget("ArrowButton", kCatButtons, kArrowButtonProps, nullptr, kEvClick, false, false,
               nullptr, 18500, "fa-circle-arrow-right"),
    MakeWidget("Checkbox", kCatInputs, kCheckboxProps, kBindValueBool, kEvChange, false, false,
               nullptr, 18500, "fa-check-square"),
    MakeWidget("RadioButton", kCatInputs, kRadioButtonProps, kBindValueInt, kEvChange, false,
               false, nullptr, 18500, "fa-circle-dot"),
    MakeWidget("Combo", kCatInputs, kComboProps, kBindValueIntEnum, kEvChange, false, false,
               nullptr, 18500, "fa-square-caret-down"),
    MakeWidget("ListBox", kCatInputs, kListBoxProps, kBindValueIntEnum, kEvChange, false, false,
               nullptr, 18500, "fa-list-ul"),
    MakeWidget("SliderFloat", kCatInputs, kSliderFloatProps, kBindValueFloatN, kEvChange, false,
               false, nullptr, 18500, "fa-sliders"),
    MakeWidget("SliderInt", kCatInputs, kSliderIntProps, kBindValueIntN, kEvChange, false, false,
               nullptr, 18500, "fa-sliders"),
    MakeWidget("VSliderFloat", kCatInputs, kVSliderFloatProps, kBindValueFloat, kEvChange, false,
               true, nullptr, 18500, "fa-sliders"),
    MakeWidget("VSliderInt", kCatInputs, kVSliderIntProps, kBindValueInt, kEvChange, false, true,
               nullptr, 18500, "fa-sliders"),
    MakeWidget("DragFloat", kCatInputs, kDragFloatProps, kBindValueFloatN, kEvChange, false,
               false, nullptr, 18500, "fa-arrows-left-right"),
    MakeWidget("DragInt", kCatInputs, kDragIntProps, kBindValueIntN, kEvChange, false, false,
               nullptr, 18500, "fa-arrows-left-right"),
    MakeWidget("InputText", kCatInputs, kInputTextProps, kBindValueString, kEvChangeEnter, false,
               false, nullptr, 18500, "fa-i-cursor"),
    MakeWidget("InputTextMultiline", kCatInputs, kInputTextMultilineProps, kBindValueString,
               kEvChange, false, true, nullptr, 18500, "fa-align-left"),
    MakeWidget("InputInt", kCatInputs, kInputIntProps, kBindValueIntN, kEvChange, false, false,
               nullptr, 18500, "fa-arrow-up-1-9"),
    MakeWidget("InputFloat", kCatInputs, kInputFloatProps, kBindValueFloatN, kEvChange, false,
               false, nullptr, 18500, "fa-calculator"),
    MakeWidget("ColorEdit", kCatInputs, kColorEditProps, kBindValueColor, kEvChange, false, false,
               nullptr, 18500, "fa-palette"),
    MakeWidget("ColorPicker", kCatInputs, kColorPickerProps, kBindValueColor, kEvChange, false,
               false, nullptr, 18500, "fa-eye-dropper"),
    MakeWidget("ColorButton", kCatButtons, kColorButtonProps, nullptr, kEvClick, false, true,
               nullptr, 18500, "fa-fill-drip"),
    MakeWidget("ProgressBar", kCatDisplay, kProgressBarProps, kBindValueFloatOpt, nullptr, false,
               true, nullptr, 18500, "fa-bars-progress"),
    MakeWidget("Image", kCatDisplay, kImageProps, kBindTexture, nullptr, false, true, nullptr,
               18500, "fa-image"),
    MakeWidget("ImageButton", kCatButtons, kImageButtonProps, kBindTexture, kEvClick, false, true,
               nullptr, 18500, "fa-images"),
    MakeWidget("Selectable", kCatButtons, kSelectableProps, kBindSelectedOpt, kEvClick, false,
               true, nullptr, 18500, "fa-hand-pointer"),
    MakeWidget("MenuItem", kCatButtons, kMenuItemProps, kBindSelectedOpt, kEvClick, false, false,
               nullptr, 18500, "fa-list-check"),
    MakeWidget("PlotLines", kCatDisplay, kPlotProps, kBindValues, nullptr, false, true, nullptr,
               18500, "fa-chart-line"),
    MakeWidget("PlotHistogram", kCatDisplay, kPlotProps, kBindValues, nullptr, false, true,
               nullptr, 18500, "fa-chart-column"),
};

}  // namespace detail

inline const WidgetDef* StockWidgets(size_t& count) {
    count = sizeof(detail::kStockWidgets) / sizeof(detail::kStockWidgets[0]);
    return detail::kStockWidgets;
}

}  // namespace imforge
