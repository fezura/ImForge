#ifndef IMFORGE_INTERP_PARTS
#include "imforge_interp.h"
#else
#pragma once

// keyframe motions (imb_motion.h) for interpreted nodes, part of imforge_interp.h

namespace imforge {

namespace interp_motion {

constexpr size_t kMaxRetired = 64;

inline unsigned long long HashJson(const Json& j, unsigned long long h) {
    auto mix = [&h](const void* p, size_t n) {
        const unsigned char* b = static_cast<const unsigned char*>(p);
        for (size_t i = 0; i < n; ++i) h = (h ^ b[i]) * 1099511628211ull;
    };
    unsigned char k = static_cast<unsigned char>(j.kind);
    mix(&k, 1);
    switch (j.kind) {
    case Json::Kind::Bool: mix(&j.b, sizeof(j.b)); break;
    case Json::Kind::Number: mix(&j.num, sizeof(j.num)); break;
    case Json::Kind::String: mix(j.str.data(), j.str.size()); break;
    case Json::Kind::Array:
        for (const Json& e : j.arr) h = HashJson(e, h);
        break;
    case Json::Kind::Object:
        for (const auto& kv : j.obj) {
            mix(kv.first.data(), kv.first.size() + 1);
            h = HashJson(kv.second, h);
        }
        break;
    default: break;
    }
    return h;
}

inline unsigned long long HashList(const std::vector<Json>& list) {
    unsigned long long h = 1469598103934665603ull;
    for (const Json& j : list) h = HashJson(j, h);
    return h ^ list.size();
}

struct CustomAnim {
    std::string name;
    unsigned long long hash = 0;
    AnimStorage storage;
};

// registered defs must outlive the registry entry, so storage is process wide. A replaced def stays
// alive for a while in case someone still holds the pointer from FindAnim.
struct CustomStore {
    std::vector<std::unique_ptr<CustomAnim>> live;
    std::vector<std::unique_ptr<CustomAnim>> retired;
};

inline CustomStore& Store() {
    static CustomStore s;
    return s;
}

inline bool PresetKnown(const char* name) { return imb::FindAnim(name) != nullptr; }

inline void InstallPresetResolver() {
    if (!MotionPresetResolver()) MotionPresetResolver() = &PresetKnown;
}

inline const bool kPresetResolverInstalled = (InstallPresetResolver(), true);

inline imb::Trigger TriggerFromName(const std::string& name, bool* ok) {
    imb::Trigger t = imb::Trigger::Enter;
    *ok = TryMotionTriggerFromName(name.c_str(), &t);
    return t;
}

enum class Mode { None, Outer, Content };

// popups, tabs, menus and real windows aren't laid out in the parent, so their content is animated
inline Mode ModeFor(const DrawCtx& ctx) {
    const std::string& t = ctx.node.type;
    if (t == "Window") return ctx.opts.design_mode ? Mode::Outer : Mode::Content;
    if (t == "Popup" || t == "Modal" || t == "Tab" || t == "Menu") return Mode::Content;
    if (t == "MenuBar") return Mode::None;
    return Mode::Outer;
}

inline const imb::AnimDef* ResolveDef(const DrawCtx& ctx, const MotionSpec& m, bool quiet) {
    if (!m.preset.empty()) {
        const imb::AnimDef* d = imb::FindAnim(m.preset.c_str());
        if (!d && !quiet) ctx.Diag(Diagnostic::Level::Warning, "unknown motion preset '" + m.preset + "'", "anim");
        return d;
    }
    if (m.custom.empty()) {
        if (!quiet) ctx.Diag(Diagnostic::Level::Error, "motion has no preset or custom animation", "anim");
        return nullptr;
    }
    const Project* p = ctx.interp.project;
    const imb::AnimDef* d = (!p || ctx.interp.anim_names.count(m.custom)) ? imb::FindAnim(m.custom.c_str()) : nullptr;
    if (!d && !quiet) ctx.Diag(Diagnostic::Level::Error, "unknown custom animation '" + m.custom + "'", "anim");
    return d;
}

inline void AddBinding(const DrawCtx& ctx, const MotionSpec& m, bool quiet, interp_detail::MotionScope& out) {
    bool ok = false;
    imb::Trigger trigger = TriggerFromName(m.trigger, &ok);
    if (!ok) {
        if (!quiet) ctx.Diag(Diagnostic::Level::Error, "unknown motion trigger '" + m.trigger + "'", "anim");
        return;
    }
    const imb::AnimDef* def = ResolveDef(ctx, m, quiet);
    if (!def) return;
    if (out.count >= imb::detail::kMaxMotionBindings) {
        if (!quiet) ctx.Diag(Diagnostic::Level::Warning, "more than 8 motions on one item, the rest are ignored", "anim");
        return;
    }
    imb::MotionBinding b;
    b.def = def;
    b.trigger = trigger;
    b.duration_scale = m.duration_scale > 0 ? m.duration_scale : 1.0f;
    b.delay = m.delay > 0 ? m.delay : 0.0f;
    b.iterations = m.iterations < -1 ? -1 : m.iterations;
    b.stagger = m.stagger > 0 ? m.stagger : 0.0f;
    out.bindings[out.count++] = b;
}

inline bool IsDirectChild(const Node& parent, const Node& n) {
    return !parent.children.empty() && &n >= &parent.children.front() && &n <= &parent.children.back();
}

// A container's motion with a stagger plays on each direct child (delay + stagger * index) instead of
// on the container, like the older anim.enter stagger.
inline Mode Prepare(DrawCtx& ctx, interp_detail::MotionScope& s) {
    if (!ctx.opts.animations) return Mode::None;
    const Node& n = ctx.node;
    const DrawCtx* p = ctx.parent;
    bool inherits = false;
    if (p && IsDirectChild(p->node, n))
        for (const MotionSpec& m : p->node.anim.motions) inherits = inherits || m.stagger > 0;
    if (n.anim.motions.empty() && !inherits) return Mode::None;
    Mode mode = ModeFor(ctx);
    if (mode == Mode::None) {
        if (!n.anim.motions.empty())
            interp_detail::InfoOnce(ctx, "motions on " + n.type + " are not played", "anim");
        return Mode::None;
    }
    const bool delegates = !n.children.empty();
    for (const MotionSpec& m : n.anim.motions) {
        if (delegates && m.stagger > 0) {
            bool ok = false;
            TriggerFromName(m.trigger, &ok);
            if (!ok) ctx.Diag(Diagnostic::Level::Error, "unknown motion trigger '" + m.trigger + "'", "anim");
            else ResolveDef(ctx, m, false);
            continue;
        }
        AddBinding(ctx, m, false, s);
    }
    if (inherits && mode == Mode::Outer) {
        for (const MotionSpec& m : p->node.anim.motions)
            if (m.stagger > 0) AddBinding(ctx, m, true, s);
        s.options.stagger_index = ctx.sibling_index;
    }
    if (s.count == 0) return Mode::None;
    s.options.visible = mode == Mode::Content ? true : ctx.visible;
    bool forced = false;
    if (ctx.interp.ForcedTrigger(n, "hover", forced)) s.options.force_hover = forced;
    if (ctx.interp.ForcedTrigger(n, "press", forced) || ctx.interp.ForcedTrigger(n, "active", forced))
        s.options.force_press = forced;
    if (ctx.interp.hooks.motion_options) ctx.interp.hooks.motion_options(n, s.options);
    return mode;
}

inline bool BeginScope(const DrawCtx& ctx, const interp_detail::MotionScope& s) {
    return imb::BeginMotion(ctx.id, s.bindings, s.count, s.options);
}

// cut to the first round(reveal * n) UTF-8 characters
inline std::string RevealText(const std::string& text, float reveal) {
    if (!(reveal < 1.0f)) return text;
    if (!(reveal > 0.0f)) return std::string();
    size_t chars = 0;
    for (unsigned char c : text) chars += (c & 0xC0) != 0x80 ? 1 : 0;
    size_t keep = static_cast<size_t>(std::floor(reveal * static_cast<float>(chars) + 0.5f));
    if (keep >= chars) return text;
    size_t seen = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        if ((static_cast<unsigned char>(text[i]) & 0xC0) == 0x80) continue;
        if (seen++ == keep) return text.substr(0, i);
    }
    return text;
}

inline void TriggerFor(const DrawCtx& ctx, const char* event) {
    if (!ctx.opts.animations || !event) return;
    for (const MotionSpec& m : ctx.node.anim.motions) {
        if (m.on != event) continue;
        bool ok = false;
        imb::Trigger t = TriggerFromName(m.trigger, &ok);
        if (ok) imb::TriggerMotion(ctx.id, t);
    }
}

inline bool Listens(const Node& n, const char* event) {
    for (const MotionSpec& m : n.anim.motions)
        if (m.on == event) return true;
    return false;
}

}  // namespace interp_motion

// Registers AnimJson objects (project animations, the live `animations` message) in the imb registry,
// replacing animations with the same name. Unchanged entries are skipped. Returns how many are registered.
inline int RegisterCustomAnimations(const std::vector<Json>& anims, std::vector<std::string>* errors = nullptr) {
    using namespace interp_motion;
    CustomStore& store = Store();
    int n = 0;
    for (const Json& a : anims) {
        if (!a.IsObject() || !a["name"].IsString()) {
            if (errors) errors->push_back("animation without a name");
            continue;
        }
        const std::string& name = a["name"].AsString();
        unsigned long long h = HashJson(a, 1469598103934665603ull);
        CustomAnim* existing = nullptr;
        for (const auto& e : store.live)
            if (e->name == name) existing = e.get();
        if (existing && existing->hash == h) {
            if (imb::FindAnim(name.c_str()) != existing->storage.Def()) imb::RegisterAnim(existing->storage.Def());
            ++n;
            continue;
        }
        std::unique_ptr<CustomAnim> fresh(new CustomAnim());
        Error err;
        if (!AnimFromJson(a, fresh->storage, &err)) {
            if (errors) errors->push_back(err.message);
            continue;
        }
        fresh->name = name;
        fresh->hash = h;
        imb::RegisterAnim(fresh->storage.Def());
        for (auto& e : store.live) {
            if (e->name != name) continue;
            store.retired.push_back(std::move(e));
            e = std::move(fresh);
            break;
        }
        if (fresh) store.live.push_back(std::move(fresh));
        if (store.retired.size() > kMaxRetired) store.retired.erase(store.retired.begin());
        ++n;
    }
    return n;
}

inline void Interpreter::SyncAnimations() {
    interp_motion::InstallPresetResolver();
    if (!project) return;
    unsigned long long h = interp_motion::HashList(project->animations);
    if (project == anim_project && h == anim_hash) return;
    anim_project = project;
    anim_hash = h;
    anim_errors.clear();
    anim_names.clear();
    for (const Json& a : project->animations)
        if (a.IsObject() && a["name"].IsString()) anim_names[a["name"].AsString()] = 1;
    RegisterCustomAnimations(project->animations, &anim_errors);
}

}  // namespace imforge

#endif
