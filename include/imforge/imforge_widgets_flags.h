#pragma once

#include <cstddef>
#include <cstring>

#include "imforge_widgets_defs.h"

// With imgui.h included first, anything added/renamed/renumbered after 1.91.9 follows the running
// imgui. Otherwise (or with IMFORGE_CATALOG_ALL_VERSIONS) you get the superset of every supported
// release, newest values, for tools that target several versions. The inline namespace keeps the two
// variants apart if one program mixes them.
#if defined(IMGUI_VERSION_NUM) && !defined(IMFORGE_CATALOG_ALL_VERSIONS)
#define IMFORGE_CATALOG_NUM IMGUI_VERSION_NUM
#define IMFORGE_CATALOG_ALL 0
#define IMFORGE_CATALOG_NS_PASTE(v) catalog_##v
#define IMFORGE_CATALOG_NS_EXPAND(v) IMFORGE_CATALOG_NS_PASTE(v)
#define IMFORGE_CATALOG_NS IMFORGE_CATALOG_NS_EXPAND(IMGUI_VERSION_NUM)
#else
#define IMFORGE_CATALOG_NUM 99999
#define IMFORGE_CATALOG_ALL 1
#define IMFORGE_CATALOG_NS catalog_all
#endif
#define IMFORGE_CATALOG_HAS(v) (IMFORGE_CATALOG_NUM >= (v))
#define IMFORGE_CATALOG_BEFORE(v) (IMFORGE_CATALOG_ALL || IMFORGE_CATALOG_NUM < (v))
#define IMFORGE_CATALOG_PICK(v, new_value, old_value) (IMFORGE_CATALOG_HAS(v) ? (new_value) : (old_value))

namespace imforge {
namespace detail {

inline const char* StripImGuiPrefix(const char* name) {
    if (name && std::strncmp(name, "ImGui", 5) == 0) return name + 5;
    return name;
}

}  // namespace detail

inline namespace IMFORGE_CATALOG_NS {

struct FlagRename {
    const char* enum_name;
    const char* old_name;
    const char* new_name;
    int since;
};

namespace catalog_detail {

inline constexpr FlagDef kWindowFlags[] = {
    {"NoTitleBar", 1 << 0, 18500},
    {"NoResize", 1 << 1, 18500},
    {"NoMove", 1 << 2, 18500},
    {"NoScrollbar", 1 << 3, 18500},
    {"NoScrollWithMouse", 1 << 4, 18500},
    {"NoCollapse", 1 << 5, 18500},
    {"AlwaysAutoResize", 1 << 6, 18500},
    {"NoBackground", 1 << 7, 18500},
    {"NoSavedSettings", 1 << 8, 18500},
    {"NoMouseInputs", 1 << 9, 18500},
    {"MenuBar", 1 << 10, 18500},
    {"HorizontalScrollbar", 1 << 11, 18500},
    {"NoFocusOnAppearing", 1 << 12, 18500},
    {"NoBringToFrontOnFocus", 1 << 13, 18500},
    {"AlwaysVerticalScrollbar", 1 << 14, 18500},
    {"AlwaysHorizontalScrollbar", 1 << 15, 18500},
    {"NoNavInputs", 1 << 16, 18500},
    {"NoNavFocus", 1 << 17, 18500},
    {"UnsavedDocument", 1 << 18, 18500},
    {"NoNav", (1 << 16) | (1 << 17), 18500},
    {"NoDecoration", (1 << 0) | (1 << 1) | (1 << 3) | (1 << 5), 18500},
    {"NoInputs", (1 << 9) | (1 << 16) | (1 << 17), 18500},
};

inline constexpr FlagDef kChildFlags[] = {
    {"Borders", 1 << 0, 19110},
    {"AlwaysUseWindowPadding", 1 << 1, 19000},
    {"ResizeX", 1 << 2, 19000},
    {"ResizeY", 1 << 3, 19000},
    {"AutoResizeX", 1 << 4, 19000},
    {"AutoResizeY", 1 << 5, 19000},
    {"AlwaysAutoResize", 1 << 6, 19000},
    {"FrameStyle", 1 << 7, 19000},
    {"NavFlattened", 1 << 8, 19090},
};

inline constexpr FlagDef kInputTextFlags[] = {
    {"CharsDecimal", 1 << 0, 18500},
    {"CharsHexadecimal", 1 << 1, 18500},
    {"CharsScientific", 1 << 2, 18500},
    {"CharsUppercase", 1 << 3, 18500},
    {"CharsNoBlank", 1 << 4, 18500},
    {"AllowTabInput", 1 << 5, 18500},
    {"EnterReturnsTrue", 1 << 6, 18500},
    {"EscapeClearsAll", 1 << 7, 18900},
    {"CtrlEnterForNewLine", 1 << 8, 18500},
    {"ReadOnly", 1 << 9, 18500},
    {"Password", 1 << 10, 18500},
    {"AlwaysOverwrite", 1 << 11, 18500},
    {"AutoSelectAll", 1 << 12, 18500},
    {"ParseEmptyRefVal", 1 << 13, 19080},
    {"DisplayEmptyRefVal", 1 << 14, 19080},
    {"NoHorizontalScroll", 1 << 15, 18500},
    {"NoUndoRedo", 1 << 16, 18500},
    {"ElideLeft", 1 << 17, 19160},
    {"CallbackCompletion", 1 << 18, 18500},
    {"CallbackHistory", 1 << 19, 18500},
    {"CallbackAlways", 1 << 20, 18500},
    {"CallbackCharFilter", 1 << 21, 18500},
    {"CallbackResize", 1 << 22, 18500},
    {"CallbackEdit", 1 << 23, 18500},
#if IMFORGE_CATALOG_HAS(19230)
    {"WordWrap", 1 << 24, 19230},
#endif
};

inline constexpr FlagDef kTreeNodeFlags[] = {
    {"Selected", 1 << 0, 18500},
    {"Framed", 1 << 1, 18500},
    {"AllowOverlap", 1 << 2, 18970},
    {"NoTreePushOnOpen", 1 << 3, 18500},
    {"NoAutoOpenOnLog", 1 << 4, 18500},
    {"DefaultOpen", 1 << 5, 18500},
    {"OpenOnDoubleClick", 1 << 6, 18500},
    {"OpenOnArrow", 1 << 7, 18500},
    {"Leaf", 1 << 8, 18500},
    {"Bullet", 1 << 9, 18500},
    {"FramePadding", 1 << 10, 18500},
    {"SpanAvailWidth", 1 << 11, 18500},
    {"SpanFullWidth", 1 << 12, 18500},
    {"SpanLabelWidth", 1 << 13, 19170},
    {"SpanAllColumns", 1 << 14, 19000},
    {"LabelSpanAllColumns", 1 << 15, 19170},
#if IMFORGE_CATALOG_BEFORE(19200)
    {"NavLeftJumpsBackHere", 1 << 17, 18500},
#endif
#if IMFORGE_CATALOG_HAS(19200)
    {"NavLeftJumpsToParent", 1 << 17, 19200},
    {"DrawLinesNone", 1 << 18, 19200},
    {"DrawLinesFull", 1 << 19, 19200},
    {"DrawLinesToNodes", 1 << 20, 19200},
#endif
    {"CollapsingHeader", (1 << 1) | (1 << 3) | (1 << 4), 18500},
};

inline constexpr FlagDef kPopupFlags[] = {
    {"MouseButtonLeft", IMFORGE_CATALOG_PICK(19260, 1 << 2, 0), 18500},
    {"MouseButtonRight", IMFORGE_CATALOG_PICK(19260, 2 << 2, 1), 18500},
    {"MouseButtonMiddle", IMFORGE_CATALOG_PICK(19260, 3 << 2, 2), 18500},
    {"NoReopen", 1 << 5, 19020},
    {"NoOpenOverExistingPopup", 1 << 7, 18500},
    {"NoOpenOverItems", 1 << 8, 18500},
    {"AnyPopupId", 1 << 10, 18500},
    {"AnyPopupLevel", 1 << 11, 18500},
    {"AnyPopup", (1 << 10) | (1 << 11), 18500},
};

inline constexpr FlagDef kSelectableFlags[] = {
    {"NoAutoClosePopups", 1 << 0, 19100},
    {"SpanAllColumns", 1 << 1, 18500},
    {"AllowDoubleClick", 1 << 2, 18500},
    {"Disabled", 1 << 3, 18500},
    {"AllowOverlap", 1 << 4, 18970},
    {"Highlight", 1 << 5, 19100},
#if IMFORGE_CATALOG_HAS(19230)
    {"SelectOnNav", 1 << 6, 19230},
#endif
};

inline constexpr FlagDef kComboFlags[] = {
    {"PopupAlignLeft", 1 << 0, 18500},
    {"HeightSmall", 1 << 1, 18500},
    {"HeightRegular", 1 << 2, 18500},
    {"HeightLarge", 1 << 3, 18500},
    {"HeightLargest", 1 << 4, 18500},
    {"NoArrowButton", 1 << 5, 18500},
    {"NoPreview", 1 << 6, 18500},
    {"WidthFitPreview", 1 << 7, 19000},
};

inline constexpr FlagDef kTabBarFlags[] = {
    {"Reorderable", 1 << 0, 18500},
    {"AutoSelectNewTabs", 1 << 1, 18500},
    {"TabListPopupButton", 1 << 2, 18500},
    {"NoCloseWithMiddleMouseButton", 1 << 3, 18500},
    {"NoTabListScrollingButtons", 1 << 4, 18500},
    {"NoTooltip", 1 << 5, 18500},
    {"DrawSelectedOverline", 1 << 6, 19090},
#if IMFORGE_CATALOG_BEFORE(19220)
    {"FittingPolicyResizeDown", 1 << 7, 18500},
#endif
#if IMFORGE_CATALOG_HAS(19220)
    {"FittingPolicyMixed", 1 << 7, 19220},
    {"FittingPolicyShrink", 1 << 8, 19220},
#endif
    {"FittingPolicyScroll", IMFORGE_CATALOG_PICK(19220, 1 << 9, 1 << 8), 18500},
};

inline constexpr FlagDef kTabItemFlags[] = {
    {"UnsavedDocument", 1 << 0, 18500},
    {"SetSelected", 1 << 1, 18500},
    {"NoCloseWithMiddleMouseButton", 1 << 2, 18500},
    {"NoPushId", 1 << 3, 18500},
    {"NoTooltip", 1 << 4, 18500},
    {"NoReorder", 1 << 5, 18500},
    {"Leading", 1 << 6, 18500},
    {"Trailing", 1 << 7, 18500},
    {"NoAssumedClosure", 1 << 8, 19010},
};

inline constexpr FlagDef kColorEditFlags[] = {
    {"NoAlpha", 1 << 1, 18500},
    {"NoPicker", 1 << 2, 18500},
    {"NoOptions", 1 << 3, 18500},
    {"NoSmallPreview", 1 << 4, 18500},
    {"NoInputs", 1 << 5, 18500},
    {"NoTooltip", 1 << 6, 18500},
    {"NoLabel", 1 << 7, 18500},
    {"NoSidePreview", 1 << 8, 18500},
    {"NoDragDrop", 1 << 9, 18500},
    {"NoBorder", 1 << 10, 18500},
#if IMFORGE_CATALOG_HAS(19260)
    {"NoColorMarkers", 1 << 11, 19260},
#endif
    {"AlphaOpaque", IMFORGE_CATALOG_PICK(19260, 1 << 12, 1 << 11), 19180},
    {"AlphaNoBg", IMFORGE_CATALOG_PICK(19260, 1 << 13, 1 << 12), 19180},
    {"AlphaPreviewHalf", IMFORGE_CATALOG_PICK(19260, 1 << 14, 1 << 13), 18500},
    {"AlphaBar", IMFORGE_CATALOG_PICK(19260, 1 << 18, 1 << 16), 18500},
    {"HDR", 1 << 19, 18500},
    {"DisplayRGB", 1 << 20, 18500},
    {"DisplayHSV", 1 << 21, 18500},
    {"DisplayHex", 1 << 22, 18500},
    {"Uint8", 1 << 23, 18500},
    {"Float", 1 << 24, 18500},
    {"PickerHueBar", 1 << 25, 18500},
    {"PickerHueWheel", 1 << 26, 18500},
#if IMFORGE_CATALOG_HAS(19290)
    {"PickerNoRotate", 1 << 27, 19290},
#endif
    {"InputRGB", IMFORGE_CATALOG_PICK(19290, 1 << 28, 1 << 27), 18500},
    {"InputHSV", IMFORGE_CATALOG_PICK(19290, 1 << 29, 1 << 28), 18500},
};

inline constexpr FlagDef kSliderFlags[] = {
    {"Logarithmic", 1 << 5, 18500},
    {"NoRoundToFormat", 1 << 6, 18500},
    {"NoInput", 1 << 7, 18500},
    {"WrapAround", 1 << 8, 19090},
    {"ClampOnInput", 1 << 9, 19130},
    {"ClampZeroRange", 1 << 10, 19130},
    {"NoSpeedTweaks", 1 << 11, 19170},
#if IMFORGE_CATALOG_HAS(19260)
    {"ColorMarkers", 1 << 12, 19260},
#endif
    {"AlwaysClamp", (1 << 9) | (1 << 10), 18500},
};

inline constexpr FlagDef kTableFlags[] = {
    {"Resizable", 1 << 0, 18500},
    {"Reorderable", 1 << 1, 18500},
    {"Hideable", 1 << 2, 18500},
    {"Sortable", 1 << 3, 18500},
    {"NoSavedSettings", 1 << 4, 18500},
    {"ContextMenuInBody", 1 << 5, 18500},
    {"RowBg", 1 << 6, 18500},
    {"BordersInnerH", 1 << 7, 18500},
    {"BordersOuterH", 1 << 8, 18500},
    {"BordersInnerV", 1 << 9, 18500},
    {"BordersOuterV", 1 << 10, 18500},
    {"BordersH", (1 << 7) | (1 << 8), 18500},
    {"BordersV", (1 << 9) | (1 << 10), 18500},
    {"BordersInner", (1 << 9) | (1 << 7), 18500},
    {"BordersOuter", (1 << 10) | (1 << 8), 18500},
    {"Borders", (1 << 7) | (1 << 8) | (1 << 9) | (1 << 10), 18500},
    {"NoBordersInBody", 1 << 11, 18500},
    {"NoBordersInBodyUntilResize", 1 << 12, 18500},
    {"SizingFixedFit", 1 << 13, 18500},
    {"SizingFixedSame", 2 << 13, 18500},
    {"SizingStretchProp", 3 << 13, 18500},
    {"SizingStretchSame", 4 << 13, 18500},
    {"NoHostExtendX", 1 << 16, 18500},
    {"NoHostExtendY", 1 << 17, 18500},
    {"NoKeepColumnsVisible", 1 << 18, 18500},
    {"PreciseWidths", 1 << 19, 18500},
    {"NoClip", 1 << 20, 18500},
    {"PadOuterX", 1 << 21, 18500},
    {"NoPadOuterX", 1 << 22, 18500},
    {"NoPadInnerX", 1 << 23, 18500},
    {"ScrollX", 1 << 24, 18500},
    {"ScrollY", 1 << 25, 18500},
    {"SortMulti", 1 << 26, 18500},
    {"SortTristate", 1 << 27, 18500},
    {"HighlightHoveredColumn", 1 << 28, 19000},
};

inline constexpr FlagDef kCondFlags[] = {
    {"Always", 1 << 0, 18500},
    {"Once", 1 << 1, 18500},
    {"FirstUseEver", 1 << 2, 18500},
    {"Appearing", 1 << 3, 18500},
};

inline constexpr FlagDef kDirFlags[] = {
    {"Left", 0, 18500},
    {"Right", 1, 18500},
    {"Up", 2, 18500},
    {"Down", 3, 18500},
};

struct FlagTable {
    const char* enum_name;
    const FlagDef* flags;
    size_t count;
};

template <size_t N>
constexpr FlagTable MakeFlagTable(const char* name, const FlagDef (&a)[N]) {
    return FlagTable{name, a, N};
}

inline constexpr FlagTable kFlagTables[] = {
    MakeFlagTable("WindowFlags", kWindowFlags),
    MakeFlagTable("ChildFlags", kChildFlags),
    MakeFlagTable("TableFlags", kTableFlags),
    MakeFlagTable("TabBarFlags", kTabBarFlags),
    MakeFlagTable("TabItemFlags", kTabItemFlags),
    MakeFlagTable("TreeNodeFlags", kTreeNodeFlags),
    MakeFlagTable("ComboFlags", kComboFlags),
    MakeFlagTable("SliderFlags", kSliderFlags),
    MakeFlagTable("InputTextFlags", kInputTextFlags),
    MakeFlagTable("ColorEditFlags", kColorEditFlags),
    MakeFlagTable("SelectableFlags", kSelectableFlags),
    MakeFlagTable("PopupFlags", kPopupFlags),
    MakeFlagTable("Cond", kCondFlags),
    MakeFlagTable("Dir", kDirFlags),
};

struct StyleEntry {
    const char* name;
    int min_version;
    bool vec2;
};

inline constexpr StyleEntry kCols[] = {
    {"Text", 18500, false}, {"TextDisabled", 18500, false}, {"WindowBg", 18500, false},
    {"ChildBg", 18500, false}, {"PopupBg", 18500, false}, {"Border", 18500, false},
    {"BorderShadow", 18500, false}, {"FrameBg", 18500, false}, {"FrameBgHovered", 18500, false},
    {"FrameBgActive", 18500, false}, {"TitleBg", 18500, false}, {"TitleBgActive", 18500, false},
    {"TitleBgCollapsed", 18500, false}, {"MenuBarBg", 18500, false}, {"ScrollbarBg", 18500, false},
    {"ScrollbarGrab", 18500, false}, {"ScrollbarGrabHovered", 18500, false},
    {"ScrollbarGrabActive", 18500, false}, {"CheckMark", 18500, false},
#if IMFORGE_CATALOG_HAS(19280)
    {"CheckboxSelectedBg", 19280, false},
#endif
    {"SliderGrab", 18500, false}, {"SliderGrabActive", 18500, false}, {"Button", 18500, false},
    {"ButtonHovered", 18500, false}, {"ButtonActive", 18500, false}, {"Header", 18500, false},
    {"HeaderHovered", 18500, false}, {"HeaderActive", 18500, false}, {"Separator", 18500, false},
    {"SeparatorHovered", 18500, false}, {"SeparatorActive", 18500, false},
    {"ResizeGrip", 18500, false}, {"ResizeGripHovered", 18500, false},
    {"ResizeGripActive", 18500, false},
#if IMFORGE_CATALOG_HAS(19200)
    {"InputTextCursor", 19200, false},
#endif
    {"TabHovered", 18500, false}, {"Tab", 18500, false},
    {"TabSelected", 19090, false}, {"TabSelectedOverline", 19090, false},
    {"TabDimmed", 19090, false}, {"TabDimmedSelected", 19090, false},
    {"TabDimmedSelectedOverline", 19090, false}, {"PlotLines", 18500, false},
    {"PlotLinesHovered", 18500, false}, {"PlotHistogram", 18500, false},
    {"PlotHistogramHovered", 18500, false}, {"TableHeaderBg", 18500, false},
    {"TableBorderStrong", 18500, false}, {"TableBorderLight", 18500, false},
    {"TableRowBg", 18500, false}, {"TableRowBgAlt", 18500, false}, {"TextLink", 19100, false},
    {"TextSelectedBg", 18500, false},
#if IMFORGE_CATALOG_HAS(19200)
    {"TreeLines", 19200, false},
#endif
    {"DragDropTarget", 18500, false},
#if IMFORGE_CATALOG_HAS(19250)
    {"DragDropTargetBg", 19250, false},
#endif
#if IMFORGE_CATALOG_HAS(19240)
    {"UnsavedMarker", 19240, false},
#endif
    {"NavCursor", 19140, false}, {"NavWindowingHighlight", 18500, false},
    {"NavWindowingDimBg", 18500, false}, {"ModalWindowDimBg", 18500, false},
};

inline constexpr StyleEntry kColsDocking[] = {
    {"DockingPreview", 18500, false}, {"DockingEmptyBg", 18500, false},
};

inline constexpr StyleEntry kStyleVars[] = {
    {"Alpha", 18500, false}, {"DisabledAlpha", 18500, false}, {"WindowPadding", 18500, true},
    {"WindowRounding", 18500, false}, {"WindowBorderSize", 18500, false},
    {"WindowMinSize", 18500, true}, {"WindowTitleAlign", 18500, true},
    {"ChildRounding", 18500, false}, {"ChildBorderSize", 18500, false},
    {"PopupRounding", 18500, false}, {"PopupBorderSize", 18500, false},
    {"FramePadding", 18500, true}, {"FrameRounding", 18500, false},
    {"FrameBorderSize", 18500, false}, {"ItemSpacing", 18500, true},
    {"ItemInnerSpacing", 18500, true}, {"IndentSpacing", 18500, false},
    {"CellPadding", 18500, true}, {"ScrollbarSize", 18500, false},
    {"ScrollbarRounding", 18500, false},
#if IMFORGE_CATALOG_HAS(19230)
    {"ScrollbarPadding", 19230, false},
#endif
    {"GrabMinSize", 18500, false}, {"GrabRounding", 18500, false},
#if IMFORGE_CATALOG_HAS(19260)
    {"ImageRounding", 19260, false},
#endif
    {"ImageBorderSize", 19190, false}, {"TabRounding", 18500, false},
    {"TabBorderSize", 19050, false},
#if IMFORGE_CATALOG_HAS(19220)
    {"TabMinWidthBase", 19220, false}, {"TabMinWidthShrink", 19220, false},
#endif
    {"TabBarBorderSize", 19000, false}, {"TabBarOverlineSize", 19100, false},
    {"TableAngledHeadersAngle", 19000, false}, {"TableAngledHeadersTextAlign", 19060, true},
#if IMFORGE_CATALOG_HAS(19200)
    {"TreeLinesSize", 19200, false}, {"TreeLinesRounding", 19200, false},
#endif
#if IMFORGE_CATALOG_HAS(19290)
    {"MenuItemRounding", 19290, false}, {"SelectableRounding", 19290, false},
#endif
#if IMFORGE_CATALOG_HAS(19280)
    {"DragDropTargetRounding", 19280, false},
#endif
    {"ButtonTextAlign", 18500, true}, {"SelectableTextAlign", 18500, true},
#if IMFORGE_CATALOG_HAS(19270)
    {"SeparatorSize", 19270, false},
#endif
    {"SeparatorTextBorderSize", 18950, false}, {"SeparatorTextAlign", 18950, true},
    {"SeparatorTextPadding", 18950, true},
};

inline constexpr StyleEntry kStyleVarsDocking[] = {
    {"DockingSeparatorSize", 19000, false},
};

struct ColAlias {
    const char* old_name;
    const char* new_name;
};

inline constexpr ColAlias kColAliases[] = {
    {"TabActive", "TabSelected"},
    {"TabUnfocused", "TabDimmed"},
    {"TabUnfocusedActive", "TabDimmedSelected"},
    {"NavHighlight", "NavCursor"},
};

inline constexpr FlagRename kFlagRenames[] = {
    {"TreeNodeFlags", "NavLeftJumpsBackHere", "NavLeftJumpsToParent", 19200},
    {"TabBarFlags", "FittingPolicyResizeDown", "FittingPolicyShrink", 19220},
};

template <size_t N>
struct NameList {
    const char* names[N];
};

template <size_t N>
inline NameList<N> MakeNames(const StyleEntry (&table)[N]) {
    NameList<N> out{};
    for (size_t i = 0; i < N; ++i) out.names[i] = table[i].name;
    return out;
}

template <size_t N>
inline NameList<N> MakeNames(const FlagTable (&table)[N]) {
    NameList<N> out{};
    for (size_t i = 0; i < N; ++i) out.names[i] = table[i].enum_name;
    return out;
}

template <size_t N>
inline const StyleEntry* FindEntry(const StyleEntry (&table)[N], const char* name) {
    if (!name) return nullptr;
    for (const StyleEntry& e : table)
        if (std::strcmp(e.name, name) == 0) return &e;
    return nullptr;
}

}  // namespace catalog_detail

inline const FlagDef* FlagsFor(const char* enum_name, size_t& count) {
    count = 0;
    enum_name = detail::StripImGuiPrefix(enum_name);
    if (!enum_name) return nullptr;
    for (const catalog_detail::FlagTable& t : catalog_detail::kFlagTables) {
        if (std::strcmp(t.enum_name, enum_name) == 0) {
            count = t.count;
            return t.flags;
        }
    }
    return nullptr;
}

inline const FlagDef* FindFlag(const char* enum_name, const char* flag_name) {
    size_t n = 0;
    const FlagDef* f = FlagsFor(enum_name, n);
    if (!f || !flag_name) return nullptr;
    for (size_t i = 0; i < n; ++i)
        if (std::strcmp(f[i].name, flag_name) == 0) return &f[i];
    return nullptr;
}

inline const char* const* FlagEnumNames(size_t& count) {
    static const auto names = catalog_detail::MakeNames(catalog_detail::kFlagTables);
    count = sizeof(catalog_detail::kFlagTables) / sizeof(catalog_detail::kFlagTables[0]);
    return names.names;
}

inline const char* const* ImGuiCondNames(size_t& count) {
    static const char* const names[] = {"Always", "Once", "FirstUseEver", "Appearing"};
    count = 4;
    return names;
}

inline const char* const* ImGuiDirNames(size_t& count) {
    static const char* const names[] = {"Left", "Right", "Up", "Down"};
    count = 4;
    return names;
}

inline const char* const* ImGuiColNames(size_t& count) {
    static const auto names = catalog_detail::MakeNames(catalog_detail::kCols);
    count = sizeof(catalog_detail::kCols) / sizeof(catalog_detail::kCols[0]);
    return names.names;
}

inline const char* const* ImGuiColNamesDocking(size_t& count) {
    static const auto names = catalog_detail::MakeNames(catalog_detail::kColsDocking);
    count = sizeof(catalog_detail::kColsDocking) / sizeof(catalog_detail::kColsDocking[0]);
    return names.names;
}

inline const char* const* ImGuiStyleVarNames(size_t& count) {
    static const auto names = catalog_detail::MakeNames(catalog_detail::kStyleVars);
    count = sizeof(catalog_detail::kStyleVars) / sizeof(catalog_detail::kStyleVars[0]);
    return names.names;
}

inline const char* const* ImGuiStyleVarNamesDocking(size_t& count) {
    static const auto names = catalog_detail::MakeNames(catalog_detail::kStyleVarsDocking);
    count = sizeof(catalog_detail::kStyleVarsDocking) / sizeof(catalog_detail::kStyleVarsDocking[0]);
    return names.names;
}

inline const char* ImGuiColCanonicalName(const char* name) {
    for (const catalog_detail::ColAlias& a : catalog_detail::kColAliases)
        if (name && std::strcmp(a.old_name, name) == 0) return a.new_name;
    return name;
}

inline int ImGuiColIndex(const char* name) {
    name = ImGuiColCanonicalName(name);
    const catalog_detail::StyleEntry* e = catalog_detail::FindEntry(catalog_detail::kCols, name);
    return e ? static_cast<int>(e - catalog_detail::kCols) : -1;
}

inline int ImGuiStyleVarIndex(const char* name) {
    const catalog_detail::StyleEntry* e = catalog_detail::FindEntry(catalog_detail::kStyleVars, name);
    return e ? static_cast<int>(e - catalog_detail::kStyleVars) : -1;
}

inline int ImGuiColMinVersion(const char* name) {
    name = ImGuiColCanonicalName(name);
    const catalog_detail::StyleEntry* e = catalog_detail::FindEntry(catalog_detail::kCols, name);
    if (!e) e = catalog_detail::FindEntry(catalog_detail::kColsDocking, name);
    return e ? e->min_version : 0;
}

inline int ImGuiStyleVarMinVersion(const char* name) {
    const catalog_detail::StyleEntry* e = catalog_detail::FindEntry(catalog_detail::kStyleVars, name);
    if (!e) e = catalog_detail::FindEntry(catalog_detail::kStyleVarsDocking, name);
    return e ? e->min_version : 0;
}

inline bool StyleVarIsVec2(const char* name) {
    const catalog_detail::StyleEntry* e = catalog_detail::FindEntry(catalog_detail::kStyleVars, name);
    if (!e) e = catalog_detail::FindEntry(catalog_detail::kStyleVarsDocking, name);
    return e ? e->vec2 : false;
}

// matches either the old or the new name
inline const FlagRename* FindFlagRename(const char* enum_name, const char* flag_name) {
    enum_name = detail::StripImGuiPrefix(enum_name);
    if (!enum_name || !flag_name) return nullptr;
    for (const FlagRename& r : catalog_detail::kFlagRenames)
        if (std::strcmp(r.enum_name, enum_name) == 0 &&
            (std::strcmp(r.old_name, flag_name) == 0 || std::strcmp(r.new_name, flag_name) == 0))
            return &r;
    return nullptr;
}

}  // namespace IMFORGE_CATALOG_NS
}  // namespace imforge

#undef IMFORGE_CATALOG_PICK
#undef IMFORGE_CATALOG_BEFORE
#undef IMFORGE_CATALOG_HAS
#undef IMFORGE_CATALOG_NS
#undef IMFORGE_CATALOG_NS_EXPAND
#undef IMFORGE_CATALOG_NS_PASTE
#undef IMFORGE_CATALOG_ALL
#undef IMFORGE_CATALOG_NUM
