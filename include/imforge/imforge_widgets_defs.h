#pragma once

#include <cstddef>

namespace imforge {

enum class PropType {
    Bool, Int, Float, String, Color, Vec2, Vec4, Size, Enum, Flags, StringList, SizeList, Expr, Code
};

// min == max == 0: no numeric range
struct PropDef {
    const char* name;
    PropType type;
    const char* def_json;
    const char* enum_values;
    float min, max;
    int min_version;
    const char* hint;
};

struct BindDef {
    const char* name;
    const char* types;
    bool required;
};

struct WidgetDef {
    const char* type;
    const char* category;
    const PropDef* props;
    int prop_count;
    const BindDef* binds;
    int bind_count;
    const char* const* events;
    int event_count;
    bool container;
    bool sizable;
    const char* child_types;
    int min_version;
    const char* icon;
};

struct FlagDef {
    const char* name;
    int value;
    int min_version;
};

namespace detail {

struct PropSpan {
    const PropDef* p;
    int n;
    template <size_t N>
    constexpr PropSpan(const PropDef (&a)[N]) : p(a), n(static_cast<int>(N)) {}
    constexpr PropSpan(std::nullptr_t) : p(nullptr), n(0) {}
};

struct BindSpan {
    const BindDef* p;
    int n;
    template <size_t N>
    constexpr BindSpan(const BindDef (&a)[N]) : p(a), n(static_cast<int>(N)) {}
    constexpr BindSpan(std::nullptr_t) : p(nullptr), n(0) {}
};

struct EventSpan {
    const char* const* p;
    int n;
    template <size_t N>
    constexpr EventSpan(const char* const (&a)[N]) : p(a), n(static_cast<int>(N)) {}
    constexpr EventSpan(std::nullptr_t) : p(nullptr), n(0) {}
};

constexpr WidgetDef MakeWidget(const char* type, const char* category, PropSpan props,
                               BindSpan binds, EventSpan events, bool container, bool sizable,
                               const char* child_types, int min_version, const char* icon) {
    return WidgetDef{type, category, props.p, props.n, binds.p, binds.n, events.p, events.n,
                     container, sizable, child_types, min_version, icon};
}

}  // namespace detail
}  // namespace imforge
