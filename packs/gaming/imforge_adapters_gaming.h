#pragma once

// interpreter draw functions for the gaming widgets (builder canvas + live client)

#include "imforge/imforge_pack_adapter.h"
#include "imb_gaming.h"

namespace imforge {
namespace adapters_gaming {

inline ImVec4* OptVec4(const DrawCtx& ctx, const char* slot) {
    return ctx.HasBinding(slot) ? reinterpret_cast<ImVec4*>(pack::BindFloats(ctx, slot, 4)) : nullptr;
}

// overlays stay inline on the builder canvas so they can be seen and selected there
inline int Corner(const DrawCtx& ctx) {
    if (ctx.Design()) return imb::game::Corner_None;
    int c = pack::Enum(ctx, "corner", 0) - 1;
    return c < imb::game::Corner_None || c > imb::game::Corner_BottomRight ? imb::game::Corner_None : c;
}

inline void Seed(const DrawCtx& ctx, const char* slot, const Value& v) {
    if (ctx.HasBinding(slot)) pack::SeedMock(ctx, slot, v);
}

// int[N] / std::vector<int> binding, or `want` local ints when the slot isn't bound
inline int* IntArray(const DrawCtx& ctx, const char* slot, int want, int* count) {
    void* p = nullptr;
    CType t;
    *count = want;
    if (ctx.HasBinding(slot) && ctx.Binding(slot, &p, &t) && p) {
        if (t.kind == CKind::IntArray && t.count > 0) {
            *count = t.count < want ? t.count : want;
            return static_cast<int*>(p);
        }
        if (t.kind == CKind::Vector && t.elem && (t.elem->kind == CKind::I32 || t.elem->kind == CKind::U32) &&
            t.vec_data && t.vec_size) {
            int n = static_cast<int>(t.vec_size(p));
            *count = n < want ? n : want;
            return static_cast<int*>(t.vec_data(p));
        }
        if ((t.kind == CKind::I32 || t.kind == CKind::U32) && want >= 1) {
            *count = 1;
            return static_cast<int*>(p);
        }
        ctx.Diag(Diagnostic::Level::Error, "binding '" + ctx.BindingPath(slot) + "' is not an int array (" + t.name + ")", slot);
    }
    std::vector<int> zero(static_cast<size_t>(want > 0 ? want : 1), 0);
    std::string& bytes = pack::LocalBytes(ctx, slot, sizeof(int) * zero.size(), zero.data());
    return reinterpret_cast<int*>(&bytes[0]);
}

inline bool EspBoxPreview(DrawCtx& ctx) {
    Seed(ctx, "box", Value(true));
    Seed(ctx, "boxStyle", Value(1LL));
    Seed(ctx, "healthBar", Value(true));
    Seed(ctx, "name", Value(true));
    Seed(ctx, "distance", Value(true));
    Seed(ctx, "outline", Value(true));
    Seed(ctx, "health", Value(0.72));
    Seed(ctx, "boxColor", Value(ImVec4(1.0f, 0.34f, 0.34f, 1.0f)));
    std::string id = pack::Label(ctx);
    std::string name = pack::Text(ctx, "nameText");
    imb::game::EspBoxPreviewFields(id.c_str(), pack::OptBool(ctx, "box"), pack::OptInt(ctx, "boxStyle"),
                                   OptVec4(ctx, "boxColor"), pack::OptBool(ctx, "healthBar"),
                                   pack::OptInt(ctx, "healthSide"), pack::OptBool(ctx, "name"),
                                   pack::OptBool(ctx, "distance"), pack::OptBool(ctx, "outline"),
                                   pack::OptBool(ctx, "fill"), pack::OptBool(ctx, "headDot"),
                                   pack::OptFloat(ctx, "health"), name.c_str(), pack::Float(ctx, "distanceValue", 38.0f),
                                   pack::Float(ctx, "thickness", 1.5f), pack::Size(ctx));
    return false;
}

inline void SeedCrosshair(const DrawCtx& ctx) {
    Seed(ctx, "gap", Value(4.0));
    Seed(ctx, "length", Value(7.0));
    Seed(ctx, "thickness", Value(2.0));
    Seed(ctx, "outline", Value(true));
    Seed(ctx, "color", Value(ImVec4(0.3f, 1.0f, 0.45f, 1.0f)));
}

inline bool CrosshairPreview(DrawCtx& ctx) {
    SeedCrosshair(ctx);
    std::string id = pack::Label(ctx);
    bool clicked = imb::game::CrosshairPreviewFields(
        id.c_str(), pack::OptFloat(ctx, "gap"), pack::OptFloat(ctx, "length"), pack::OptFloat(ctx, "thickness"),
        pack::OptBool(ctx, "dot"), pack::OptBool(ctx, "outline"), pack::OptBool(ctx, "tStyle"), OptVec4(ctx, "color"),
        pack::Size(ctx));
    return pack::Fire(ctx, "onClick", clicked);
}

inline bool CrosshairEditor(DrawCtx& ctx) {
    SeedCrosshair(ctx);
    std::string label = pack::Label(ctx);
    bool changed = imb::game::CrosshairEditorFields(
        label.c_str(), pack::OptFloat(ctx, "gap"), pack::OptFloat(ctx, "length"), pack::OptFloat(ctx, "thickness"),
        pack::OptBool(ctx, "dot"), pack::OptBool(ctx, "outline"), pack::OptBool(ctx, "tStyle"), OptVec4(ctx, "color"));
    return pack::Fire(ctx, "onChange", changed);
}

inline bool FovCirclePreview(DrawCtx& ctx) {
    Seed(ctx, "radius", Value(120.0));
    std::string id = pack::Label(ctx);
    bool changed = imb::game::FovCirclePreview(id.c_str(), pack::OptFloat(ctx, "radius"), pack::Float(ctx, "referenceHeight", 1080.0f),
                                               pack::Color(ctx, "color"), pack::Bool(ctx, "filled", true),
                                               pack::Bool(ctx, "editable", true), pack::Size(ctx));
    return pack::Fire(ctx, "onChange", changed);
}

inline bool RadarMiniMap(DrawCtx& ctx) {
    static const float kDemo[] = {0.0f, 0.55f, 1.0f, -0.4f, 0.3f, 1.0f, 0.6f, -0.2f, 0.0f, -0.3f, -0.6f, 0.0f, 1.3f, 0.9f, 1.0f};
    std::string id = pack::Label(ctx);
    const float* pts = kDemo;
    int n = IM_ARRAYSIZE(kDemo);
    if (ctx.HasBinding("points")) {
        int count = 0;
        float* bound = ctx.BindFloatArray("points", &count);
        pts = bound;
        n = bound ? count : 0;
    } else if (!ctx.Design()) {
        pts = nullptr;
        n = 0;
    }
    float* rot = pack::OptFloat(ctx, "rotation");
    imb::game::RadarMiniMapPacked(id.c_str(), pts, n, rot, pack::Enum(ctx, "shape", 0), pack::Float(ctx, "zoom", 1.0f),
                                  pack::IntClamped(ctx, "rings", 0, 8, 3), pack::Bool(ctx, "sweep", true), pack::Size(ctx));
    return false;
}

inline bool ProfileSlots(DrawCtx& ctx) {
    std::string id = pack::Label(ctx);
    int* selected = pack::OptInt(ctx, "selected");
    int* action = pack::OptInt(ctx, "action");
    int active = pack::ExprInt(ctx, "active", -1);
    int result = imb::game::ProfileAction_None;
    void* p = nullptr;
    CType t;
    if (ctx.HasBinding("names") && ctx.Binding("names", &p, &t) && p && t.kind == CKind::Vector && t.elem &&
        t.elem->kind == CKind::StdString && t.vec_data && t.vec_size) {
        const std::string* names = static_cast<const std::string*>(t.vec_data(p));
        int n = static_cast<int>(t.vec_size(p));
        const char* items[32] = {};
        if (n > 32) n = 32;
        for (int i = 0; i < n; ++i) items[i] = names[i].c_str();
        result = imb::game::ProfileSlots(id.c_str(), items, n, selected, active, action);
    } else {
        std::string slots = pack::ZeroList(ctx, "slots");
        result = imb::game::ProfileSlots(id.c_str(), slots.c_str(), selected, active, action);
    }
    return pack::Fire(ctx, "onChange", result != imb::game::ProfileAction_None);
}

inline bool HotkeyList(DrawCtx& ctx) {
    std::string id = pack::Label(ctx);
    std::vector<std::string> actions = pack::List(ctx, "actions");
    int want = static_cast<int>(actions.size() > 64 ? 64 : actions.size());
    int count = 0;
    int* keys = IntArray(ctx, "keys", want, &count);
    const char* items[64] = {};
    for (int i = 0; i < want; ++i) items[i] = actions[static_cast<size_t>(i)].c_str();
    int* changed_index = pack::OptInt(ctx, "changedIndex");
    bool changed = imb::game::HotkeyList(id.c_str(), items, keys, count, changed_index);
    return pack::Fire(ctx, "onChange", changed);
}

inline bool Watermark(DrawCtx& ctx) {
    std::string text = pack::Text(ctx, "text");
    std::string extra = pack::Text(ctx, "extra");
    int corner = Corner(ctx);
    imb::game::Watermark((text + "###" + ctx.node.id).c_str(), extra.c_str(), corner, pack::Bool(ctx, "fps", true),
                         pack::Bool(ctx, "clock", true));
    if (corner != imb::game::Corner_None) pack::DesignPlaceholder(ctx, "Watermark");
    return false;
}

inline bool FpsPingCounter(DrawCtx& ctx) {
    std::string id = pack::Label(ctx);
    int corner = Corner(ctx);
    imb::game::FpsPingCounter(id.c_str(), pack::ExprFloat(ctx, "fps", -1.0f), pack::ExprFloat(ctx, "ping", -1.0f), corner,
                              pack::Bool(ctx, "graph", true));
    if (corner != imb::game::Corner_None) pack::DesignPlaceholder(ctx, "FPS / ping");
    return false;
}

inline bool PlayerListRow(DrawCtx& ctx) {
    std::string id = pack::Label(ctx);
    std::string name = pack::Text(ctx, "name");
    Value sel;
    bool selected = pack::ExprValue(ctx, "selected", sel) && sel.ToBool();
    int r = imb::game::PlayerListRow(id.c_str(), name.c_str(), pack::ExprInt(ctx, "team", 1), pack::ExprFloat(ctx, "health", 1.0f),
                                     pack::ExprInt(ctx, "flags", 0), pack::ExprInt(ctx, "ping", -1), selected);
    return pack::Fire(ctx, "onClick", r != imb::game::PlayerRow_None);
}

inline bool TeamColorPicker(DrawCtx& ctx) {
    std::string label = pack::Label(ctx);
    std::string a = pack::Text(ctx, "nameA"), b = pack::Text(ctx, "nameB");
    bool changed = imb::game::TeamColorPicker(label.c_str(), OptVec4(ctx, "teamA"), OptVec4(ctx, "teamB"), a.c_str(),
                                              b.c_str(), pack::Bool(ctx, "presets", true));
    return pack::Fire(ctx, "onChange", changed);
}

inline bool IconRailNav(DrawCtx& ctx) {
    std::string id = pack::Label(ctx);
    std::string icons = pack::IconZeroList(ctx, "icons");
    std::string tips = pack::ZeroList(ctx, "tooltips");
    bool changed = imb::game::IconRailNav(id.c_str(), icons.c_str(), tips.c_str(), pack::OptInt(ctx, "index"));
    return pack::Fire(ctx, "onChange", changed);
}

inline bool CategoryCard(DrawCtx& ctx) {
    std::string title = pack::Label(ctx, "title");
    std::string desc = pack::Text(ctx, "description");
    std::string icon = pack::Icon(ctx, pack::Text(ctx, "icon"));
    bool changed = imb::game::CategoryCard(title.c_str(), desc.c_str(), icon.c_str(), pack::OptBool(ctx, "enabled"),
                                           pack::Size(ctx), pack::Color(ctx, "accent"));
    return pack::Fire(ctx, "onChange", changed);
}

inline bool HealthArmorBar(DrawCtx& ctx) {
    std::string id = pack::Label(ctx);
    imb::game::HealthArmorBar(id.c_str(), pack::ExprFloat(ctx, "health", 75.0f), pack::ExprFloat(ctx, "armor", 50.0f),
                              pack::Float(ctx, "maxHealth", 100.0f), pack::Float(ctx, "maxArmor", 100.0f),
                              pack::Bool(ctx, "text", true), pack::Bool(ctx, "ticks", true), 0.0f);
    return false;
}

inline bool KillFeedEntry(DrawCtx& ctx) {
    std::string id = pack::Label(ctx);
    std::string killer = pack::Text(ctx, "killer"), victim = pack::Text(ctx, "victim"), weapon = pack::Text(ctx, "weapon");
    float life = pack::Float(ctx, "lifetime", 6.0f);
    float age = pack::ExprFloat(ctx, "age", 1.0f);
    // replay the slide/fade on the canvas so the timing can be judged
    if (ctx.Design() && pack::Bool(ctx, "previewLoop", true) && life > 0.0f)
        age = std::fmod(static_cast<float>(ImGui::GetTime()), life);
    bool alive = imb::game::KillFeedEntry(id.c_str(), killer.c_str(), victim.c_str(), weapon.c_str(), age,
                                          pack::ExprInt(ctx, "killerTeam", 0), pack::ExprInt(ctx, "victimTeam", 1),
                                          pack::ExprInt(ctx, "flags", 0), life);
    if (!alive) pack::DesignPlaceholder(ctx, "Kill feed");
    return false;
}

inline bool NotificationBanner(DrawCtx& ctx) {
    pack::SeedMock(ctx, "open", Value(true));
    std::string id = pack::Label(ctx);
    std::string text = pack::Text(ctx, "text"), action = pack::Text(ctx, "action");
    bool* open = pack::OptBool(ctx, "open");
    bool overlay = pack::Bool(ctx, "overlay", false) && !ctx.Design();
    bool clicked = imb::game::NotificationBanner(id.c_str(), open, text.c_str(), action.c_str(), pack::Enum(ctx, "kind", 0),
                                                 overlay, pack::Float(ctx, "timeout", 0.0f));
    if (overlay || (open && !*open)) pack::DesignPlaceholder(ctx, "Banner");
    return pack::Fire(ctx, "onClick", clicked);
}

}  // namespace adapters_gaming

inline void RegisterPackWidgets_gaming(Interpreter& in) {
    namespace a = adapters_gaming;
    static const pack::Entry kEntries[] = {
        {"EspBoxPreview", &a::EspBoxPreview, nullptr},
        {"CrosshairPreview", &a::CrosshairPreview, nullptr},
        {"CrosshairEditor", &a::CrosshairEditor, nullptr},
        {"FovCirclePreview", &a::FovCirclePreview, nullptr},
        {"RadarMiniMap", &a::RadarMiniMap, nullptr},
        {"ProfileSlots", &a::ProfileSlots, nullptr},
        {"HotkeyList", &a::HotkeyList, nullptr},
        {"Watermark", &a::Watermark, nullptr},
        {"FpsPingCounter", &a::FpsPingCounter, nullptr},
        {"PlayerListRow", &a::PlayerListRow, nullptr},
        {"TeamColorPicker", &a::TeamColorPicker, nullptr},
        {"IconRailNav", &a::IconRailNav, nullptr},
        {"CategoryCard", &a::CategoryCard, nullptr},
        {"HealthArmorBar", &a::HealthArmorBar, nullptr},
        {"KillFeedEntry", &a::KillFeedEntry, nullptr},
        {"NotificationBanner", &a::NotificationBanner, nullptr},
    };
    pack::RegisterAll(in, kEntries);
}

}  // namespace imforge
