#pragma once

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "imforge_json.h"
#include "imforge_value.h"
#include "imforge_widgets.h"

namespace imforge {

namespace core_detail {

inline std::string FormatFloat(float f) {
    char buf[32];
    for (int prec = 6; prec <= 9; ++prec) {
        std::snprintf(buf, sizeof(buf), "%.*g", prec, (double)f);
        if (std::strtof(buf, nullptr) == f) break;
    }
    for (char* p = buf; *p; ++p)
        if (*p == ',') *p = '.';
    return buf;
}

inline double FloatToNum(float f) {
    std::string s = FormatFloat(f);
    return json_detail::StrToDoubleC(s.data(), s.size());
}

inline bool ParseNumber(const std::string& s, double& out) {
    if (s.empty()) return false;
    std::string t = s;
    char dp = json_detail::DecimalPoint();
    if (dp != '.')
        for (char& c : t)
            if (c == '.') c = dp;
    char* end = nullptr;
    out = std::strtod(t.c_str(), &end);
    return end != t.c_str() && *end == '\0';
}

inline bool IsIdentifier(const std::string& s) {
    if (s.empty()) return false;
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        bool alpha = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
        bool digit = c >= '0' && c <= '9';
        if (!alpha && !(digit && i > 0)) return false;
    }
    return true;
}

inline int HexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

}  // namespace core_detail

struct SizeSpec {
    enum class Mode { Auto, Px, Fill, Percent, FillMinus } mode = Mode::Auto;
    float value = 0;

    static bool Parse(const Json& j, SizeSpec& out) {
        double v = 0;
        if (j.IsNumber()) {
            out.mode = Mode::Px;
            out.value = (float)j.AsDouble();
            return true;
        }
        if (!j.IsString()) return false;
        const std::string& s = j.AsString();
        if (s == "auto") {
            out.mode = Mode::Auto;
            out.value = 0;
            return true;
        }
        if (s == "fill") {
            out.mode = Mode::Fill;
            out.value = 0;
            return true;
        }
        if (s.size() > 1 && s.back() == '%' && core_detail::ParseNumber(s.substr(0, s.size() - 1), v)) {
            out.mode = Mode::Percent;
            out.value = (float)v;
            return true;
        }
        if (s.size() > 5 && s.compare(0, 5, "fill-") == 0 && core_detail::ParseNumber(s.substr(5), v)) {
            out.mode = Mode::FillMinus;
            out.value = (float)v;
            return true;
        }
        if (core_detail::ParseNumber(s, v)) {
            out.mode = Mode::Px;
            out.value = (float)v;
            return true;
        }
        return false;
    }
    Json ToJson() const {
        if (mode == Mode::Px) return Json(core_detail::FloatToNum(value));
        return Json(ToString());
    }
    std::string ToString() const {
        switch (mode) {
        case Mode::Px: return core_detail::FormatFloat(value);
        case Mode::Fill: return "fill";
        case Mode::Percent: return core_detail::FormatFloat(value) + "%";
        case Mode::FillMinus: return "fill-" + core_detail::FormatFloat(value);
        default: return "auto";
        }
    }
};

struct PropValue {
    Json raw;
    PropValue() = default;
    PropValue(Json j) : raw(std::move(j)) {}
    bool IsExpr() const { return raw.IsObject() && raw["expr"].IsString(); }
    bool IsParam() const { return raw.IsObject() && raw["param"].IsString(); }
    std::string Expr() const { return IsExpr() ? raw["expr"].AsString() : std::string(); }
    std::string Param() const { return IsParam() ? raw["param"].AsString() : std::string(); }
};

struct Action {
    std::string type, target;
    Json value;
};
struct EventSpec {
    std::vector<Action> actions;
    bool handler = false;
};
struct StyleOverride {
    std::vector<std::pair<std::string, Vec4>> colors;
    std::vector<std::pair<std::string, Json>> vars;
    std::vector<std::string> classes;
    bool Empty() const { return colors.empty() && vars.empty() && classes.empty(); }
};
struct Transition {
    std::string prop, on;
    Json to;
    float duration = 0.15f;
    std::string ease = "OutCubic";
    float stiffness = 0, damping = 0;
};
struct MotionSpec {
    std::string preset, custom, trigger = "enter", on;
    float duration_scale = 1, delay = 0, stagger = 0;
    int iterations = -1;
    const std::string& Name() const { return preset.empty() ? custom : preset; }
};
struct AnimSpec {
    std::vector<Transition> transitions;
    std::string enter_preset, exit_preset;
    float enter_duration = 0.25f, exit_duration = 0.2f, stagger = 0;
    std::vector<MotionSpec> motions;
    bool Empty() const { return transitions.empty() && enter_preset.empty() && exit_preset.empty() && motions.empty(); }
};

// set by imforge_interp.h (motion registry); without it preset names aren't checked
using MotionPresetFn = bool (*)(const char* name);
inline MotionPresetFn& MotionPresetResolver() {
    static MotionPresetFn fn = nullptr;
    return fn;
}

inline bool IsMotionTriggerName(const std::string& t) {
    static const char* const kNames[] = {"enter", "exit", "hover", "press", "focus", "loop", "change", "attention"};
    for (const char* k : kNames)
        if (t == k) return true;
    return false;
}
struct FxSpec {
    std::string type;
    Json params;
};
struct Layout {
    std::string anchor = "TopLeft";
    Vec2 offset;
    bool IsDefault() const { return anchor == "TopLeft" && offset.x == 0 && offset.y == 0; }
};

#ifdef SetProp
#undef SetProp
#endif
#ifdef RemoveProp
#undef RemoveProp
#endif
struct Node {
    std::string id, type, name, component;
    std::vector<std::pair<std::string, PropValue>> props;
    std::vector<std::pair<std::string, PropValue>> bind;
    std::string visible_if, enabled_if, tooltip, font;
    StyleOverride style;
    std::vector<std::pair<std::string, EventSpec>> events;
    AnimSpec anim;
    std::vector<FxSpec> fx;
    Layout layout;
    bool locked = false;
    std::vector<Node> children;
    std::vector<std::pair<std::string, std::vector<Node>>> slots;

    const PropValue* Prop(const char* key) const {
        for (const auto& p : props)
            if (p.first == key) return &p.second;
        return nullptr;
    }
    PropValue* Prop(const char* key) {
        for (auto& p : props)
            if (p.first == key) return &p.second;
        return nullptr;
    }
    void SetProp(const char* key, Json v) {
        if (PropValue* p = Prop(key)) {
            p->raw = std::move(v);
            return;
        }
        props.emplace_back(std::string(key), PropValue(std::move(v)));
    }
    void RemoveProp(const char* key) {
        for (auto it = props.begin(); it != props.end(); ++it) {
            if (it->first == key) {
                props.erase(it);
                return;
            }
        }
    }
    const PropValue* Bind(const char* slot) const {
        for (const auto& b : bind)
            if (b.first == slot) return &b.second;
        return nullptr;
    }
};

struct Param {
    std::string name, type;
    Json def;
};
struct StateVar {
    std::string name, type;
    Json def;
};
struct Panel {
    std::string name, file;
    std::vector<Param> params;
    std::vector<StateVar> state;
    Node root;
};
struct Component {
    std::string name, file;
    std::vector<Param> params;
    std::vector<std::string> slots;
    Node root;
};
struct Theme {
    std::string name, file;
    std::vector<std::pair<std::string, Vec4>> colors;
    std::vector<std::pair<std::string, Json>> vars;
};
struct FontSpec {
    std::string name, file, ranges, merge_icons;
    float size = 16;
};
struct CodegenSettings {
    int cpp_standard = 17;
    std::string nmspace = "ui", function_case = "pascal", variable_case = "snake", output_dir = "generated",
                dpi_mode = "fontSize", runtime_header = "imb_runtime.h";
    bool split_header = true, use_tabs = false, markers = true, std_string = true;
    int indent = 4;
    float base_font_size = 13;
};
struct LiveSettings {
    bool enabled = true;
    std::string transport = "tcp", pipe_name = "ImForge", shm_name = "ImForge";
    int tcp_port = 7777;
};

struct Project {
    std::string name, dir, imgui_version = "1.91.9", imgui_branch = "docking", window = "sdl3", renderer = "gl3";
    std::vector<std::pair<std::string, bool>> features;
    CodegenSettings codegen;
    LiveSettings live;
    std::vector<std::string> binding_types, include_paths;
    std::string compile_commands, types_json;
    std::vector<FontSpec> fonts;
    std::vector<Theme> themes;
    std::string active_theme;
    std::vector<std::pair<std::string, StyleOverride>> style_classes;
    std::vector<Panel> panels;
    std::vector<Component> components;
    std::vector<std::string> libraries;
    std::vector<std::string> panel_files, component_files, theme_files;
    std::vector<std::string> animation_files;
    std::vector<Json> animations;
    const WidgetCatalog* widget_catalog = nullptr;

    static int ParseVersion(const std::string& v) {
        int parts[3] = {0, 0, 0};
        int n = 0;
        size_t i = 0;
        while (n < 3 && i < v.size() && v[i] >= '0' && v[i] <= '9') {
            int x = 0;
            while (i < v.size() && v[i] >= '0' && v[i] <= '9') x = x * 10 + (v[i++] - '0');
            parts[n++] = x;
            if (i < v.size() && v[i] == '.') ++i;
            else break;
        }
        if (n == 0) return 0;
        if (parts[1] > 99) parts[1] = 99;
        if (parts[2] > 9) parts[2] = 9;
        return parts[0] * 10000 + parts[1] * 100 + parts[2] * 10;
    }
    static std::string VersionString(int num) {
        char buf[32];
        int major = num / 10000, minor = (num / 100) % 100, patch = (num % 100) / 10;
        if (patch > 0) std::snprintf(buf, sizeof(buf), "%d.%d.%d", major, minor, patch);
        else std::snprintf(buf, sizeof(buf), "%d.%d", major, minor);
        return buf;
    }
    int ImGuiVersionNum() const { return ParseVersion(imgui_version); }
    bool Feature(const char* key, bool def = false) const {
        for (const auto& f : features)
            if (f.first == key) return f.second;
        return def;
    }
    const Component* FindComponent(const std::string& cname) const {
        for (const auto& c : components)
            if (c.name == cname) return &c;
        return nullptr;
    }
    const Json* FindAnimation(const std::string& aname) const {
        for (const Json& a : animations)
            if (a.IsObject() && a["name"].IsString() && a["name"].AsString() == aname) return &a;
        return nullptr;
    }
};

inline Vec4 ParseColor(const std::string& hex, bool* ok = nullptr) {
    Vec4 c;
    unsigned v[4] = {0, 0, 0, 255};
    bool good = (hex.size() == 7 || hex.size() == 9) && hex[0] == '#';
    for (size_t i = 1; good && i + 1 < hex.size(); i += 2) {
        int hi = core_detail::HexDigit(hex[i]), lo = core_detail::HexDigit(hex[i + 1]);
        if (hi < 0 || lo < 0) good = false;
        else v[(i - 1) / 2] = (unsigned)(hi * 16 + lo);
    }
    if (ok) *ok = good;
    if (!good) return c;
    c.x = (float)v[0] / 255.0f;
    c.y = (float)v[1] / 255.0f;
    c.z = (float)v[2] / 255.0f;
    c.w = (float)v[3] / 255.0f;
    return c;
}

inline std::string ColorToHex(const Vec4& c) {
    const float comps[4] = {c.x, c.y, c.z, c.w};
    int q[4];
    for (int i = 0; i < 4; ++i) {
        float f = comps[i];
        q[i] = (f != f || f <= 0.0f) ? 0 : (f >= 1.0f ? 255 : (int)(f * 255.0f + 0.5f));
    }
    char buf[16];
    std::snprintf(buf, sizeof(buf), "#%02x%02x%02x%02x", q[0] & 0xff, q[1] & 0xff, q[2] & 0xff, q[3] & 0xff);
    return buf;
}

inline Node* FindNode(Node& root, const std::string& id) {
    if (root.id == id) return &root;
    for (Node& c : root.children)
        if (Node* n = FindNode(c, id)) return n;
    for (auto& s : root.slots)
        for (Node& c : s.second)
            if (Node* n = FindNode(c, id)) return n;
    return nullptr;
}
inline const Node* FindNode(const Node& root, const std::string& id) {
    return FindNode(const_cast<Node&>(root), id);
}

inline Node* FindParent(Node& root, const std::string& id, size_t* index_out) {
    for (size_t i = 0; i < root.children.size(); ++i) {
        if (root.children[i].id == id) {
            if (index_out) *index_out = i;
            return &root;
        }
    }
    for (auto& s : root.slots) {
        for (size_t i = 0; i < s.second.size(); ++i) {
            if (s.second[i].id == id) {
                if (index_out) *index_out = i;
                return &root;
            }
        }
    }
    for (Node& c : root.children)
        if (Node* p = FindParent(c, id, index_out)) return p;
    for (auto& s : root.slots)
        for (Node& c : s.second)
            if (Node* p = FindParent(c, id, index_out)) return p;
    return nullptr;
}

template <class F>
inline void Walk(Node& root, F&& f) {
    f(root);
    for (Node& c : root.children) Walk(c, f);
    for (auto& s : root.slots)
        for (Node& c : s.second) Walk(c, f);
}
template <class F>
inline void Walk(const Node& root, F&& f) {
    f(root);
    for (const Node& c : root.children) Walk(c, f);
    for (const auto& s : root.slots)
        for (const Node& c : s.second) Walk(c, f);
}

inline void CollectIds(const Node& root, std::vector<std::string>& out) {
    Walk(root, [&](const Node& n) { out.push_back(n.id); });
}

inline std::string GenerateId(const Node& root, const std::string& type) {
    std::string base;
    for (char c : type) {
        bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
        base.push_back(ok ? c : '_');
    }
    if (base.empty() || (base[0] >= '0' && base[0] <= '9')) base = "node" + base;
    if (base[0] >= 'A' && base[0] <= 'Z') base[0] = (char)(base[0] - 'A' + 'a');
    std::vector<std::string> ids;
    CollectIds(root, ids);
    for (int n = 1;; ++n) {
        std::string cand = base + "_" + std::to_string(n);
        bool used = false;
        for (const std::string& s : ids) {
            if (s == cand) {
                used = true;
                break;
            }
        }
        if (!used) return cand;
    }
}

}  // namespace imforge

#include "imforge_core_json.h"
#include "imforge_core_validate.h"
