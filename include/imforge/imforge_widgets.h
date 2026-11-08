#pragma once

#include <cstddef>
#include <cstring>
#include <deque>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "imforge_json.h"
#include "imforge_value.h"
#include "imforge_widgets_defs.h"
#include "imforge_widgets_flags.h"
#include "imforge_widgets_stock.h"

namespace imforge {

inline const WidgetDef* FindWidget(const char* type) {
    if (!type) return nullptr;
    for (const WidgetDef& w : detail::kStockWidgets)
        if (std::strcmp(w.type, type) == 0) return &w;
    return nullptr;
}

inline const char* PropTypeName(PropType t) {
    switch (t) {
        case PropType::Bool: return "bool";
        case PropType::Int: return "int";
        case PropType::Float: return "float";
        case PropType::String: return "string";
        case PropType::Color: return "color";
        case PropType::Vec2: return "vec2";
        case PropType::Vec4: return "vec4";
        case PropType::Size: return "size";
        case PropType::Enum: return "enum";
        case PropType::Flags: return "flags";
        case PropType::StringList: return "stringList";
        case PropType::SizeList: return "sizeList";
        case PropType::Expr: return "expr";
        case PropType::Code: return "code";
    }
    return "unknown";
}

inline bool ParsePropType(const char* s, PropType& out) {
    if (!s) return false;
    std::string k;
    for (const char* c = s; *c; ++c)
        k += static_cast<char>((*c >= 'A' && *c <= 'Z') ? *c + ('a' - 'A') : *c);
    struct Alias { const char* name; PropType type; };
    static const Alias kAliases[] = {
        {"bool", PropType::Bool}, {"int", PropType::Int}, {"float", PropType::Float},
        {"string", PropType::String}, {"color", PropType::Color}, {"vec2", PropType::Vec2},
        {"vec4", PropType::Vec4}, {"size", PropType::Size}, {"enum", PropType::Enum},
        {"flags", PropType::Flags}, {"stringlist", PropType::StringList},
        {"list<string>", PropType::StringList}, {"string[]", PropType::StringList},
        {"sizelist", PropType::SizeList}, {"list<size>", PropType::SizeList},
        {"list<sizespec>", PropType::SizeList}, {"size[]", PropType::SizeList},
        {"expr", PropType::Expr}, {"code", PropType::Code},
    };
    for (const Alias& a : kAliases) {
        if (k == a.name) {
            out = a.type;
            return true;
        }
    }
    return false;
}

inline Json PropDefaultJson(const PropDef& p) {
    Json out;
    if (p.def_json && ParseJson(p.def_json, std::strlen(p.def_json), out, nullptr)) return out;
    return Json();
}

inline const PropDef* FindProp(const WidgetDef& w, const char* name) {
    if (!name) return nullptr;
    for (int i = 0; i < w.prop_count; ++i)
        if (std::strcmp(w.props[i].name, name) == 0) return &w.props[i];
    return nullptr;
}

inline const BindDef* FindBind(const WidgetDef& w, const char* slot) {
    if (!slot) return nullptr;
    for (int i = 0; i < w.bind_count; ++i)
        if (std::strcmp(w.binds[i].name, slot) == 0) return &w.binds[i];
    return nullptr;
}

inline bool WidgetHasEvent(const WidgetDef& w, const char* event) {
    if (!event) return false;
    for (int i = 0; i < w.event_count; ++i)
        if (std::strcmp(w.events[i], event) == 0) return true;
    return false;
}

inline std::vector<std::string> SplitPipe(const char* list) {
    std::vector<std::string> out;
    if (!list) return out;
    const char* start = list;
    for (const char* c = list;; ++c) {
        if (*c == '|' || *c == '\0') {
            if (c > start) out.emplace_back(start, static_cast<size_t>(c - start));
            if (*c == '\0') break;
            start = c + 1;
        }
    }
    return out;
}

inline bool PipeListContains(const char* list, const char* item) {
    if (!list || !item) return false;
    size_t n = std::strlen(item);
    for (const char* c = list; *c;) {
        const char* end = std::strchr(c, '|');
        size_t len = end ? static_cast<size_t>(end - c) : std::strlen(c);
        if (len == n && std::strncmp(c, item, n) == 0) return true;
        if (!end) break;
        c = end + 1;
    }
    return false;
}

inline std::vector<std::string> EnumValues(const PropDef& p) {
    return p.type == PropType::Enum ? SplitPipe(p.enum_values) : std::vector<std::string>();
}

inline bool IsValidEnumValue(const PropDef& p, const char* value) {
    return p.type == PropType::Enum && PipeListContains(p.enum_values, value);
}

inline bool WidgetAcceptsChild(const WidgetDef& parent, const char* child_type) {
    if (!child_type || !parent.container) return false;
    if (std::strcmp(child_type, "Window") == 0) return false;
    if (std::strcmp(child_type, "Tab") == 0) return std::strcmp(parent.type, "TabBar") == 0;
    if (std::strcmp(child_type, "MenuBar") == 0) return std::strcmp(parent.type, "Window") == 0;
    if (!parent.child_types) return true;
    return PipeListContains(parent.child_types, child_type);
}

namespace detail {

struct WidgetStorage {
    std::deque<std::string> strings;
    std::vector<PropDef> props;
    std::vector<BindDef> binds;
    std::vector<const char*> events;

    const char* Keep(const std::string& s) {
        strings.push_back(s);
        return strings.back().c_str();
    }
};

inline const char* DefaultJsonForType(PropType t) {
    switch (t) {
        case PropType::Bool: return "false";
        case PropType::Int: return "0";
        case PropType::Float: return "0";
        case PropType::String: return "\"\"";
        case PropType::Color: return "\"#ffffffff\"";
        case PropType::Vec2: return R"({"x":0,"y":0})";
        case PropType::Vec4: return R"({"x":0,"y":0,"z":0,"w":0})";
        case PropType::Size: return R"({"w":"auto","h":"auto"})";
        case PropType::Enum: return "null";
        case PropType::Flags: return "[]";
        case PropType::StringList: return "[]";
        case PropType::SizeList: return "[]";
        case PropType::Expr: return "\"\"";
        case PropType::Code: return "\"\"";
    }
    return "null";
}

inline bool JoinStringArray(const Json& arr, std::string& out) {
    out.clear();
    if (!arr.IsArray()) return false;
    for (size_t i = 0; i < arr.Size(); ++i) {
        if (!arr[i].IsString() || arr[i].AsString().empty()) return false;
        if (i) out += '|';
        out += arr[i].AsString();
    }
    return true;
}

}  // namespace detail

struct CustomWidgetDef {
    std::string type, category, include, call, returns;
    Json manifest;
    std::vector<PropDef> props;
    std::vector<BindDef> binds;
    std::vector<std::string> events;
    std::string child_types, icon;
    bool container = false;
    bool sizable = false;
    int min_version = 18500;
    WidgetDef def{};
    std::shared_ptr<detail::WidgetStorage> storage;
};

inline const WidgetDef* AsWidgetDef(const CustomWidgetDef& c) { return &c.def; }

struct WidgetCatalog {
    std::vector<CustomWidgetDef> custom;

    bool AddManifest(const Json& m, Error* err);
    bool RemoveCustom(const char* type);
    const WidgetDef* Find(const char* type) const;
    const CustomWidgetDef* FindCustom(const char* type) const;
    std::vector<const WidgetDef*> All() const;
};

inline const CustomWidgetDef* WidgetCatalog::FindCustom(const char* type) const {
    if (!type) return nullptr;
    for (const CustomWidgetDef& c : custom)
        if (c.type == type) return &c;
    return nullptr;
}

inline const WidgetDef* WidgetCatalog::Find(const char* type) const {
    if (const WidgetDef* w = FindWidget(type)) return w;
    const CustomWidgetDef* c = FindCustom(type);
    return c ? &c->def : nullptr;
}

inline std::vector<const WidgetDef*> WidgetCatalog::All() const {
    std::vector<const WidgetDef*> out;
    size_t n = 0;
    const WidgetDef* stock = StockWidgets(n);
    out.reserve(n + custom.size());
    for (size_t i = 0; i < n; ++i) out.push_back(&stock[i]);
    for (const CustomWidgetDef& c : custom) out.push_back(&c.def);
    return out;
}

inline bool WidgetCatalog::RemoveCustom(const char* type) {
    if (!type) return false;
    for (size_t i = 0; i < custom.size(); ++i) {
        if (custom[i].type == type) {
            custom.erase(custom.begin() + static_cast<std::ptrdiff_t>(i));
            return true;
        }
    }
    return false;
}

// header names from manifests end up in an #include line and in a path under the packs folder
inline bool IsSafeIncludeName(const std::string& s) {
    if (s.size() < 3 || s.size() > 200 || s[0] == '/' || s[0] == '.') return false;
    for (char c : s) {
        bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.' ||
                  c == '/' || c == '-';
        if (!ok) return false;
    }
    if (s.find("..") != std::string::npos || s.find("//") != std::string::npos) return false;
    size_t n = s.size();
    return s.compare(n - 2, 2, ".h") == 0 || (n > 4 && s.compare(n - 4, 4, ".hpp") == 0);
}

// call and callEnd templates are pasted into generated code: one call `ns::Function(...)` whose arguments
// hold {placeholders} and literals. No statements, braces, comments, preprocessor lines or escapes.
inline bool IsSafeCallTemplate(const std::string& s) {
    if (s.empty() || s.size() > 1024) return false;
    size_t i = 0;
    auto ident = [&]() {
        size_t start = i;
        while (i < s.size() && ((s[i] >= 'A' && s[i] <= 'Z') || (s[i] >= 'a' && s[i] <= 'z') || s[i] == '_' ||
                                (i > start && s[i] >= '0' && s[i] <= '9')))
            ++i;
        return i > start;
    };
    if (!ident()) return false;
    while (i + 1 < s.size() && s[i] == ':' && s[i + 1] == ':') {
        i += 2;
        if (!ident()) return false;
    }
    if (i >= s.size() || s[i] != '(') return false;
    int depth = 0;
    bool in_string = false;
    for (; i < s.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        if (c < 0x20 || c > 0x7E || c == ';' || c == '#' || c == '`' || c == '\\' || c == '$' || c == '@' || c == '\'')
            return false;
        if (c == '"') {
            in_string = !in_string;
            continue;
        }
        if (in_string) continue;
        if (c == '/' && i + 1 < s.size() && (s[i + 1] == '/' || s[i + 1] == '*')) return false;
        if (c == '?' && i + 1 < s.size() && s[i + 1] == '?') return false;
        if (c == '{') {
            size_t close = s.find('}', i + 1);
            if (close == std::string::npos) return false;
            std::string body = s.substr(i + 1, close - i - 1);
            size_t colon = body.find(':');
            std::string name = body.substr(0, colon), mod = colon == std::string::npos ? std::string() : body.substr(colon + 1);
            if (name.empty() || (colon != std::string::npos && mod.empty())) return false;
            for (size_t k = 0; k < body.size(); ++k) {
                char b = body[k];
                bool ok = (b >= 'A' && b <= 'Z') || (b >= 'a' && b <= 'z') || b == '_' || (b >= '0' && b <= '9' && k > 0) || k == colon;
                if (!ok) return false;
            }
            i = close;
            continue;
        }
        if (c == '}') return false;
        if (c == '(') {
            ++depth;
        } else if (c == ')') {
            if (--depth < 0) return false;
            if (depth == 0) return i + 1 == s.size();
        }
    }
    return false;
}

inline bool WidgetCatalog::AddManifest(const Json& m, Error* err) {
    auto fail = [err](const std::string& msg) {
        if (err) {
            err->message = "widget manifest: " + msg;
            err->line = err->col = 0;
        }
        return false;
    };
    if (!m.IsObject()) return fail("expected an object");
    if (m["kind"].AsString() != "widget") return fail("\"kind\" must be \"widget\"");
    const std::string& name = m["name"].AsString();
    if (name.empty()) return fail("missing \"name\"");
    if (FindWidget(name.c_str())) return fail("\"" + name + "\" is a stock widget type");
    if (m["call"].AsString().empty()) return fail("\"" + name + "\": missing \"call\"");
    if (m.Has("include") && !(m["include"].IsString() && (m["include"].AsString().empty() || IsSafeIncludeName(m["include"].AsString()))))
        return fail("\"" + name + "\": \"include\" must be a header name like imb_widgets.h (letters, digits, _ . / - only)");
    if (!IsSafeCallTemplate(m["call"].AsString()))
        return fail("\"" + name + "\": \"call\" must be a single call like ns::Function({label}, {value}), without ; { } # or comments");
    if (m.Has("callEnd") && !(m["callEnd"].IsString() && (m["callEnd"].AsString().empty() || IsSafeCallTemplate(m["callEnd"].AsString()))))
        return fail("\"" + name + "\": \"callEnd\" must be a single call like ns::EndFunction()");
    std::string returns = m["returns"].AsString("void");
    if (!PipeListContains("changed|clicked|open|void", returns.c_str()))
        return fail("\"" + name + "\": \"returns\" must be changed|clicked|open|void");
    const std::string pre = "\"" + name + "\": ";
    if (m.Has("props") && !m["props"].IsArray()) return fail(pre + "\"props\" must be an array");
    if (m.Has("bindings") && !m["bindings"].IsArray())
        return fail(pre + "\"bindings\" must be an array");
    if (m.Has("events") && !m["events"].IsArray()) return fail(pre + "\"events\" must be an array");

    CustomWidgetDef c;
    c.storage = std::make_shared<detail::WidgetStorage>();
    detail::WidgetStorage& st = *c.storage;
    c.manifest = m;
    c.type = name;
    c.category = m["category"].AsString("Custom");
    c.include = m["include"].AsString();
    c.call = m["call"].AsString();
    c.returns = returns;
    c.container = m["container"].AsBool(false);
    c.min_version = m["minImGuiVersion"].AsInt(18500);
    c.icon = m["icon"].AsString("fa-puzzle-piece");
    if (m["childTypes"].IsString()) {
        c.child_types = m["childTypes"].AsString();
    } else if (m["childTypes"].IsArray()) {
        if (!detail::JoinStringArray(m["childTypes"], c.child_types))
            return fail(pre + "\"childTypes\" must be strings");
    }

    const Json& props = m["props"];
    for (size_t i = 0; i < props.Size(); ++i) {
        const Json& p = props[i];
        const std::string& pname = p["name"].AsString();
        if (!p.IsObject() || pname.empty())
            return fail(pre + "prop " + std::to_string(i) + " needs a \"name\"");
        const std::string ppre = pre + "prop \"" + pname + "\" ";
        PropType type;
        if (!ParsePropType(p["type"].AsString().c_str(), type))
            return fail(ppre + "has unknown type \"" + p["type"].AsString() + "\"");
        for (const PropDef& existing : st.props)
            if (pname == existing.name) return fail(ppre + "is duplicated");
        PropDef d{};
        d.name = st.Keep(pname);
        d.type = type;
        if (type == PropType::Enum) {
            std::string values;
            if (!detail::JoinStringArray(p["values"], values) || values.empty())
                return fail(ppre + "needs \"values\"");
            d.enum_values = st.Keep(values);
        } else if (type == PropType::Flags) {
            size_t n = 0;
            const std::string& flags = p["flags"].AsString();
            if (!FlagsFor(flags.c_str(), n)) return fail(ppre + "has an unknown \"flags\" enum");
            d.enum_values = st.Keep(detail::StripImGuiPrefix(flags.c_str()));
        }
        if (p.Has("default")) {
            d.def_json = st.Keep(DumpJson(p["default"], 0, true));
        } else if (type == PropType::Enum) {
            d.def_json = st.Keep("\"" + SplitPipe(d.enum_values)[0] + "\"");
        } else {
            d.def_json = detail::DefaultJsonForType(type);
        }
        d.min = static_cast<float>(p["min"].AsDouble(0));
        d.max = static_cast<float>(p["max"].AsDouble(0));
        d.min_version = p["minImGuiVersion"].AsInt(c.min_version);
        d.hint = p["hint"].IsString() ? st.Keep(p["hint"].AsString()) : nullptr;
        if (type == PropType::Size && pname == "size") c.sizable = true;
        st.props.push_back(d);
    }
    if (m["sizable"].IsBool()) c.sizable = m["sizable"].AsBool();

    const Json& binds = m["bindings"];
    for (size_t i = 0; i < binds.Size(); ++i) {
        const Json& b = binds[i];
        const std::string& bname = b["name"].AsString();
        if (!b.IsObject() || bname.empty())
            return fail(pre + "binding " + std::to_string(i) + " needs a \"name\"");
        std::string types;
        if (!detail::JoinStringArray(b["types"], types) || types.empty())
            return fail(pre + "binding \"" + bname + "\" needs \"types\"");
        for (const BindDef& existing : st.binds)
            if (bname == existing.name) return fail(pre + "binding \"" + bname + "\" duplicated");
        st.binds.push_back(BindDef{st.Keep(bname), st.Keep(types), b["required"].AsBool(false)});
    }

    const Json& events = m["events"];
    for (size_t i = 0; i < events.Size(); ++i) {
        if (!events[i].IsString() || events[i].AsString().empty())
            return fail(pre + "events must be non-empty strings");
        c.events.push_back(events[i].AsString());
        st.events.push_back(st.Keep(events[i].AsString()));
    }

    c.props = st.props;
    c.binds = st.binds;
    c.def = detail::MakeWidget(st.Keep(c.type), st.Keep(c.category), nullptr, nullptr, nullptr,
                               c.container, c.sizable,
                               c.child_types.empty() ? nullptr : st.Keep(c.child_types),
                               c.min_version, st.Keep(c.icon));
    c.def.props = st.props.empty() ? nullptr : st.props.data();
    c.def.prop_count = static_cast<int>(st.props.size());
    c.def.binds = st.binds.empty() ? nullptr : st.binds.data();
    c.def.bind_count = static_cast<int>(st.binds.size());
    c.def.events = st.events.empty() ? nullptr : st.events.data();
    c.def.event_count = static_cast<int>(st.events.size());

    for (CustomWidgetDef& existing : custom) {
        if (existing.type == name) {
            existing = std::move(c);
            return true;
        }
    }
    custom.push_back(std::move(c));
    return true;
}

}  // namespace imforge
