#pragma once

#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "imgui.h"
#include "imforge_core.h"
#include "imforge_expr.h"
#include "imb_runtime.h"
#include "imforge_anim_names.h"

namespace imforge {
namespace interp_detail {

inline float MaxF(float a, float b) { return a > b ? a : b; }

template <class Fn>
inline void SetEntry(std::vector<std::pair<std::string, Fn>>& table, const char* key, Fn fn) {
    for (auto& kv : table) {
        if (kv.first == key) {
            kv.second = fn;
            return;
        }
    }
    table.emplace_back(std::string(key), fn);
}

template <class Fn>
inline Fn FindEntry(const std::vector<std::pair<std::string, Fn>>& table, const std::string& key) {
    for (const auto& kv : table)
        if (kv.first == key) return kv.second;
    return nullptr;
}

struct Shadow {
    void* dst = nullptr;
    CType type;
    bool as_int = false;
    float f = 0, f0 = 0;
    int i = 0, i0 = 0;
};

struct CachedExpr {
    expr::Ast ast;
    bool parsed = false, ok = false;
    Error err;
};

struct CachedInterp {
    std::vector<expr::InterpSegment> segs;
    bool parsed = false, ok = false;
    Error err;
};

enum class MockKind { Bool, Int, Float, String, Vec2, Vec4, Color, FloatArray, IntArray, Texture };

struct MockType {
    MockKind kind = MockKind::Float;
    int count = 1;
};

inline std::string Trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t')) --b;
    return s.substr(a, b - a);
}

inline MockType MockTypeOf(const char* types) {
    MockType mt;
    if (!types) return mt;
    std::string all(types);
    size_t bar = all.find('|');
    std::string t = Trim(bar == std::string::npos ? all : all.substr(0, bar));
    size_t br = t.find('[');
    std::string base = br == std::string::npos ? t : t.substr(0, br);
    int n = 0;
    if (br != std::string::npos) {
        size_t p = br + 1;
        while (p < t.size() && t[p] >= '0' && t[p] <= '9') n = n * 10 + (t[p++] - '0');
        if (n == 0) n = (p < t.size() && t[p] == ']') ? 16 : 4;
    }
    if (base == "bool") mt.kind = MockKind::Bool;
    else if ((base == "int" || base == "enum") && (br == std::string::npos || base == "enum")) mt.kind = MockKind::Int;
    else if (base == "string" || base == "char") mt.kind = MockKind::String;
    else if (base == "vec2") mt.kind = MockKind::Vec2;
    else if (base == "vec4") mt.kind = MockKind::Vec4;
    else if (base == "color") mt.kind = MockKind::Color;
    else if (base == "texture") mt.kind = MockKind::Texture;
    else if (base == "vector<float>") mt.kind = MockKind::FloatArray, mt.count = 16;
    else if (base == "vector<int>") mt.kind = MockKind::IntArray, mt.count = 16;
    else if (base == "float" && n > 0) mt.kind = MockKind::FloatArray, mt.count = n;
    else if (base == "int" && n > 0) mt.kind = MockKind::IntArray, mt.count = n;
    else mt.kind = MockKind::Float;
    if (mt.kind == MockKind::FloatArray || mt.kind == MockKind::IntArray) {
        if (n > 0) mt.count = n;
    }
    return mt;
}

inline Value MakeMockValue(const MockType& mt) {
    switch (mt.kind) {
    case MockKind::Bool: return Value(false);
    case MockKind::Int: return Value(0LL);
    case MockKind::Float: return Value(0.5);
    case MockKind::String: return Value(std::string());
    case MockKind::Vec2: return Value(Vec2{0, 0});
    case MockKind::Vec4: return Value(Vec4{1, 1, 1, 1});
    case MockKind::Color: return Value::Color(Vec4{1, 1, 1, 1});
    case MockKind::Texture: return Value(0LL);
    case MockKind::FloatArray: {
        Value v(std::string(static_cast<size_t>(mt.count) * sizeof(float), '\0'));
        float* f = reinterpret_cast<float*>(&v.s[0]);
        for (int k = 0; k < mt.count; ++k) f[k] = 0.5f + 0.5f * std::sin(static_cast<float>(k) * 0.7f);
        return v;
    }
    case MockKind::IntArray: {
        Value v(std::string(static_cast<size_t>(mt.count) * sizeof(int), '\0'));
        int* p = reinterpret_cast<int*>(&v.s[0]);
        for (int k = 0; k < mt.count; ++k) p[k] = k;
        return v;
    }
    }
    return Value();
}

inline bool ValueStorage(Value& v, void** ptr, CType* type) {
    using value_detail::Simple;
    switch (v.kind) {
    case Value::Kind::Bool: *ptr = &v.b; *type = Simple(CKind::Bool, "bool", sizeof(bool)); return true;
    case Value::Kind::Int: *ptr = &v.i; *type = Simple(CKind::I64, "long long", sizeof(long long)); return true;
    case Value::Kind::Float: *ptr = &v.f; *type = Simple(CKind::Double, "double", sizeof(double)); return true;
    case Value::Kind::String:
        *ptr = &v.s;
        *type = Simple(CKind::StdString, "std::string", sizeof(std::string));
        return true;
    case Value::Kind::Vec2: *ptr = &v.v2; *type = Simple(CKind::Vec2, "imforge::Vec2", sizeof(Vec2)); return true;
    case Value::Kind::Vec4:
    case Value::Kind::Color:
        *ptr = &v.v4;
        *type = Simple(CKind::Vec4, "imforge::Vec4", sizeof(Vec4));
        return true;
    default: return false;
    }
}

inline bool MockValueStorage(Value& v, const MockType& mt, void** ptr, CType* type) {
    using value_detail::Simple;
    if (mt.kind == MockKind::FloatArray || mt.kind == MockKind::IntArray) {
        size_t es = mt.kind == MockKind::FloatArray ? sizeof(float) : sizeof(int);
        if (!v.IsString() || v.s.size() != es * static_cast<size_t>(mt.count)) v = MakeMockValue(mt);
        *ptr = &v.s[0];
        *type = Simple(mt.kind == MockKind::FloatArray ? CKind::FloatArray : CKind::IntArray,
                       mt.kind == MockKind::FloatArray ? "float[]" : "int[]", es * static_cast<size_t>(mt.count));
        type->count = mt.count;
        return true;
    }
    if (mt.kind == MockKind::Texture) {
        if (!v.IsInt()) v = Value(0LL);
        *ptr = &v.i;
        *type = Simple(CKind::TexID, "ImTextureID", sizeof(ImTextureID));
        return true;
    }
    if (!ValueStorage(v, ptr, type)) {
        v = MakeMockValue(mt);
        return ValueStorage(v, ptr, type);
    }
    return true;
}

inline Value::Kind KindForTypeName(const std::string& t) {
    if (t == "bool") return Value::Kind::Bool;
    if (t == "int") return Value::Kind::Int;
    if (t == "float" || t == "double") return Value::Kind::Float;
    if (t == "string") return Value::Kind::String;
    if (t == "vec2") return Value::Kind::Vec2;
    if (t == "vec4") return Value::Kind::Vec4;
    if (t == "color") return Value::Kind::Color;
    if (t.compare(0, 5, "enum:") == 0) return Value::Kind::String;
    return Value::Kind::Null;
}

inline bool IsBindingParamType(const std::string& t) { return t.compare(0, 8, "binding<") == 0; }

inline void SplitRoot(const std::string& path, std::string& root, std::string& rest) {
    size_t i = 0;
    while (i < path.size() && path[i] != '.' && path[i] != '[') ++i;
    root = path.substr(0, i);
    rest = path.substr(i);
    if (!rest.empty() && rest[0] == '.') rest.erase(0, 1);
}

inline std::string JoinPath(const std::string& a, const std::string& b) {
    if (b.empty()) return a;
    if (a.empty()) return b;
    if (b[0] == '[') return a + b;
    return a + "." + b;
}

inline bool ComponentOf(void* base, const CType& t, const char* comp, void** ptr, CType* type) {
    if (t.kind != CKind::Vec2 && t.kind != CKind::Vec4) return false;
    if (!comp || !comp[0] || comp[1]) return false;
    int c = comp[0] == 'x' ? 0 : comp[0] == 'y' ? 1 : comp[0] == 'z' ? 2 : comp[0] == 'w' ? 3 : -1;
    if (c < 0 || c >= (t.kind == CKind::Vec2 ? 2 : 4)) return false;
    *ptr = static_cast<float*>(base) + c;
    *type = value_detail::Simple(CKind::Float, "float", sizeof(float));
    return true;
}

inline float ResolveAxis(const SizeSpec& s, float avail, float S) {
    switch (s.mode) {
    case SizeSpec::Mode::Px: return s.value * S;
    case SizeSpec::Mode::Fill: return -FLT_MIN;
    case SizeSpec::Mode::Percent: return avail * s.value / 100.0f;
    case SizeSpec::Mode::FillMinus: return MaxF(avail - s.value * S, 1.0f);
    default: return 0.0f;
    }
}

inline ImVec2 ResolveSize(const SizeSpec& w, const SizeSpec& h, float S) {
    ImVec2 avail = ImGui::GetContentRegionAvail();
    return ImVec2(ResolveAxis(w, avail.x, S), ResolveAxis(h, avail.y, S));
}

inline bool ParseSizeJson(const Json& j, SizeSpec& w, SizeSpec& h) {
    if (j.IsObject()) {
        bool a = SizeSpec::Parse(j["w"], w), b = SizeSpec::Parse(j["h"], h);
        return a || b;
    }
    if (j.IsArray() && j.Size() == 2) return SizeSpec::Parse(j[0], w) | SizeSpec::Parse(j[1], h);
    if (j.IsString() || j.IsNumber()) {
        if (!SizeSpec::Parse(j, w)) return false;
        h = SizeSpec();
        return true;
    }
    return false;
}

inline int ColorIndexUncached(const char* name) {
    static const char* const aliases[][2] = {{"TabActive", "TabSelected"}, {"TabUnfocused", "TabDimmed"},
                                              {"TabUnfocusedActive", "TabDimmedSelected"},
                                              {"NavHighlight", "NavCursor"}};
    for (int i = 0; i < ImGuiCol_COUNT; ++i)
        if (std::strcmp(ImGui::GetStyleColorName(i), name) == 0) return i;
    for (const auto& a : aliases) {
        const char* other = std::strcmp(a[0], name) == 0 ? a[1] : std::strcmp(a[1], name) == 0 ? a[0] : nullptr;
        if (!other) continue;
        for (int i = 0; i < ImGuiCol_COUNT; ++i)
            if (std::strcmp(ImGui::GetStyleColorName(i), other) == 0) return i;
    }
    return -1;
}

inline int ColorIndex(const char* name) {
    if (!name || !*name) return -1;
    static std::map<std::string, int> cache;
    auto it = cache.find(name);
    if (it != cache.end()) return it->second;
    int idx = ColorIndexUncached(name);
    if (cache.size() < 1024) cache.emplace(name, idx);
    return idx;
}

enum class FlagLookup { UnknownEnum, Found, UnknownName, NeedsVersion };

// NeedsVersion: flag is from a newer imgui, out = the IMGUI_VERSION_NUM it showed up in.
// renamed flags only appear under the current spelling here, ResolveFlag handles the old one
inline FlagLookup RunningFlagValue(const char* enum_name, const char* name, int& out) {
#define IMFORGE_FLAG(E, N) \
    if (std::strcmp(name, #N) == 0) { out = static_cast<int>(E##_##N); return FlagLookup::Found; }
#define IMFORGE_FLAG_AS(N, V) \
    if (std::strcmp(name, #N) == 0) { out = static_cast<int>(V); return FlagLookup::Found; }
#define IMFORGE_FLAG_SINCE(N, V) \
    if (std::strcmp(name, #N) == 0) { out = V; return FlagLookup::NeedsVersion; }
    if (std::strcmp(enum_name, "WindowFlags") == 0) {
        IMFORGE_FLAG(ImGuiWindowFlags, NoTitleBar)
        IMFORGE_FLAG(ImGuiWindowFlags, NoResize)
        IMFORGE_FLAG(ImGuiWindowFlags, NoMove)
        IMFORGE_FLAG(ImGuiWindowFlags, NoScrollbar)
        IMFORGE_FLAG(ImGuiWindowFlags, NoScrollWithMouse)
        IMFORGE_FLAG(ImGuiWindowFlags, NoCollapse)
        IMFORGE_FLAG(ImGuiWindowFlags, AlwaysAutoResize)
        IMFORGE_FLAG(ImGuiWindowFlags, NoBackground)
        IMFORGE_FLAG(ImGuiWindowFlags, NoSavedSettings)
        IMFORGE_FLAG(ImGuiWindowFlags, NoMouseInputs)
        IMFORGE_FLAG(ImGuiWindowFlags, MenuBar)
        IMFORGE_FLAG(ImGuiWindowFlags, HorizontalScrollbar)
        IMFORGE_FLAG(ImGuiWindowFlags, NoFocusOnAppearing)
        IMFORGE_FLAG(ImGuiWindowFlags, NoBringToFrontOnFocus)
        IMFORGE_FLAG(ImGuiWindowFlags, AlwaysVerticalScrollbar)
        IMFORGE_FLAG(ImGuiWindowFlags, AlwaysHorizontalScrollbar)
        IMFORGE_FLAG(ImGuiWindowFlags, NoNavInputs)
        IMFORGE_FLAG(ImGuiWindowFlags, NoNavFocus)
        IMFORGE_FLAG(ImGuiWindowFlags, UnsavedDocument)
        IMFORGE_FLAG(ImGuiWindowFlags, NoNav)
        IMFORGE_FLAG(ImGuiWindowFlags, NoDecoration)
        IMFORGE_FLAG(ImGuiWindowFlags, NoInputs)
#ifdef IMGUI_HAS_DOCK
        IMFORGE_FLAG(ImGuiWindowFlags, NoDocking)
#endif
        return FlagLookup::UnknownName;
    }
    if (std::strcmp(enum_name, "ChildFlags") == 0) {
#if IMGUI_VERSION_NUM >= 19000
#if IMGUI_VERSION_NUM >= 19110
        IMFORGE_FLAG(ImGuiChildFlags, Borders)
#else
        IMFORGE_FLAG_AS(Borders, ImGuiChildFlags_Border)
#endif
        IMFORGE_FLAG(ImGuiChildFlags, AlwaysUseWindowPadding)
        IMFORGE_FLAG(ImGuiChildFlags, ResizeX)
        IMFORGE_FLAG(ImGuiChildFlags, ResizeY)
        IMFORGE_FLAG(ImGuiChildFlags, AutoResizeX)
        IMFORGE_FLAG(ImGuiChildFlags, AutoResizeY)
        IMFORGE_FLAG(ImGuiChildFlags, AlwaysAutoResize)
        IMFORGE_FLAG(ImGuiChildFlags, FrameStyle)
#if IMGUI_VERSION_NUM >= 19090
        IMFORGE_FLAG(ImGuiChildFlags, NavFlattened)
#endif
#endif
        return FlagLookup::UnknownName;
    }
    if (std::strcmp(enum_name, "InputTextFlags") == 0) {
        IMFORGE_FLAG(ImGuiInputTextFlags, CharsDecimal)
        IMFORGE_FLAG(ImGuiInputTextFlags, CharsHexadecimal)
        IMFORGE_FLAG(ImGuiInputTextFlags, CharsScientific)
        IMFORGE_FLAG(ImGuiInputTextFlags, CharsUppercase)
        IMFORGE_FLAG(ImGuiInputTextFlags, CharsNoBlank)
        IMFORGE_FLAG(ImGuiInputTextFlags, AllowTabInput)
        IMFORGE_FLAG(ImGuiInputTextFlags, EnterReturnsTrue)
#if IMGUI_VERSION_NUM >= 18970
        IMFORGE_FLAG(ImGuiInputTextFlags, EscapeClearsAll)
#endif
        IMFORGE_FLAG(ImGuiInputTextFlags, CtrlEnterForNewLine)
        IMFORGE_FLAG(ImGuiInputTextFlags, ReadOnly)
        IMFORGE_FLAG(ImGuiInputTextFlags, Password)
        IMFORGE_FLAG(ImGuiInputTextFlags, AlwaysOverwrite)
        IMFORGE_FLAG(ImGuiInputTextFlags, AutoSelectAll)
#if IMGUI_VERSION_NUM >= 19080
        IMFORGE_FLAG(ImGuiInputTextFlags, ParseEmptyRefVal)
        IMFORGE_FLAG(ImGuiInputTextFlags, DisplayEmptyRefVal)
#endif
        IMFORGE_FLAG(ImGuiInputTextFlags, NoHorizontalScroll)
        IMFORGE_FLAG(ImGuiInputTextFlags, NoUndoRedo)
#if IMGUI_VERSION_NUM >= 19160
        IMFORGE_FLAG(ImGuiInputTextFlags, ElideLeft)
#endif
        IMFORGE_FLAG(ImGuiInputTextFlags, CallbackCompletion)
        IMFORGE_FLAG(ImGuiInputTextFlags, CallbackHistory)
        IMFORGE_FLAG(ImGuiInputTextFlags, CallbackAlways)
        IMFORGE_FLAG(ImGuiInputTextFlags, CallbackCharFilter)
        IMFORGE_FLAG(ImGuiInputTextFlags, CallbackResize)
        IMFORGE_FLAG(ImGuiInputTextFlags, CallbackEdit)
#if IMGUI_VERSION_NUM >= 19230
        IMFORGE_FLAG(ImGuiInputTextFlags, WordWrap)
#else
        IMFORGE_FLAG_SINCE(WordWrap, 19230)
#endif
        return FlagLookup::UnknownName;
    }
    if (std::strcmp(enum_name, "TreeNodeFlags") == 0) {
        IMFORGE_FLAG(ImGuiTreeNodeFlags, Selected)
        IMFORGE_FLAG(ImGuiTreeNodeFlags, Framed)
#if IMGUI_VERSION_NUM >= 18970
        IMFORGE_FLAG(ImGuiTreeNodeFlags, AllowOverlap)
#else
        IMFORGE_FLAG_AS(AllowOverlap, ImGuiTreeNodeFlags_AllowItemOverlap)
#endif
        IMFORGE_FLAG(ImGuiTreeNodeFlags, NoTreePushOnOpen)
        IMFORGE_FLAG(ImGuiTreeNodeFlags, NoAutoOpenOnLog)
        IMFORGE_FLAG(ImGuiTreeNodeFlags, DefaultOpen)
        IMFORGE_FLAG(ImGuiTreeNodeFlags, OpenOnDoubleClick)
        IMFORGE_FLAG(ImGuiTreeNodeFlags, OpenOnArrow)
        IMFORGE_FLAG(ImGuiTreeNodeFlags, Leaf)
        IMFORGE_FLAG(ImGuiTreeNodeFlags, Bullet)
        IMFORGE_FLAG(ImGuiTreeNodeFlags, FramePadding)
        IMFORGE_FLAG(ImGuiTreeNodeFlags, SpanAvailWidth)
        IMFORGE_FLAG(ImGuiTreeNodeFlags, SpanFullWidth)
#if IMGUI_VERSION_NUM >= 19170
        IMFORGE_FLAG(ImGuiTreeNodeFlags, SpanLabelWidth)
        IMFORGE_FLAG(ImGuiTreeNodeFlags, LabelSpanAllColumns)
#elif IMGUI_VERSION_NUM >= 19060
        IMFORGE_FLAG_AS(SpanLabelWidth, ImGuiTreeNodeFlags_SpanTextWidth)
#endif
#if IMGUI_VERSION_NUM >= 19000
        IMFORGE_FLAG(ImGuiTreeNodeFlags, SpanAllColumns)
#endif
#if IMGUI_VERSION_NUM >= 19200
        IMFORGE_FLAG(ImGuiTreeNodeFlags, NavLeftJumpsToParent)
        IMFORGE_FLAG(ImGuiTreeNodeFlags, DrawLinesNone)
        IMFORGE_FLAG(ImGuiTreeNodeFlags, DrawLinesFull)
        IMFORGE_FLAG(ImGuiTreeNodeFlags, DrawLinesToNodes)
#else
        IMFORGE_FLAG(ImGuiTreeNodeFlags, NavLeftJumpsBackHere)
        IMFORGE_FLAG_SINCE(DrawLinesNone, 19200)
        IMFORGE_FLAG_SINCE(DrawLinesFull, 19200)
        IMFORGE_FLAG_SINCE(DrawLinesToNodes, 19200)
#endif
        IMFORGE_FLAG(ImGuiTreeNodeFlags, CollapsingHeader)
        return FlagLookup::UnknownName;
    }
    if (std::strcmp(enum_name, "SelectableFlags") == 0) {
#if IMGUI_VERSION_NUM >= 19100
        IMFORGE_FLAG(ImGuiSelectableFlags, NoAutoClosePopups)
        IMFORGE_FLAG(ImGuiSelectableFlags, Highlight)
#else
        IMFORGE_FLAG_AS(NoAutoClosePopups, ImGuiSelectableFlags_DontClosePopups)
#endif
        IMFORGE_FLAG(ImGuiSelectableFlags, SpanAllColumns)
        IMFORGE_FLAG(ImGuiSelectableFlags, AllowDoubleClick)
        IMFORGE_FLAG(ImGuiSelectableFlags, Disabled)
#if IMGUI_VERSION_NUM >= 18970
        IMFORGE_FLAG(ImGuiSelectableFlags, AllowOverlap)
#else
        IMFORGE_FLAG_AS(AllowOverlap, ImGuiSelectableFlags_AllowItemOverlap)
#endif
#if IMGUI_VERSION_NUM >= 19230
        IMFORGE_FLAG(ImGuiSelectableFlags, SelectOnNav)
#else
        IMFORGE_FLAG_SINCE(SelectOnNav, 19230)
#endif
        return FlagLookup::UnknownName;
    }
    if (std::strcmp(enum_name, "ComboFlags") == 0) {
        IMFORGE_FLAG(ImGuiComboFlags, PopupAlignLeft)
        IMFORGE_FLAG(ImGuiComboFlags, HeightSmall)
        IMFORGE_FLAG(ImGuiComboFlags, HeightRegular)
        IMFORGE_FLAG(ImGuiComboFlags, HeightLarge)
        IMFORGE_FLAG(ImGuiComboFlags, HeightLargest)
        IMFORGE_FLAG(ImGuiComboFlags, NoArrowButton)
        IMFORGE_FLAG(ImGuiComboFlags, NoPreview)
#if IMGUI_VERSION_NUM >= 19000
        IMFORGE_FLAG(ImGuiComboFlags, WidthFitPreview)
#endif
        return FlagLookup::UnknownName;
    }
    if (std::strcmp(enum_name, "TabBarFlags") == 0) {
        IMFORGE_FLAG(ImGuiTabBarFlags, Reorderable)
        IMFORGE_FLAG(ImGuiTabBarFlags, AutoSelectNewTabs)
        IMFORGE_FLAG(ImGuiTabBarFlags, TabListPopupButton)
        IMFORGE_FLAG(ImGuiTabBarFlags, NoCloseWithMiddleMouseButton)
        IMFORGE_FLAG(ImGuiTabBarFlags, NoTabListScrollingButtons)
        IMFORGE_FLAG(ImGuiTabBarFlags, NoTooltip)
#if IMGUI_VERSION_NUM >= 19090
        IMFORGE_FLAG(ImGuiTabBarFlags, DrawSelectedOverline)
#endif
#if IMGUI_VERSION_NUM >= 19220
        IMFORGE_FLAG(ImGuiTabBarFlags, FittingPolicyMixed)
        IMFORGE_FLAG(ImGuiTabBarFlags, FittingPolicyShrink)
#else
        IMFORGE_FLAG(ImGuiTabBarFlags, FittingPolicyResizeDown)
        IMFORGE_FLAG_SINCE(FittingPolicyMixed, 19220)
#endif
        IMFORGE_FLAG(ImGuiTabBarFlags, FittingPolicyScroll)
        return FlagLookup::UnknownName;
    }
    if (std::strcmp(enum_name, "TabItemFlags") == 0) {
        IMFORGE_FLAG(ImGuiTabItemFlags, UnsavedDocument)
        IMFORGE_FLAG(ImGuiTabItemFlags, SetSelected)
        IMFORGE_FLAG(ImGuiTabItemFlags, NoCloseWithMiddleMouseButton)
        IMFORGE_FLAG(ImGuiTabItemFlags, NoPushId)
        IMFORGE_FLAG(ImGuiTabItemFlags, NoTooltip)
        IMFORGE_FLAG(ImGuiTabItemFlags, NoReorder)
        IMFORGE_FLAG(ImGuiTabItemFlags, Leading)
        IMFORGE_FLAG(ImGuiTabItemFlags, Trailing)
#if IMGUI_VERSION_NUM >= 19010
        IMFORGE_FLAG(ImGuiTabItemFlags, NoAssumedClosure)
#endif
        return FlagLookup::UnknownName;
    }
    if (std::strcmp(enum_name, "ColorEditFlags") == 0) {
        IMFORGE_FLAG(ImGuiColorEditFlags, NoAlpha)
        IMFORGE_FLAG(ImGuiColorEditFlags, NoPicker)
        IMFORGE_FLAG(ImGuiColorEditFlags, NoOptions)
        IMFORGE_FLAG(ImGuiColorEditFlags, NoSmallPreview)
        IMFORGE_FLAG(ImGuiColorEditFlags, NoInputs)
        IMFORGE_FLAG(ImGuiColorEditFlags, NoTooltip)
        IMFORGE_FLAG(ImGuiColorEditFlags, NoLabel)
        IMFORGE_FLAG(ImGuiColorEditFlags, NoSidePreview)
        IMFORGE_FLAG(ImGuiColorEditFlags, NoDragDrop)
        IMFORGE_FLAG(ImGuiColorEditFlags, NoBorder)
#if IMGUI_VERSION_NUM >= 19260
        IMFORGE_FLAG(ImGuiColorEditFlags, NoColorMarkers)
#else
        IMFORGE_FLAG_SINCE(NoColorMarkers, 19260)
#endif
#if IMGUI_VERSION_NUM >= 19290
        IMFORGE_FLAG(ImGuiColorEditFlags, PickerNoRotate)
#else
        IMFORGE_FLAG_SINCE(PickerNoRotate, 19290)
#endif
#if IMGUI_VERSION_NUM >= 19180
        IMFORGE_FLAG(ImGuiColorEditFlags, AlphaOpaque)
        IMFORGE_FLAG(ImGuiColorEditFlags, AlphaNoBg)
#endif
        IMFORGE_FLAG(ImGuiColorEditFlags, AlphaPreviewHalf)
        IMFORGE_FLAG(ImGuiColorEditFlags, AlphaBar)
        IMFORGE_FLAG(ImGuiColorEditFlags, HDR)
        IMFORGE_FLAG(ImGuiColorEditFlags, DisplayRGB)
        IMFORGE_FLAG(ImGuiColorEditFlags, DisplayHSV)
        IMFORGE_FLAG(ImGuiColorEditFlags, DisplayHex)
        IMFORGE_FLAG(ImGuiColorEditFlags, Uint8)
        IMFORGE_FLAG(ImGuiColorEditFlags, Float)
        IMFORGE_FLAG(ImGuiColorEditFlags, PickerHueBar)
        IMFORGE_FLAG(ImGuiColorEditFlags, PickerHueWheel)
        IMFORGE_FLAG(ImGuiColorEditFlags, InputRGB)
        IMFORGE_FLAG(ImGuiColorEditFlags, InputHSV)
        return FlagLookup::UnknownName;
    }
    if (std::strcmp(enum_name, "SliderFlags") == 0) {
        IMFORGE_FLAG(ImGuiSliderFlags, Logarithmic)
        IMFORGE_FLAG(ImGuiSliderFlags, NoRoundToFormat)
        IMFORGE_FLAG(ImGuiSliderFlags, NoInput)
#if IMGUI_VERSION_NUM >= 19090
        IMFORGE_FLAG(ImGuiSliderFlags, WrapAround)
#endif
#if IMGUI_VERSION_NUM >= 19130
        IMFORGE_FLAG(ImGuiSliderFlags, ClampOnInput)
        IMFORGE_FLAG(ImGuiSliderFlags, ClampZeroRange)
#endif
#if IMGUI_VERSION_NUM >= 19170
        IMFORGE_FLAG(ImGuiSliderFlags, NoSpeedTweaks)
#endif
        IMFORGE_FLAG(ImGuiSliderFlags, AlwaysClamp)
#if IMGUI_VERSION_NUM >= 19260
        IMFORGE_FLAG(ImGuiSliderFlags, ColorMarkers)
#else
        IMFORGE_FLAG_SINCE(ColorMarkers, 19260)
#endif
        return FlagLookup::UnknownName;
    }
    if (std::strcmp(enum_name, "TableFlags") == 0) {
        IMFORGE_FLAG(ImGuiTableFlags, Resizable)
        IMFORGE_FLAG(ImGuiTableFlags, Reorderable)
        IMFORGE_FLAG(ImGuiTableFlags, Hideable)
        IMFORGE_FLAG(ImGuiTableFlags, Sortable)
        IMFORGE_FLAG(ImGuiTableFlags, NoSavedSettings)
        IMFORGE_FLAG(ImGuiTableFlags, ContextMenuInBody)
        IMFORGE_FLAG(ImGuiTableFlags, RowBg)
        IMFORGE_FLAG(ImGuiTableFlags, BordersInnerH)
        IMFORGE_FLAG(ImGuiTableFlags, BordersOuterH)
        IMFORGE_FLAG(ImGuiTableFlags, BordersInnerV)
        IMFORGE_FLAG(ImGuiTableFlags, BordersOuterV)
        IMFORGE_FLAG(ImGuiTableFlags, BordersH)
        IMFORGE_FLAG(ImGuiTableFlags, BordersV)
        IMFORGE_FLAG(ImGuiTableFlags, BordersInner)
        IMFORGE_FLAG(ImGuiTableFlags, BordersOuter)
        IMFORGE_FLAG(ImGuiTableFlags, Borders)
        IMFORGE_FLAG(ImGuiTableFlags, NoBordersInBody)
        IMFORGE_FLAG(ImGuiTableFlags, NoBordersInBodyUntilResize)
        IMFORGE_FLAG(ImGuiTableFlags, SizingFixedFit)
        IMFORGE_FLAG(ImGuiTableFlags, SizingFixedSame)
        IMFORGE_FLAG(ImGuiTableFlags, SizingStretchProp)
        IMFORGE_FLAG(ImGuiTableFlags, SizingStretchSame)
        IMFORGE_FLAG(ImGuiTableFlags, NoHostExtendX)
        IMFORGE_FLAG(ImGuiTableFlags, NoHostExtendY)
        IMFORGE_FLAG(ImGuiTableFlags, NoKeepColumnsVisible)
        IMFORGE_FLAG(ImGuiTableFlags, PreciseWidths)
        IMFORGE_FLAG(ImGuiTableFlags, NoClip)
        IMFORGE_FLAG(ImGuiTableFlags, PadOuterX)
        IMFORGE_FLAG(ImGuiTableFlags, NoPadOuterX)
        IMFORGE_FLAG(ImGuiTableFlags, NoPadInnerX)
        IMFORGE_FLAG(ImGuiTableFlags, ScrollX)
        IMFORGE_FLAG(ImGuiTableFlags, ScrollY)
        IMFORGE_FLAG(ImGuiTableFlags, SortMulti)
        IMFORGE_FLAG(ImGuiTableFlags, SortTristate)
#if IMGUI_VERSION_NUM >= 19000
        IMFORGE_FLAG(ImGuiTableFlags, HighlightHoveredColumn)
#endif
        return FlagLookup::UnknownName;
    }
#undef IMFORGE_FLAG
#undef IMFORGE_FLAG_AS
#undef IMFORGE_FLAG_SINCE
    return FlagLookup::UnknownEnum;
}

inline FlagLookup ResolveFlag(const char* enum_name, const char* name, int& out) {
    FlagLookup r = RunningFlagValue(enum_name, name, out);
    if (r != FlagLookup::UnknownName && r != FlagLookup::NeedsVersion) return r;
    if (const FlagRename* rn = FindFlagRename(enum_name, name)) {
        const char* other = std::strcmp(rn->old_name, name) == 0 ? rn->new_name : rn->old_name;
        int v = 0;
        if (RunningFlagValue(enum_name, other, v) == FlagLookup::Found) {
            out = v;
            return FlagLookup::Found;
        }
    }
    return r;
}

inline std::string ImGuiVersionText(int num) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d.%d.%d", num / 10000, (num / 100) % 100, (num % 100) / 10);
    return buf;
}

// the catalog only lists post-1.91.9 colors when the running imgui has them
inline int StyleColorSince(const char* name) {
    static const struct {
        const char* name;
        int since;
    } kNewer[] = {{"InputTextCursor", 19200}, {"TreeLines", 19200}, {"UnsavedMarker", 19240},
                  {"DragDropTargetBg", 19250}, {"CheckboxSelectedBg", 19280}};
    if (!name) return 0;
    if (int v = ImGuiColMinVersion(name)) return v;
    for (const auto& e : kNewer)
        if (std::strcmp(e.name, name) == 0) return e.since;
    return 0;
}

inline int InputTextFlagsWithoutCallbacks(int flags) {
    return flags & ~(ImGuiInputTextFlags_CallbackCompletion | ImGuiInputTextFlags_CallbackHistory |
                     ImGuiInputTextFlags_CallbackAlways | ImGuiInputTextFlags_CallbackCharFilter |
                     ImGuiInputTextFlags_CallbackResize | ImGuiInputTextFlags_CallbackEdit);
}

inline int MaxGridColumns() {
#ifdef IMGUI_TABLE_MAX_COLUMNS
    return IMGUI_TABLE_MAX_COLUMNS - 1;
#elif IMGUI_VERSION_NUM >= 18990
    return 511;
#else
    return 63;
#endif
}

struct StyleVarRef {
    int var = -1;
    float* f = nullptr;
    ImVec2* v = nullptr;
    bool scaled = true;
    int since = 0;
};

inline StyleVarRef StyleVar(ImGuiStyle& st, const char* name) {
    StyleVarRef r;
    if (!name) return r;
#define IMFORGE_SVF(N, SC) \
    if (std::strcmp(name, #N) == 0) { r.var = ImGuiStyleVar_##N; r.f = &st.N; r.scaled = SC; return r; }
#define IMFORGE_SVV(N, SC) \
    if (std::strcmp(name, #N) == 0) { r.var = ImGuiStyleVar_##N; r.v = &st.N; r.scaled = SC; return r; }
    IMFORGE_SVF(Alpha, false)
    IMFORGE_SVV(WindowPadding, true)
    IMFORGE_SVF(WindowRounding, true)
    IMFORGE_SVF(WindowBorderSize, true)
    IMFORGE_SVV(WindowMinSize, true)
    IMFORGE_SVV(WindowTitleAlign, false)
    IMFORGE_SVF(ChildRounding, true)
    IMFORGE_SVF(ChildBorderSize, true)
    IMFORGE_SVF(PopupRounding, true)
    IMFORGE_SVF(PopupBorderSize, true)
    IMFORGE_SVV(FramePadding, true)
    IMFORGE_SVF(FrameRounding, true)
    IMFORGE_SVF(FrameBorderSize, true)
    IMFORGE_SVV(ItemSpacing, true)
    IMFORGE_SVV(ItemInnerSpacing, true)
    IMFORGE_SVF(IndentSpacing, true)
    IMFORGE_SVV(CellPadding, true)
    IMFORGE_SVF(ScrollbarSize, true)
    IMFORGE_SVF(ScrollbarRounding, true)
    IMFORGE_SVF(GrabMinSize, true)
    IMFORGE_SVF(GrabRounding, true)
    IMFORGE_SVF(TabRounding, true)
    IMFORGE_SVV(ButtonTextAlign, false)
    IMFORGE_SVV(SelectableTextAlign, false)
#if IMGUI_VERSION_NUM >= 18600
    IMFORGE_SVF(DisabledAlpha, false)
#endif
#if IMGUI_VERSION_NUM >= 18950
    IMFORGE_SVF(SeparatorTextBorderSize, true)
    IMFORGE_SVV(SeparatorTextAlign, false)
    IMFORGE_SVV(SeparatorTextPadding, true)
#endif
#if IMGUI_VERSION_NUM >= 19000
    IMFORGE_SVF(TabBarBorderSize, true)
    IMFORGE_SVF(TableAngledHeadersAngle, false)
#endif
#if IMGUI_VERSION_NUM >= 19050
    IMFORGE_SVF(TabBorderSize, true)
#endif
#if IMGUI_VERSION_NUM >= 19060
    IMFORGE_SVV(TableAngledHeadersTextAlign, false)
#endif
#if IMGUI_VERSION_NUM >= 19100
    IMFORGE_SVF(TabBarOverlineSize, true)
#endif
#if IMGUI_VERSION_NUM >= 19190
    IMFORGE_SVF(ImageBorderSize, true)
#endif
#if defined(IMGUI_HAS_DOCK) && IMGUI_VERSION_NUM >= 19000
    IMFORGE_SVF(DockingSeparatorSize, true)
#endif
#define IMFORGE_SV_SINCE(N, V) \
    if (std::strcmp(name, #N) == 0) { r.since = V; return r; }
#if IMGUI_VERSION_NUM >= 19200
    IMFORGE_SVF(TreeLinesSize, true)
    IMFORGE_SVF(TreeLinesRounding, true)
#else
    IMFORGE_SV_SINCE(TreeLinesSize, 19200)
    IMFORGE_SV_SINCE(TreeLinesRounding, 19200)
#endif
#if IMGUI_VERSION_NUM >= 19220
    IMFORGE_SVF(TabMinWidthBase, true)
    IMFORGE_SVF(TabMinWidthShrink, true)
#else
    IMFORGE_SV_SINCE(TabMinWidthBase, 19220)
    IMFORGE_SV_SINCE(TabMinWidthShrink, 19220)
#endif
#if IMGUI_VERSION_NUM >= 19230
    IMFORGE_SVF(ScrollbarPadding, true)
#else
    IMFORGE_SV_SINCE(ScrollbarPadding, 19230)
#endif
#if IMGUI_VERSION_NUM >= 19260
    IMFORGE_SVF(ImageRounding, true)
#else
    IMFORGE_SV_SINCE(ImageRounding, 19260)
#endif
#if IMGUI_VERSION_NUM >= 19270
    IMFORGE_SVF(SeparatorSize, true)
#else
    IMFORGE_SV_SINCE(SeparatorSize, 19270)
#endif
#if IMGUI_VERSION_NUM >= 19280
    IMFORGE_SVF(DragDropTargetRounding, true)
#else
    IMFORGE_SV_SINCE(DragDropTargetRounding, 19280)
#endif
#if IMGUI_VERSION_NUM >= 19290
    IMFORGE_SVF(MenuItemRounding, true)
    IMFORGE_SVF(SelectableRounding, true)
#else
    IMFORGE_SV_SINCE(MenuItemRounding, 19290)
    IMFORGE_SV_SINCE(SelectableRounding, 19290)
#endif
#undef IMFORGE_SV_SINCE
#undef IMFORGE_SVF
#undef IMFORGE_SVV
    r.since = ImGuiStyleVarMinVersion(name);
    return r;
}

inline bool PushStyleVarJson(const char* name, const Json& value, float S, int* since = nullptr) {
    StyleVarRef r = StyleVar(ImGui::GetStyle(), name);
    if (since) *since = r.since;
    if (r.var < 0) return false;
    float k = r.scaled ? S : 1.0f;
    if (r.v) {
        Vec2 v = Value::FromJson(value, Value::Kind::Vec2).ToVec2();
        ImGui::PushStyleVar(r.var, ImVec2(v.x * k, v.y * k));
    } else {
        ImGui::PushStyleVar(r.var, static_cast<float>(value.AsDouble()) * k);
    }
    return true;
}

inline bool SetStyleVarJson(ImGuiStyle& st, const char* name, const Json& value) {
    StyleVarRef r = StyleVar(st, name);
    if (r.var < 0) return false;
    if (r.v) {
        Vec2 v = Value::FromJson(value, Value::Kind::Vec2).ToVec2();
        *r.v = ImVec2(v.x, v.y);
    } else {
        *r.f = static_cast<float>(value.AsDouble());
    }
    return true;
}

inline ImVec2 AnchorFraction(const std::string& a) {
    if (a == "Top" || a == "TopCenter") return ImVec2(0.5f, 0);
    if (a == "TopRight") return ImVec2(1, 0);
    if (a == "Left" || a == "CenterLeft") return ImVec2(0, 0.5f);
    if (a == "Center") return ImVec2(0.5f, 0.5f);
    if (a == "Right" || a == "CenterRight") return ImVec2(1, 0.5f);
    if (a == "BottomLeft") return ImVec2(0, 1);
    if (a == "Bottom" || a == "BottomCenter") return ImVec2(0.5f, 1);
    if (a == "BottomRight") return ImVec2(1, 1);
    return ImVec2(0, 0);
}

inline ImVec4 ValueToColor(const Value& v, const ImVec4& def) {
    if (v.IsString()) {
        bool ok = false;
        Vec4 c = ParseColor(v.s, &ok);
        return ok ? ImVec4(c.x, c.y, c.z, c.w) : def;
    }
    if (v.IsVec4() || v.IsColor()) return ImVec4(v.v4.x, v.v4.y, v.v4.z, v.v4.w);
    if (v.IsList() && v.list.size() >= 3) {
        Vec4 c = v.ToVec4();
        return ImVec4(c.x, c.y, c.z, v.list.size() > 3 ? c.w : 1.0f);
    }
    return def;
}

inline ImU32 JsonColorU32(const Json& j, ImU32 def) {
    if (!j.IsString()) return def;
    bool ok = false;
    Vec4 c = ParseColor(j.AsString(), &ok);
    return ok ? ImGui::ColorConvertFloat4ToU32(ImVec4(c.x, c.y, c.z, c.w)) : def;
}

inline void DrawFxBehind(ImDrawList* dl, const FxSpec& fx, const ImVec2& mn, const ImVec2& mx, float S,
                         ImGuiID id, float rounding) {
    const Json& p = fx.params;
    float r = p.Has("rounding") ? static_cast<float>(p["rounding"].AsDouble()) * S : rounding;
    if (fx.type == "shadow") {
        Vec2 off = Value::FromJson(p["offset"], Value::Kind::Vec2).ToVec2();
        if (!p.Has("offset")) off = Vec2{0, 2};
        imb::Shadow(dl, mn, mx, r, JsonColorU32(p["color"], IM_COL32(0, 0, 0, 128)),
                    static_cast<float>(p["size"].AsDouble(8)) * S, ImVec2(off.x * S, off.y * S));
    } else if (fx.type == "glow") {
        imb::Glow(dl, mn, mx, r, JsonColorU32(p["color"], IM_COL32(255, 255, 255, 96)),
                  static_cast<float>(p["size"].AsDouble(8)) * S);
    } else if (fx.type == "gradient") {
        imb::GradientRect(dl, mn, mx, JsonColorU32(p["from"], IM_COL32(255, 255, 255, 32)),
                          JsonColorU32(p["to"], IM_COL32(0, 0, 0, 32)), p["vertical"].AsBool(true), r);
    } else if (fx.type == "radialGradient") {
        ImVec2 c((mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f);
        float rad = p.Has("radius") ? static_cast<float>(p["radius"].AsDouble()) * S
                                    : MaxF(mx.x - mn.x, mx.y - mn.y) * 0.5f;
        imb::RadialGradient(dl, c, rad, JsonColorU32(p["inner"], IM_COL32(255, 255, 255, 64)),
                            JsonColorU32(p["outer"], IM_COL32(0, 0, 0, 0)));
    } else if (fx.type == "noise") {
        imb::NoiseBackground(dl, mn, mx, JsonColorU32(p["color"], IM_COL32(255, 255, 255, 24)),
                             static_cast<float>(p["density"].AsDouble(0.3)), id);
    } else if (fx.type == "particles") {
        imb::ParticleStyle ps;
        ps.count = p["count"].AsInt(ps.count);
        ps.speed = static_cast<float>(p["speed"].AsDouble(ps.speed)) * S;
        ps.size = static_cast<float>(p["size"].AsDouble(ps.size)) * S;
        ps.color = JsonColorU32(p["color"], ps.color);
        ps.connect = p["connect"].AsBool(ps.connect);
        ps.connect_dist = static_cast<float>(p["connectDist"].AsDouble(ps.connect_dist)) * S;
        imb::Particles(dl, id, mn, mx, ps);
    } else if (fx.type == "blur") {
#ifdef IMB_FX_AVAILABLE
        imb::fx::BlurParams bp;
        bp.radius = static_cast<float>(p["radius"].AsDouble(8)) * S;
        bp.rounding = r;
        bp.tint = JsonColorU32(p["tint"], 0xFFFFFFFF);
        bp.saturation = static_cast<float>(p["saturation"].AsDouble(1));
        bp.brightness = static_cast<float>(p["brightness"].AsDouble(1));
        bp.noise = static_cast<float>(p["noise"].AsDouble(0));
        imb::fx::Blur(dl, mn, mx, bp);
#endif
    }
}

inline void DrawFxOver(ImDrawList* dl, const FxSpec& fx, const ImVec2& mn, const ImVec2& mx, float S,
                       float rounding) {
    const Json& p = fx.params;
    float r = p.Has("rounding") ? static_cast<float>(p["rounding"].AsDouble()) * S : rounding;
    if (fx.type == "border") {
        const std::string st = p["style"].AsString("Solid");
        imb::BorderStyle bs = st == "Rainbow" ? imb::BorderStyle::Rainbow
                            : st == "Sweep"   ? imb::BorderStyle::Sweep
                            : st == "Pulse"   ? imb::BorderStyle::Pulse
                                              : imb::BorderStyle::Solid;
        imb::AnimatedBorder(dl, mn, mx, r, static_cast<float>(p["thickness"].AsDouble(1)) * S, bs,
                            JsonColorU32(p["color"], IM_COL32(255, 255, 255, 255)),
                            static_cast<float>(p["speed"].AsDouble(1)));
    } else if (fx.type == "cursorHighlight") {
        imb::CursorHighlight(dl, mn, mx, r, JsonColorU32(p["color"], IM_COL32(255, 255, 255, 40)),
                             static_cast<float>(p["radius"].AsDouble(80)) * S);
    }
}

inline bool IsFxBehind(const std::string& t) {
    return t == "shadow" || t == "glow" || t == "gradient" || t == "radialGradient" || t == "noise" ||
           t == "particles" || t == "blur";
}
inline bool IsFxOver(const std::string& t) { return t == "border" || t == "cursorHighlight"; }

}  // namespace interp_detail
}  // namespace imforge
