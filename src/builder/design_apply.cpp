#include "design_apply.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace imforge::builder {

namespace {

bool Fail(Error* err, const std::string& msg) {
    if (err) err->message = msg;
    return false;
}

std::string Lower(const std::string& s) {
    std::string out = s;
    for (char& c : out) c = (char)std::tolower((unsigned char)c);
    return out;
}

std::string ThemeFileFor(const std::string& name) {
    std::string stem;
    for (char c : name) {
        bool ok = std::isalnum((unsigned char)c) || c == '_' || c == '-' || c == '.' || c == ' ' || (unsigned char)c >= 0x80;
        stem.push_back(ok ? c : '_');
    }
    if (stem.empty() || stem == "." || stem == "..") stem.insert(0, "_");
    return "themes/" + stem + ".theme.json";
}

std::string UniqueThemeFile(const Project& p, const std::string& name) {
    const std::string base = ThemeFileFor(name);
    auto taken = [&](const std::string& f) {
        for (const Theme& t : p.themes)
            if (Lower(t.file) == Lower(f)) return true;
        return false;
    };
    if (!taken(base)) return base;
    const std::string stem = base.substr(0, base.size() - 11);
    for (int i = 2;; ++i) {
        std::string cand = stem + "_" + std::to_string(i) + ".theme.json";
        if (!taken(cand)) return cand;
    }
}

bool KnownTrigger(const std::string& t) {
    static const char* const kTriggers[] = {"enter", "exit", "hover", "press", "focus", "loop", "change", "attention"};
    for (const char* k : kTriggers)
        if (t == k) return true;
    return false;
}

// designs come from the internet: no absolute, UNC, drive or parent-directory font paths
bool IsPlainRelativePath(const std::string& p) {
    if (p.empty() || p.size() > 260 || p[0] == '/') return false;
    for (char ch : p) {
        unsigned char c = (unsigned char)ch;
        if (c < 0x20 || c == 0x7F || c == '\\' || c == ':') return false;
    }
    size_t start = 0;
    while (start <= p.size()) {
        size_t end = p.find('/', start);
        if (end == std::string::npos) end = p.size();
        std::string seg = p.substr(start, end - start);
        if (seg == ".." || (seg.empty() && end != p.size())) return false;
        start = end + 1;
    }
    return p.back() != '/';
}

// class names become C++ identifiers in exported code
bool IsClassName(const std::string& n) {
    if (n.empty() || n.size() > 64) return false;
    for (char c : n) {
        bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
        if (!ok) return false;
    }
    return true;
}

std::string RoleFontName(const std::string& role) {
    std::string out = role;
    if (!out.empty()) out[0] = (char)std::toupper((unsigned char)out[0]);
    return out;
}

Json Hex(float r, float g, float b, float a) {
    Vec4 c;
    c.x = r;
    c.y = g;
    c.z = b;
    c.w = a;
    return Json(ColorToHex(c));
}

Json Num(double v) { return Json(std::round(v * 100.0) / 100.0); }

template <class F>
void WalkNodes(Node& n, F&& f) {
    f(n);
    for (Node& c : n.children) WalkNodes(c, f);
    for (auto& s : n.slots)
        for (Node& c : s.second) WalkNodes(c, f);
}

bool HasClass(const Node& n, const char* cls) {
    for (const std::string& c : n.style.classes)
        if (c == cls) return true;
    return false;
}

void SetFx(Node& n, const char* type, Json params) {
    for (FxSpec& fx : n.fx)
        if (fx.type == type) {
            fx.params = std::move(params);
            return;
        }
    FxSpec fx;
    fx.type = type;
    fx.params = std::move(params);
    n.fx.push_back(std::move(fx));
}

bool RemoveFx(Node& n, const char* type) {
    for (auto it = n.fx.begin(); it != n.fx.end(); ++it)
        if (it->type == type) {
            n.fx.erase(it);
            return true;
        }
    return false;
}

bool IsListContainer(const Node& n) {
    static const char* const kTypes[] = {"VStack", "ListBox", "Child", "Grid"};
    for (const char* t : kTypes)
        if (n.type == t) return n.children.size() >= 2;
    return false;
}

bool IsButtonType(const std::string& type) {
    if (type == "Button" || type == "SmallButton") return true;
    return type.size() > 6 && type.compare(type.size() - 6, 6, "Button") == 0;
}

bool TargetMatches(const std::string& target, const Node& n, bool is_root) {
    if (target == "window") return is_root && n.type == "Window";
    if (target == "root" || target == "panel") return is_root;
    if (target == "list") return IsListContainer(n);
    if (target == "button") return IsButtonType(n.type);
    return Lower(n.type) == target;
}

int ApplyMotion(Node& n, const MotionSpec& spec) {
    if (n.locked) return 0;
    std::vector<MotionSpec>& list = n.anim.motions;
    for (MotionSpec& m : list)
        if (m.trigger == spec.trigger) {
            m = spec;
            return 1;
        }
    list.push_back(spec);
    return 1;
}

int ApplyMotions(Project& p, const Json& motions, DesignApplyResult& r) {
    int count = 0;
    for (const auto& kv : motions.obj) {
        MotionSpec spec;
        std::string key = Lower(kv.first);
        size_t dot = key.find('.');
        std::string target = key.substr(0, dot);
        if (dot != std::string::npos) spec.trigger = key.substr(dot + 1);
        if (kv.second.IsString()) {
            spec.preset = kv.second.AsString();
        } else if (kv.second.IsObject()) {
            const Json& o = kv.second;
            spec.preset = o["preset"].AsString();
            spec.custom = o["custom"].AsString();
            if (o["trigger"].IsString()) spec.trigger = o["trigger"].AsString();
            spec.duration_scale = (float)o["durationScale"].AsDouble(1.0);
            spec.delay = (float)o["delay"].AsDouble(0.0);
            spec.stagger = (float)o["stagger"].AsDouble(0.0);
            spec.iterations = o["iterations"].AsInt(-1);
        }
        if (spec.Name().empty()) {
            r.warnings.push_back("motion \"" + kv.first + "\" has no preset");
            continue;
        }
        if (!KnownTrigger(spec.trigger)) {
            r.warnings.push_back("motion \"" + kv.first + "\": unknown trigger \"" + spec.trigger + "\"");
            continue;
        }
        if (!(spec.duration_scale > 0.0f && spec.duration_scale <= 1000.0f) || !(spec.delay >= 0.0f && spec.delay <= 3600.0f) ||
            !(spec.stagger >= 0.0f && spec.stagger <= 60.0f) || spec.iterations < -1 || spec.iterations > 1000) {
            r.warnings.push_back("motion \"" + kv.first + "\": durationScale, delay, stagger or iterations is out of range, skipped");
            continue;
        }
        if (target == "list" && spec.stagger <= 0.0f) spec.stagger = 0.05f;
        for (Panel& panel : p.panels) {
            Node* root = &panel.root;
            WalkNodes(panel.root, [&](Node& n) {
                if (TargetMatches(target, n, &n == root)) count += ApplyMotion(n, spec);
            });
        }
    }
    return count;
}

Vec4 AccentOf(const Json& design, const Theme& theme) {
    bool ok = false;
    Vec4 c = ParseColor(design["widgetStyle"]["accent"].AsString(), &ok);
    if (ok) return c;
    for (const char* name : {"ButtonActive", "CheckMark", "SliderGrabActive"})
        for (const auto& kv : theme.colors)
            if (kv.first == name) return kv.second;
    return ParseColor("#7c5cffff");
}

int ApplyFx(Project& p, const Json& design, const Theme& theme) {
    const Json& fx = design["fx"];
    if (!fx.IsObject()) return 0;
    int count = 0;
    for (Panel& panel : p.panels) {
        Node& root = panel.root;
        if (root.type != "Window" || root.locked) continue;
        if (fx["windowShadow"].IsNumber()) {
            float s = (float)fx["windowShadow"].AsDouble();
            if (s > 0.0f) {
                Json params = Json::Object();
                params.Set("size", Num(6.0 + 26.0 * s));
                params.Set("color", Hex(0, 0, 0, std::fmin(1.0f, s * 0.85f)));
                Json off = Json::Object();
                off.Set("x", Json(0));
                off.Set("y", Num(2.0 + 6.0 * s));
                params.Set("offset", std::move(off));
                SetFx(root, "shadow", std::move(params));
                ++count;
            } else if (RemoveFx(root, "shadow")) {
                ++count;
            }
        }
        if (fx["windowBlur"].IsNumber()) {
            float b = (float)fx["windowBlur"].AsDouble();
            if (b > 0.0f) {
                Json params = Json::Object();
                params.Set("radius", Num(b <= 1.0f ? 4.0 + 20.0 * b : b));
                SetFx(root, "blur", std::move(params));
                ++count;
            } else if (RemoveFx(root, "blur")) {
                ++count;
            }
        }
    }
    if (fx["accentGlow"].IsNumber()) {
        float g = (float)fx["accentGlow"].AsDouble();
        Vec4 a = AccentOf(design, theme);
        for (Panel& panel : p.panels)
            WalkNodes(panel.root, [&](Node& n) {
                if (n.locked || !(HasClass(n, "accent-button") || HasClass(n, "accent"))) return;
                if (g > 0.0f) {
                    Json params = Json::Object();
                    params.Set("size", Num(4.0 + 12.0 * g));
                    params.Set("color", Hex(a.x, a.y, a.z, std::fmin(1.0f, g * 0.7f)));
                    SetFx(n, "glow", std::move(params));
                    ++count;
                } else if (RemoveFx(n, "glow")) {
                    ++count;
                }
            });
    }
    return count;
}

int ApplyFonts(Project& p, const Json& fonts, DesignApplyResult& r) {
    int count = 0;
    for (const Json& f : fonts.arr) {
        if (!f.IsObject()) continue;
        std::string role = Lower(f["role"].AsString("ui"));
        std::string family = f["family"].AsString("Inter");
        std::string file = f["file"].AsString();
        if (!file.empty() && !IsPlainRelativePath(file)) {
            r.warnings.push_back("font \"" + role + "\": file path must be relative to the project, using the default font file");
            file.clear();
        }
        if (file.empty()) file = DesignFontFile(family, f["weight"].AsString());
        float size = (float)f["size"].AsDouble(16.0);
        if (size < 6.0f || size > 96.0f) {
            r.warnings.push_back("font \"" + role + "\": size out of range, using 16");
            size = 16.0f;
        }
        FontSpec* target = nullptr;
        if (role == "ui" || role == "default" || role == "body") {
            if (p.fonts.empty()) {
                FontSpec fs;
                fs.name = "Default";
                p.fonts.push_back(fs);
            }
            target = &p.fonts.front();
        } else {
            std::string name = RoleFontName(role);
            for (FontSpec& fs : p.fonts)
                if (fs.name == name) target = &fs;
            if (!target) {
                FontSpec fs;
                fs.name = name;
                if (!p.fonts.empty()) fs.merge_icons = p.fonts.front().merge_icons;
                p.fonts.push_back(fs);
                target = &p.fonts.back();
            }
        }
        target->file = file;
        target->size = size;
        ++count;
    }
    return count;
}

}  // namespace

std::string DesignName(const Json& design) {
    std::string n = design["name"].AsString();
    return n.empty() ? design["title"].AsString() : n;
}

std::string DesignTitle(const Json& design) {
    std::string t = design["title"].AsString();
    return t.empty() ? DesignName(design) : t;
}

std::string DesignFontFile(const std::string& family, const std::string& weight) {
    std::string f = Lower(family), w = Lower(weight);
    if (f.find("mono") != std::string::npos || f.find("code") != std::string::npos) return "fonts/JetBrainsMono-Regular.ttf";
    bool bold = w.find("bold") != std::string::npos || w.find("heavy") != std::string::npos ||
                w.find("black") != std::string::npos || std::atoi(w.c_str()) >= 600;
    return bold ? "fonts/Inter-Bold.ttf" : "fonts/Inter-Regular.ttf";
}

bool ThemeFromDesign(const Json& design, Theme& out, Error* err) {
    if (!design.IsObject() || design["kind"].AsString() != "design") return Fail(err, "not a design file (kind must be \"design\")");
    std::string name = DesignName(design);
    if (name.empty()) return Fail(err, "design: missing \"name\"");
    const Json& th = design["theme"];
    if (!th.IsObject()) return Fail(err, "design \"" + name + "\": missing \"theme\" object");
    Theme t;
    t.name = name;
    const std::string ctx = "design \"" + name + "\"";
    if (!core_detail::ColorMapFromJson(th["colors"], t.colors, err, ctx) || !core_detail::VarMapFromJson(th["vars"], t.vars, err, ctx))
        return false;
    out = std::move(t);
    return true;
}

bool ApplyDesign(Project& project, const Json& design, unsigned mode, DesignApplyResult* result, Error* err) {
    DesignApplyResult local;
    DesignApplyResult& r = result ? *result : local;
    r = DesignApplyResult();
    Theme theme;
    if (!ThemeFromDesign(design, theme, err)) return false;
    std::vector<std::pair<std::string, StyleOverride>> classes;
    if (mode & kApplyClasses) {
        const Json& sc = design["styleClasses"];
        if (!sc.IsNull() && !sc.IsObject()) return Fail(err, "design \"" + theme.name + "\": \"styleClasses\" must be an object");
        for (const auto& kv : sc.obj) {
            if (!IsClassName(kv.first)) {
                r.warnings.push_back("style class \"" + kv.first.substr(0, 40) + "\" skipped: use letters, digits, - and _ only");
                continue;
            }
            StyleOverride so;
            if (!core_detail::StyleFromJson(kv.second, so, err, "design \"" + theme.name + "\": class \"" + kv.first + "\""))
                return false;
            classes.emplace_back(kv.first, std::move(so));
        }
    }

    if (mode & kApplyTheme) {
        Theme* existing = nullptr;
        for (Theme& t : project.themes)
            if (t.name == theme.name) existing = &t;
        if (existing) {
            existing->colors = theme.colors;
            existing->vars = theme.vars;
        } else {
            theme.file = UniqueThemeFile(project, theme.name);
            project.themes.push_back(theme);
        }
        project.active_theme = theme.name;
        r.theme = theme.name;
    }
    for (auto& kv : classes) {
        bool replaced = false;
        for (auto& e : project.style_classes)
            if (e.first == kv.first) {
                e.second = kv.second;
                replaced = true;
            }
        if (!replaced) project.style_classes.emplace_back(kv.first, kv.second);
        ++r.classes;
    }
    if ((mode & kApplyFonts) && design["fonts"].IsArray()) r.fonts = ApplyFonts(project, design["fonts"], r);
    if (mode & kApplyFx) r.fx = ApplyFx(project, design, theme);
    if ((mode & kApplyMotions) && design["motions"].IsObject()) r.motions = ApplyMotions(project, design["motions"], r);
    return true;
}

}  // namespace imforge::builder
