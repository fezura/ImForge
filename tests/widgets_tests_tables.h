#pragma once

#include <doctest/doctest.h>

#include <cstring>
#include <set>
#include <string>
#include <vector>

#include "imgui.h"
#include "imforge/imforge_widgets.h"

using namespace imforge;

namespace {

struct EP {
    const char* name;
    PropType type;
    const char* def;
    const char* ev;
};
struct EB {
    const char* name;
    const char* types;
    bool required;
};
struct EW {
    const char* type;
    std::vector<EP> props;
    std::vector<EB> binds;
    std::vector<const char*> events;
    bool container;
    bool sizable;
    const char* child_types;
    int min_version;
};

const char* const kAuto = R"({"w":"auto","h":"auto"})";
const char* const kFill = R"({"w":"fill","h":"fill"})";
const char* const kCond = "Always|Once|FirstUseEver|Appearing";
const char* const kFloatN = "float|float[N]|vec2|vec4";
const char* const kIntN = "int|int[N]";
const char* const kStr = "string|char[N]";
const char* const kCol = "vec4|float[4]|float[3]";

// the stock catalog as documented; defaults the docs don't mention are our own picks
const std::vector<EW>& Expected() {
    static const std::vector<EW> k = {
        {"Window",
         {{"title", PropType::String, "\"Window\"", nullptr},
          {"flags", PropType::Flags, "[]", "WindowFlags"},
          {"initialSize", PropType::Vec2, R"({"x":400,"y":300})", nullptr},
          {"initialPos", PropType::Vec2, "null", nullptr},
          {"sizeCond", PropType::Enum, "\"FirstUseEver\"", kCond},
          {"menuBar", PropType::Bool, "false", nullptr}},
         {{"open", "bool", false}}, {"onOpen", "onClose"}, true, false, nullptr, 18500},
        {"VStack",
         {{"spacing", PropType::Float, "null", nullptr}, {"indent", PropType::Float, "0", nullptr}},
         {}, {}, true, false, nullptr, 18500},
        {"HStack",
         {{"spacing", PropType::Float, "null", nullptr},
          {"align", PropType::Enum, "\"Start\"", "Start|Center|End"}},
         {}, {}, true, false, nullptr, 18500},
        {"Group", {}, {}, {}, true, false, nullptr, 18500},
        {"Child",
         {{"border", PropType::Bool, "false", nullptr},
          {"flags", PropType::Flags, "[]", "WindowFlags"},
          {"childFlags", PropType::Flags, "[]", "ChildFlags"},
          {"size", PropType::Size, kFill, nullptr}},
         {}, {}, true, true, nullptr, 18500},
        {"Grid",
         {{"columns", PropType::Int, "2", nullptr},
          {"flags", PropType::Flags, "[]", "TableFlags"},
          {"headers", PropType::StringList, "null", nullptr},
          {"columnWidths", PropType::SizeList, "null", nullptr}},
         {}, {}, true, false, nullptr, 18500},
        {"TabBar", {{"flags", PropType::Flags, "[]", "TabBarFlags"}}, {}, {"onChange"}, true,
         false, "Tab", 18500},
        {"Tab",
         {{"label", PropType::String, "\"Tab\"", nullptr},
          {"flags", PropType::Flags, "[]", "TabItemFlags"},
          {"closable", PropType::Bool, "false", nullptr}},
         {{"open", "bool", false}}, {"onOpen", "onClose"}, true, false, nullptr, 18500},
        {"CollapsingHeader",
         {{"label", PropType::String, "\"Header\"", nullptr},
          {"defaultOpen", PropType::Bool, "false", nullptr},
          {"flags", PropType::Flags, "[]", "TreeNodeFlags"}},
         {}, {"onOpen", "onClose"}, true, false, nullptr, 18500},
        {"TreeNode",
         {{"label", PropType::String, "\"Node\"", nullptr},
          {"defaultOpen", PropType::Bool, "false", nullptr},
          {"flags", PropType::Flags, "[]", "TreeNodeFlags"}},
         {}, {"onOpen", "onClose"}, true, false, nullptr, 18500},
        {"Popup", {{"flags", PropType::Flags, "[]", "WindowFlags"}}, {}, {"onOpen", "onClose"},
         true, false, nullptr, 18500},
        {"Modal",
         {{"title", PropType::String, "\"Modal\"", nullptr},
          {"flags", PropType::Flags, "[]", "WindowFlags"},
          {"closable", PropType::Bool, "false", nullptr}},
         {{"open", "bool", false}}, {"onOpen", "onClose"}, true, false, nullptr, 18500},
        {"MenuBar", {}, {}, {}, true, false, nullptr, 18500},
        {"Menu",
         {{"label", PropType::String, "\"Menu\"", nullptr},
          {"enabled", PropType::Bool, "true", nullptr}},
         {}, {}, true, false, "MenuItem|Menu|Separator", 18500},
        {"Canvas",
         {{"border", PropType::Bool, "false", nullptr}, {"size", PropType::Size, kAuto, nullptr}},
         {}, {}, true, true, nullptr, 18500},
        {"Component", {{"component", PropType::String, "\"\"", nullptr}}, {}, {}, true, false,
         nullptr, 18500},
        {"Slot", {{"name", PropType::String, "\"content\"", nullptr}}, {}, {}, false, false,
         nullptr, 18500},
        {"Code",
         {{"code", PropType::Code, "\"\"", nullptr},
          {"preview", PropType::Enum, "\"Placeholder\"", "Placeholder|Live"}},
         {}, {}, false, false, nullptr, 18500},
        {"Text",
         {{"text", PropType::String, "\"Text\"", nullptr},
          {"color", PropType::Color, "null", nullptr},
          {"wrapped", PropType::Bool, "false", nullptr},
          {"bullet", PropType::Bool, "false", nullptr},
          {"disabled", PropType::Bool, "false", nullptr}},
         {}, {}, false, false, nullptr, 18500},
        {"SeparatorText", {{"text", PropType::String, "\"Section\"", nullptr}}, {}, {}, false,
         false, nullptr, 18950},
        {"Separator", {}, {}, {}, false, false, nullptr, 18500},
        {"Spacing", {{"count", PropType::Int, "1", nullptr}}, {}, {}, false, false, nullptr, 18500},
        {"NewLine", {}, {}, {}, false, false, nullptr, 18500},
        {"Dummy", {{"size", PropType::Size, kAuto, nullptr}}, {}, {}, false, true, nullptr, 18500},
        {"Button",
         {{"label", PropType::String, "\"Button\"", nullptr},
          {"repeat", PropType::Bool, "false", nullptr},
          {"size", PropType::Size, kAuto, nullptr}},
         {}, {"onClick"}, false, true, nullptr, 18500},
        {"SmallButton", {{"label", PropType::String, "\"Button\"", nullptr}}, {}, {"onClick"},
         false, false, nullptr, 18500},
        {"ArrowButton", {{"dir", PropType::Enum, "\"Right\"", "Left|Right|Up|Down"}}, {},
         {"onClick"}, false, false, nullptr, 18500},
        {"Checkbox", {{"label", PropType::String, "\"Checkbox\"", nullptr}},
         {{"value", "bool", true}}, {"onChange"}, false, false, nullptr, 18500},
        {"RadioButton",
         {{"label", PropType::String, "\"Radio\"", nullptr},
          {"value", PropType::Int, "0", nullptr}},
         {{"value", "int", true}}, {"onChange"}, false, false, nullptr, 18500},
        {"Combo",
         {{"label", PropType::String, "\"Combo\"", nullptr},
          {"items", PropType::StringList, "[]", nullptr},
          {"flags", PropType::Flags, "[]", "ComboFlags"}},
         {{"value", "int|enum", true}}, {"onChange"}, false, false, nullptr, 18500},
        {"ListBox",
         {{"label", PropType::String, "\"ListBox\"", nullptr},
          {"items", PropType::StringList, "[]", nullptr},
          {"heightItems", PropType::Int, "-1", nullptr}},
         {{"value", "int|enum", true}}, {"onChange"}, false, false, nullptr, 18500},
        {"SliderFloat",
         {{"label", PropType::String, "\"Slider\"", nullptr},
          {"min", PropType::Float, "0", nullptr},
          {"max", PropType::Float, "1", nullptr},
          {"format", PropType::String, "\"%.3f\"", nullptr},
          {"flags", PropType::Flags, "[]", "SliderFlags"},
          {"components", PropType::Int, "1", nullptr}},
         {{"value", kFloatN, true}}, {"onChange"}, false, false, nullptr, 18500},
        {"SliderInt",
         {{"label", PropType::String, "\"Slider\"", nullptr},
          {"min", PropType::Int, "0", nullptr},
          {"max", PropType::Int, "100", nullptr},
          {"format", PropType::String, "\"%d\"", nullptr},
          {"flags", PropType::Flags, "[]", "SliderFlags"},
          {"components", PropType::Int, "1", nullptr}},
         {{"value", kIntN, true}}, {"onChange"}, false, false, nullptr, 18500},
        {"VSliderFloat",
         {{"label", PropType::String, "\"VSlider\"", nullptr},
          {"min", PropType::Float, "0", nullptr},
          {"max", PropType::Float, "1", nullptr},
          {"format", PropType::String, "\"%.3f\"", nullptr},
          {"flags", PropType::Flags, "[]", "SliderFlags"},
          {"size", PropType::Size, kAuto, nullptr}},
         {{"value", "float", true}}, {"onChange"}, false, true, nullptr, 18500},
        {"VSliderInt",
         {{"label", PropType::String, "\"VSlider\"", nullptr},
          {"min", PropType::Int, "0", nullptr},
          {"max", PropType::Int, "100", nullptr},
          {"format", PropType::String, "\"%d\"", nullptr},
          {"flags", PropType::Flags, "[]", "SliderFlags"},
          {"size", PropType::Size, kAuto, nullptr}},
         {{"value", "int", true}}, {"onChange"}, false, true, nullptr, 18500},
        {"DragFloat",
         {{"label", PropType::String, "\"Drag\"", nullptr},
          {"speed", PropType::Float, "1", nullptr},
          {"min", PropType::Float, "0", nullptr},
          {"max", PropType::Float, "0", nullptr},
          {"format", PropType::String, "\"%.3f\"", nullptr},
          {"flags", PropType::Flags, "[]", "SliderFlags"},
          {"components", PropType::Int, "1", nullptr}},
         {{"value", kFloatN, true}}, {"onChange"}, false, false, nullptr, 18500},
        {"DragInt",
         {{"label", PropType::String, "\"Drag\"", nullptr},
          {"speed", PropType::Float, "1", nullptr},
          {"min", PropType::Int, "0", nullptr},
          {"max", PropType::Int, "0", nullptr},
          {"format", PropType::String, "\"%d\"", nullptr},
          {"flags", PropType::Flags, "[]", "SliderFlags"},
          {"components", PropType::Int, "1", nullptr}},
         {{"value", kIntN, true}}, {"onChange"}, false, false, nullptr, 18500},
        {"InputText",
         {{"label", PropType::String, "\"Input\"", nullptr},
          {"hint", PropType::String, "\"\"", nullptr},
          {"flags", PropType::Flags, "[]", "InputTextFlags"},
          {"maxLength", PropType::Int, "256", nullptr}},
         {{"value", kStr, true}}, {"onChange", "onEnter"}, false, false, nullptr, 18500},
        {"InputTextMultiline",
         {{"label", PropType::String, "\"Input\"", nullptr},
          {"flags", PropType::Flags, "[]", "InputTextFlags"},
          {"maxLength", PropType::Int, "256", nullptr},
          {"size", PropType::Size, kAuto, nullptr}},
         {{"value", kStr, true}}, {"onChange"}, false, true, nullptr, 18500},
        {"InputInt",
         {{"label", PropType::String, "\"Input\"", nullptr},
          {"step", PropType::Int, "1", nullptr},
          {"stepFast", PropType::Int, "100", nullptr},
          {"flags", PropType::Flags, "[]", "InputTextFlags"},
          {"components", PropType::Int, "1", nullptr}},
         {{"value", kIntN, true}}, {"onChange"}, false, false, nullptr, 18500},
        {"InputFloat",
         {{"label", PropType::String, "\"Input\"", nullptr},
          {"step", PropType::Float, "0", nullptr},
          {"stepFast", PropType::Float, "0", nullptr},
          {"format", PropType::String, "\"%.3f\"", nullptr},
          {"flags", PropType::Flags, "[]", "InputTextFlags"},
          {"components", PropType::Int, "1", nullptr}},
         {{"value", kFloatN, true}}, {"onChange"}, false, false, nullptr, 18500},
        {"ColorEdit",
         {{"label", PropType::String, "\"Color\"", nullptr},
          {"alpha", PropType::Bool, "true", nullptr},
          {"flags", PropType::Flags, "[]", "ColorEditFlags"}},
         {{"value", kCol, true}}, {"onChange"}, false, false, nullptr, 18500},
        {"ColorPicker",
         {{"label", PropType::String, "\"Color\"", nullptr},
          {"alpha", PropType::Bool, "true", nullptr},
          {"flags", PropType::Flags, "[]", "ColorEditFlags"}},
         {{"value", kCol, true}}, {"onChange"}, false, false, nullptr, 18500},
        {"ColorButton",
         {{"desc", PropType::String, "\"color\"", nullptr},
          {"color", PropType::Color, "\"#ffffffff\"", nullptr},
          {"flags", PropType::Flags, "[]", "ColorEditFlags"},
          {"size", PropType::Size, kAuto, nullptr}},
         {}, {"onClick"}, false, true, nullptr, 18500},
        {"ProgressBar",
         {{"fraction", PropType::Float, "0", nullptr},
          {"overlay", PropType::String, "null", nullptr},
          {"size", PropType::Size, kAuto, nullptr}},
         {{"value", "float", false}}, {}, false, true, nullptr, 18500},
        {"Image",
         {{"uv0", PropType::Vec2, R"({"x":0,"y":0})", nullptr},
          {"uv1", PropType::Vec2, R"({"x":1,"y":1})", nullptr},
          {"tint", PropType::Color, "\"#ffffffff\"", nullptr},
          {"border", PropType::Color, "\"#00000000\"", nullptr},
          {"size", PropType::Size, kAuto, nullptr}},
         {{"texture", "texture", true}}, {}, false, true, nullptr, 18500},
        {"ImageButton",
         {{"uv0", PropType::Vec2, R"({"x":0,"y":0})", nullptr},
          {"uv1", PropType::Vec2, R"({"x":1,"y":1})", nullptr},
          {"bg", PropType::Color, "\"#00000000\"", nullptr},
          {"tint", PropType::Color, "\"#ffffffff\"", nullptr},
          {"size", PropType::Size, kAuto, nullptr}},
         {{"texture", "texture", true}}, {"onClick"}, false, true, nullptr, 18500},
        {"Selectable",
         {{"label", PropType::String, "\"Selectable\"", nullptr},
          {"flags", PropType::Flags, "[]", "SelectableFlags"},
          {"size", PropType::Size, kAuto, nullptr}},
         {{"selected", "bool", false}}, {"onClick"}, false, true, nullptr, 18500},
        {"MenuItem",
         {{"label", PropType::String, "\"Item\"", nullptr},
          {"shortcut", PropType::String, "\"\"", nullptr},
          {"enabled", PropType::Bool, "true", nullptr}},
         {{"selected", "bool", false}}, {"onClick"}, false, false, nullptr, 18500},
        {"PlotLines",
         {{"label", PropType::String, "\"\"", nullptr},
          {"overlay", PropType::String, "null", nullptr},
          {"scaleMin", PropType::Float, "null", nullptr},
          {"scaleMax", PropType::Float, "null", nullptr},
          {"size", PropType::Size, kAuto, nullptr}},
         {{"values", "float[]|vector<float>", true}}, {}, false, true, nullptr, 18500},
        {"PlotHistogram",
         {{"label", PropType::String, "\"\"", nullptr},
          {"overlay", PropType::String, "null", nullptr},
          {"scaleMin", PropType::Float, "null", nullptr},
          {"scaleMax", PropType::Float, "null", nullptr},
          {"size", PropType::Size, kAuto, nullptr}},
         {{"values", "float[]|vector<float>", true}}, {}, false, true, nullptr, 18500},
    };
    return k;
}

Json J(const char* text) {
    Json j;
    REQUIRE(ParseJson(text, std::strlen(text), j, nullptr));
    return j;
}

bool CStrEq(const char* a, const char* b) {
    if (!a || !b) return a == b;
    return std::strcmp(a, b) == 0;
}

struct RealFlag {
    const char* name;
    int value;
};
struct RealTable {
    const char* enum_name;
    const RealFlag* flags;
    size_t count;
};

#define RF(E, N) {#N, static_cast<int>(ImGui##E##_##N)}

const RealFlag kRealWindow[] = {
    RF(WindowFlags, NoTitleBar), RF(WindowFlags, NoResize), RF(WindowFlags, NoMove),
    RF(WindowFlags, NoScrollbar), RF(WindowFlags, NoScrollWithMouse), RF(WindowFlags, NoCollapse),
    RF(WindowFlags, AlwaysAutoResize), RF(WindowFlags, NoBackground),
    RF(WindowFlags, NoSavedSettings), RF(WindowFlags, NoMouseInputs), RF(WindowFlags, MenuBar),
    RF(WindowFlags, HorizontalScrollbar), RF(WindowFlags, NoFocusOnAppearing),
    RF(WindowFlags, NoBringToFrontOnFocus), RF(WindowFlags, AlwaysVerticalScrollbar),
    RF(WindowFlags, AlwaysHorizontalScrollbar), RF(WindowFlags, NoNavInputs),
    RF(WindowFlags, NoNavFocus), RF(WindowFlags, UnsavedDocument), RF(WindowFlags, NoNav),
    RF(WindowFlags, NoDecoration), RF(WindowFlags, NoInputs),
};
#if IMGUI_VERSION_NUM >= 19000
const RealFlag kRealChild[] = {
#if IMGUI_VERSION_NUM >= 19110
    RF(ChildFlags, Borders),
#endif
    RF(ChildFlags, AlwaysUseWindowPadding), RF(ChildFlags, ResizeX), RF(ChildFlags, ResizeY),
    RF(ChildFlags, AutoResizeX), RF(ChildFlags, AutoResizeY), RF(ChildFlags, AlwaysAutoResize),
    RF(ChildFlags, FrameStyle),
#if IMGUI_VERSION_NUM >= 19090
    RF(ChildFlags, NavFlattened),
#endif
};
#endif
const RealFlag kRealInputText[] = {
    RF(InputTextFlags, CharsDecimal), RF(InputTextFlags, CharsHexadecimal),
    RF(InputTextFlags, CharsScientific), RF(InputTextFlags, CharsUppercase),
    RF(InputTextFlags, CharsNoBlank), RF(InputTextFlags, AllowTabInput),
    RF(InputTextFlags, EnterReturnsTrue),
#if IMGUI_VERSION_NUM >= 18900
    RF(InputTextFlags, EscapeClearsAll),
#endif
    RF(InputTextFlags, CtrlEnterForNewLine), RF(InputTextFlags, ReadOnly),
    RF(InputTextFlags, Password), RF(InputTextFlags, AlwaysOverwrite),
    RF(InputTextFlags, AutoSelectAll),
#if IMGUI_VERSION_NUM >= 19080
    RF(InputTextFlags, ParseEmptyRefVal), RF(InputTextFlags, DisplayEmptyRefVal),
#endif
    RF(InputTextFlags, NoHorizontalScroll), RF(InputTextFlags, NoUndoRedo),
#if IMGUI_VERSION_NUM >= 19160
    RF(InputTextFlags, ElideLeft),
#endif
    RF(InputTextFlags, CallbackCompletion), RF(InputTextFlags, CallbackHistory),
    RF(InputTextFlags, CallbackAlways), RF(InputTextFlags, CallbackCharFilter),
    RF(InputTextFlags, CallbackResize), RF(InputTextFlags, CallbackEdit),
#if IMGUI_VERSION_NUM >= 19230
    RF(InputTextFlags, WordWrap),
#endif
};
const RealFlag kRealTreeNode[] = {
    RF(TreeNodeFlags, Selected), RF(TreeNodeFlags, Framed),
#if IMGUI_VERSION_NUM >= 18970
    RF(TreeNodeFlags, AllowOverlap),
#endif
    RF(TreeNodeFlags, NoTreePushOnOpen), RF(TreeNodeFlags, NoAutoOpenOnLog),
    RF(TreeNodeFlags, DefaultOpen), RF(TreeNodeFlags, OpenOnDoubleClick),
    RF(TreeNodeFlags, OpenOnArrow), RF(TreeNodeFlags, Leaf), RF(TreeNodeFlags, Bullet),
    RF(TreeNodeFlags, FramePadding), RF(TreeNodeFlags, SpanAvailWidth),
    RF(TreeNodeFlags, SpanFullWidth),
#if IMGUI_VERSION_NUM >= 19170
    RF(TreeNodeFlags, SpanLabelWidth), RF(TreeNodeFlags, LabelSpanAllColumns),
#endif
#if IMGUI_VERSION_NUM >= 19000
    RF(TreeNodeFlags, SpanAllColumns),
#endif
#if IMGUI_VERSION_NUM >= 19200
    RF(TreeNodeFlags, NavLeftJumpsToParent), RF(TreeNodeFlags, DrawLinesNone),
    RF(TreeNodeFlags, DrawLinesFull), RF(TreeNodeFlags, DrawLinesToNodes),
#else
    RF(TreeNodeFlags, NavLeftJumpsBackHere),
#endif
    RF(TreeNodeFlags, CollapsingHeader),
};
const RealFlag kRealPopup[] = {
    RF(PopupFlags, MouseButtonLeft), RF(PopupFlags, MouseButtonRight), RF(PopupFlags, MouseButtonMiddle),
#if IMGUI_VERSION_NUM >= 19020
    RF(PopupFlags, NoReopen),
#endif
    RF(PopupFlags, NoOpenOverExistingPopup), RF(PopupFlags, NoOpenOverItems),
    RF(PopupFlags, AnyPopupId), RF(PopupFlags, AnyPopupLevel), RF(PopupFlags, AnyPopup),
};
const RealFlag kRealSelectable[] = {
#if IMGUI_VERSION_NUM >= 19100
    RF(SelectableFlags, NoAutoClosePopups), RF(SelectableFlags, Highlight),
#endif
    RF(SelectableFlags, SpanAllColumns), RF(SelectableFlags, AllowDoubleClick),
    RF(SelectableFlags, Disabled),
#if IMGUI_VERSION_NUM >= 18970
    RF(SelectableFlags, AllowOverlap),
#endif
#if IMGUI_VERSION_NUM >= 19230
    RF(SelectableFlags, SelectOnNav),
#endif
};
const RealFlag kRealCombo[] = {
    RF(ComboFlags, PopupAlignLeft), RF(ComboFlags, HeightSmall), RF(ComboFlags, HeightRegular),
    RF(ComboFlags, HeightLarge), RF(ComboFlags, HeightLargest), RF(ComboFlags, NoArrowButton),
    RF(ComboFlags, NoPreview),
#if IMGUI_VERSION_NUM >= 19000
    RF(ComboFlags, WidthFitPreview),
#endif
};
const RealFlag kRealTabBar[] = {
    RF(TabBarFlags, Reorderable), RF(TabBarFlags, AutoSelectNewTabs),
    RF(TabBarFlags, TabListPopupButton), RF(TabBarFlags, NoCloseWithMiddleMouseButton),
    RF(TabBarFlags, NoTabListScrollingButtons), RF(TabBarFlags, NoTooltip),
#if IMGUI_VERSION_NUM >= 19090
    RF(TabBarFlags, DrawSelectedOverline),
#endif
#if IMGUI_VERSION_NUM >= 19220
    RF(TabBarFlags, FittingPolicyMixed), RF(TabBarFlags, FittingPolicyShrink),
#else
    RF(TabBarFlags, FittingPolicyResizeDown),
#endif
    RF(TabBarFlags, FittingPolicyScroll),
};
const RealFlag kRealTabItem[] = {
    RF(TabItemFlags, UnsavedDocument), RF(TabItemFlags, SetSelected),
    RF(TabItemFlags, NoCloseWithMiddleMouseButton), RF(TabItemFlags, NoPushId),
    RF(TabItemFlags, NoTooltip), RF(TabItemFlags, NoReorder), RF(TabItemFlags, Leading),
    RF(TabItemFlags, Trailing),
#if IMGUI_VERSION_NUM >= 19010
    RF(TabItemFlags, NoAssumedClosure),
#endif
};
const RealFlag kRealColorEdit[] = {
    RF(ColorEditFlags, NoAlpha), RF(ColorEditFlags, NoPicker), RF(ColorEditFlags, NoOptions),
    RF(ColorEditFlags, NoSmallPreview), RF(ColorEditFlags, NoInputs), RF(ColorEditFlags, NoTooltip),
    RF(ColorEditFlags, NoLabel), RF(ColorEditFlags, NoSidePreview), RF(ColorEditFlags, NoDragDrop),
    RF(ColorEditFlags, NoBorder),
#if IMGUI_VERSION_NUM >= 19180
    RF(ColorEditFlags, AlphaOpaque), RF(ColorEditFlags, AlphaNoBg),
#endif
    RF(ColorEditFlags, AlphaPreviewHalf), RF(ColorEditFlags, AlphaBar), RF(ColorEditFlags, HDR),
    RF(ColorEditFlags, DisplayRGB), RF(ColorEditFlags, DisplayHSV), RF(ColorEditFlags, DisplayHex),
    RF(ColorEditFlags, Uint8), RF(ColorEditFlags, Float), RF(ColorEditFlags, PickerHueBar),
    RF(ColorEditFlags, PickerHueWheel), RF(ColorEditFlags, InputRGB), RF(ColorEditFlags, InputHSV),
#if IMGUI_VERSION_NUM >= 19260
    RF(ColorEditFlags, NoColorMarkers),
#endif
#if IMGUI_VERSION_NUM >= 19290
    RF(ColorEditFlags, PickerNoRotate),
#endif
};
const RealFlag kRealSlider[] = {
    RF(SliderFlags, Logarithmic), RF(SliderFlags, NoRoundToFormat), RF(SliderFlags, NoInput),
#if IMGUI_VERSION_NUM >= 19090
    RF(SliderFlags, WrapAround),
#endif
#if IMGUI_VERSION_NUM >= 19130
    RF(SliderFlags, ClampOnInput), RF(SliderFlags, ClampZeroRange),
#endif
#if IMGUI_VERSION_NUM >= 19170
    RF(SliderFlags, NoSpeedTweaks),
#endif
#if IMGUI_VERSION_NUM >= 19260
    RF(SliderFlags, ColorMarkers),
#endif
    RF(SliderFlags, AlwaysClamp),
};
const RealFlag kRealTable[] = {
    RF(TableFlags, Resizable), RF(TableFlags, Reorderable), RF(TableFlags, Hideable),
    RF(TableFlags, Sortable), RF(TableFlags, NoSavedSettings), RF(TableFlags, ContextMenuInBody),
    RF(TableFlags, RowBg), RF(TableFlags, BordersInnerH), RF(TableFlags, BordersOuterH),
    RF(TableFlags, BordersInnerV), RF(TableFlags, BordersOuterV), RF(TableFlags, BordersH),
    RF(TableFlags, BordersV), RF(TableFlags, BordersInner), RF(TableFlags, BordersOuter),
    RF(TableFlags, Borders), RF(TableFlags, NoBordersInBody),
    RF(TableFlags, NoBordersInBodyUntilResize), RF(TableFlags, SizingFixedFit),
    RF(TableFlags, SizingFixedSame), RF(TableFlags, SizingStretchProp),
    RF(TableFlags, SizingStretchSame), RF(TableFlags, NoHostExtendX), RF(TableFlags, NoHostExtendY),
    RF(TableFlags, NoKeepColumnsVisible), RF(TableFlags, PreciseWidths), RF(TableFlags, NoClip),
    RF(TableFlags, PadOuterX), RF(TableFlags, NoPadOuterX), RF(TableFlags, NoPadInnerX),
    RF(TableFlags, ScrollX), RF(TableFlags, ScrollY), RF(TableFlags, SortMulti),
    RF(TableFlags, SortTristate),
#if IMGUI_VERSION_NUM >= 19000
    RF(TableFlags, HighlightHoveredColumn),
#endif
};
const RealFlag kRealCond[] = {RF(Cond, Always), RF(Cond, Once), RF(Cond, FirstUseEver),
                              RF(Cond, Appearing)};
const RealFlag kRealDir[] = {RF(Dir, Left), RF(Dir, Right), RF(Dir, Up), RF(Dir, Down)};

#define RT(NAME, ARR) {NAME, ARR, sizeof(ARR) / sizeof(ARR[0])}
const RealTable kRealTables[] = {
    RT("WindowFlags", kRealWindow),
#if IMGUI_VERSION_NUM >= 19000
    RT("ChildFlags", kRealChild),
#endif
    RT("InputTextFlags", kRealInputText),
    RT("TreeNodeFlags", kRealTreeNode),
    RT("PopupFlags", kRealPopup),
    RT("SelectableFlags", kRealSelectable),
    RT("ComboFlags", kRealCombo),
    RT("TabBarFlags", kRealTabBar),
    RT("TabItemFlags", kRealTabItem),
    RT("ColorEditFlags", kRealColorEdit),
    RT("SliderFlags", kRealSlider),
    RT("TableFlags", kRealTable),
    RT("Cond", kRealCond),
    RT("Dir", kRealDir),
};

struct RealCol {
    const char* name;
    int index;
    bool vec2;
};
#define RC(N) {#N, static_cast<int>(ImGuiCol_##N), false}
#define RS(N, V) {#N, static_cast<int>(ImGuiStyleVar_##N), V}

const RealCol kRealCols[] = {
    RC(Text), RC(TextDisabled), RC(WindowBg), RC(ChildBg), RC(PopupBg), RC(Border),
    RC(BorderShadow), RC(FrameBg), RC(FrameBgHovered), RC(FrameBgActive), RC(TitleBg),
    RC(TitleBgActive), RC(TitleBgCollapsed), RC(MenuBarBg), RC(ScrollbarBg), RC(ScrollbarGrab),
    RC(ScrollbarGrabHovered), RC(ScrollbarGrabActive), RC(CheckMark), RC(SliderGrab),
    RC(SliderGrabActive), RC(Button), RC(ButtonHovered), RC(ButtonActive), RC(Header),
    RC(HeaderHovered), RC(HeaderActive), RC(Separator), RC(SeparatorHovered), RC(SeparatorActive),
    RC(ResizeGrip), RC(ResizeGripHovered), RC(ResizeGripActive), RC(TabHovered), RC(Tab),
#if IMGUI_VERSION_NUM >= 19090
    RC(TabSelected), RC(TabSelectedOverline), RC(TabDimmed), RC(TabDimmedSelected),
    RC(TabDimmedSelectedOverline),
#endif
    RC(PlotLines), RC(PlotLinesHovered), RC(PlotHistogram), RC(PlotHistogramHovered),
    RC(TableHeaderBg), RC(TableBorderStrong), RC(TableBorderLight), RC(TableRowBg),
    RC(TableRowBgAlt),
#if IMGUI_VERSION_NUM >= 19100
    RC(TextLink),
#endif
    RC(TextSelectedBg), RC(DragDropTarget),
#if IMGUI_VERSION_NUM >= 19140
    RC(NavCursor),
#endif
    RC(NavWindowingHighlight), RC(NavWindowingDimBg), RC(ModalWindowDimBg),
#if IMGUI_VERSION_NUM >= 19200
    RC(InputTextCursor), RC(TreeLines),
#endif
#if IMGUI_VERSION_NUM >= 19240
    RC(UnsavedMarker),
#endif
#if IMGUI_VERSION_NUM >= 19250
    RC(DragDropTargetBg),
#endif
#if IMGUI_VERSION_NUM >= 19280
    RC(CheckboxSelectedBg),
#endif
};

const RealCol kRealStyleVars[] = {
    RS(Alpha, false), RS(DisabledAlpha, false), RS(WindowPadding, true), RS(WindowRounding, false),
    RS(WindowBorderSize, false), RS(WindowMinSize, true), RS(WindowTitleAlign, true),
    RS(ChildRounding, false), RS(ChildBorderSize, false), RS(PopupRounding, false),
    RS(PopupBorderSize, false), RS(FramePadding, true), RS(FrameRounding, false),
    RS(FrameBorderSize, false), RS(ItemSpacing, true), RS(ItemInnerSpacing, true),
    RS(IndentSpacing, false), RS(CellPadding, true), RS(ScrollbarSize, false),
    RS(ScrollbarRounding, false), RS(GrabMinSize, false), RS(GrabRounding, false),
#if IMGUI_VERSION_NUM >= 19190
    RS(ImageBorderSize, false),
#endif
    RS(TabRounding, false),
#if IMGUI_VERSION_NUM >= 19050
    RS(TabBorderSize, false),
#endif
#if IMGUI_VERSION_NUM >= 19000
    RS(TabBarBorderSize, false), RS(TableAngledHeadersAngle, false),
#endif
#if IMGUI_VERSION_NUM >= 19100
    RS(TabBarOverlineSize, false),
#endif
#if IMGUI_VERSION_NUM >= 19060
    RS(TableAngledHeadersTextAlign, true),
#endif
    RS(ButtonTextAlign, true), RS(SelectableTextAlign, true),
#if IMGUI_VERSION_NUM >= 18950
    RS(SeparatorTextBorderSize, false), RS(SeparatorTextAlign, true),
    RS(SeparatorTextPadding, true),
#endif
#if IMGUI_VERSION_NUM >= 19200
    RS(TreeLinesSize, false), RS(TreeLinesRounding, false),
#endif
#if IMGUI_VERSION_NUM >= 19220
    RS(TabMinWidthBase, false), RS(TabMinWidthShrink, false),
#endif
#if IMGUI_VERSION_NUM >= 19230
    RS(ScrollbarPadding, false),
#endif
#if IMGUI_VERSION_NUM >= 19260
    RS(ImageRounding, false),
#endif
#if IMGUI_VERSION_NUM >= 19270
    RS(SeparatorSize, false),
#endif
#if IMGUI_VERSION_NUM >= 19280
    RS(DragDropTargetRounding, false),
#endif
#if IMGUI_VERSION_NUM >= 19290
    RS(MenuItemRounding, false), RS(SelectableRounding, false),
#endif
};

const RealCol* FindReal(const RealCol* t, size_t n, const char* name) {
    for (size_t i = 0; i < n; ++i)
        if (std::strcmp(t[i].name, name) == 0) return &t[i];
    return nullptr;
}

const char* const kToggleManifest = R"json({
  "schemaVersion": 1, "kind": "widget", "name": "Toggle", "category": "Essentials",
  "include": "imb_essentials.h", "minImGuiVersion": 18500,
  "props": [ { "name": "label", "type": "string", "default": "Toggle" },
             { "name": "size", "type": "size", "default": {"w":"auto","h":"auto"} },
             { "name": "onColor", "type": "color", "default": "#2ecc71ff" } ],
  "bindings": [ { "name": "value", "types": ["bool"], "required": true } ],
  "events": [ "onChange" ],
  "container": false,
  "call": "imb::ess::Toggle({label}, {value}, {size}, {onColor})",
  "returns": "changed",
  "preview": [ { "shape": "rect", "rounding": 10, "fill": "{onColor}", "w": 40, "h": 20 },
               { "shape": "circle", "fill": "#ffffffff", "r": 8, "x": 30, "y": 10 } ]
})json";

}  // namespace
