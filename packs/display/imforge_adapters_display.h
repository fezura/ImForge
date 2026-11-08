#pragma once

// interpreter draw functions for the display widgets (builder canvas + live client)

#include "imforge/imforge_pack_adapter.h"
#include "imb_display.h"

namespace imforge {
namespace adapters_display {

namespace sample {
const float kSeries[] = {3, 4, 3.5f, 5, 6.5f, 6, 7.2f, 6.8f, 8, 7.4f, 9, 8.6f, 10.2f, 9.5f, 11};
const float kBars[] = {4, 7, 5, 9, 6, 8, 3};
const float kMarkers[] = {4.0f, 12.5f, 21.0f};
}  // namespace sample

// bound float[N] / std::vector<float>, sample data on the canvas when unbound, nothing in the app
inline const float* Floats(const DrawCtx& ctx, const char* slot, int& count, const float* demo, int demo_count) {
    count = 0;
    const PropValue* b = ctx.node.Bind(slot);
    void* p = nullptr;
    CType t;
    bool found = false;
    if (b && b->IsParam()) found = ctx.ResolveParamStorage(b->Param(), &p, &t);
    else if (b && b->raw.IsString() && !b->raw.AsString().empty()) found = ctx.ResolveStorage(b->raw.AsString(), &p, &t);
    if (found) return ctx.BindFloatArray(slot, &count);
    if (b && !ctx.Design()) ctx.Diag(Diagnostic::Level::Error, "cannot resolve binding '" + ctx.BindingPath(slot) + "'", slot);
    if (!ctx.Design() || !demo) return nullptr;
    count = demo_count;
    return demo;
}

inline const char* OrNull(const std::string& s) { return s.empty() ? nullptr : s.c_str(); }

inline bool RingProgress(DrawCtx& ctx) {
    std::string label = pack::Label(ctx);
    imb::disp::RingStyle rs;
    rs.radius = pack::Px(ctx, "radius");
    rs.thickness = pack::Px(ctx, "thickness");
    rs.color = pack::Color(ctx, "color");
    rs.color2 = pack::Color(ctx, "color2");
    rs.show_value = pack::Bool(ctx, "showValue", true);
    float f = pack::Bool(ctx, "spin") ? -1.0f : pack::ExprFloat(ctx, "fraction", 0.65f);
    imb::disp::RingProgress(label.c_str(), f, rs);
    return false;
}

inline bool RadialGauge(DrawCtx& ctx) {
    std::string label = pack::Label(ctx);
    std::string format = pack::Text(ctx, "format");
    imb::disp::RadialGauge(label.c_str(), pack::ExprFloat(ctx, "value", 62.0f), pack::Float(ctx, "min"),
                           pack::Float(ctx, "max", 100.0f), pack::Float(ctx, "warnFrom", 70.0f),
                           pack::Float(ctx, "dangerFrom", 90.0f), pack::Px(ctx, "radius"),
                           format.empty() ? "%.0f" : format.c_str());
    return false;
}

inline bool Sparkline(DrawCtx& ctx) {
    std::string label = pack::Label(ctx);
    int n = 0;
    const float* v = Floats(ctx, "values", n, sample::kSeries, IM_ARRAYSIZE(sample::kSeries));
    imb::disp::Sparkline(label.c_str(), v, n, pack::Size(ctx), pack::Color(ctx, "color"), pack::Bool(ctx, "fill", true));
    return false;
}

inline bool MiniBars(DrawCtx& ctx) {
    std::string label = pack::Label(ctx);
    int n = 0;
    const float* v = Floats(ctx, "values", n, sample::kBars, IM_ARRAYSIZE(sample::kBars));
    std::vector<std::string> names = pack::List(ctx, "labels");
    std::vector<const char*> ptrs;
    ptrs.reserve(names.size());
    for (const std::string& s : names) ptrs.push_back(s.c_str());
    imb::disp::MiniBars(label.c_str(), v, n, ptrs.empty() ? nullptr : ptrs.data(), (int)ptrs.size(), pack::Size(ctx),
                        pack::Color(ctx, "color"));
    return false;
}

inline bool FrameGraph(DrawCtx& ctx) {
    std::string label = pack::Label(ctx);
    imb::disp::FrameGraphStyle fs;
    fs.budget_ms = pack::Float(ctx, "budgetMs", 1000.0f / 60.0f);
    fs.size = pack::Size(ctx);
    int n = 0;
    const float* v = Floats(ctx, "samples", n, nullptr, 0);
    if (v) imb::disp::FrameGraph(label.c_str(), v, n, 0, fs);
    else imb::disp::FrameGraph(label.c_str(), fs);
    return false;
}

inline bool LoadingBar(DrawCtx& ctx) {
    std::string label = pack::Label(ctx);
    float f = pack::Bool(ctx, "indeterminate") ? -1.0f : pack::ExprFloat(ctx, "fraction", 0.4f);
    imb::disp::LoadingBar(label.c_str(), f, pack::IntClamped(ctx, "segments", 0, 256), pack::Size(ctx),
                          pack::Color(ctx, "color"));
    return false;
}

inline bool SkeletonBlock(DrawCtx& ctx) {
    static const imb::disp::SkeletonShape kShapes[] = {imb::disp::SkeletonShape::Rect, imb::disp::SkeletonShape::Line,
                                                      imb::disp::SkeletonShape::Circle};
    int shape = pack::Enum(ctx, "shape", 1);
    if (shape < 0 || shape > 2) shape = 1;
    imb::disp::SkeletonBlock(kShapes[shape], pack::Size(ctx), pack::IntClamped(ctx, "lines", 1, 64, 3));
    return false;
}

inline bool Badge(DrawCtx& ctx) {
    static const imb::disp::BadgeKind kKinds[] = {imb::disp::BadgeKind::Pill, imb::disp::BadgeKind::Count,
                                                 imb::disp::BadgeKind::Dot};
    std::string text = pack::Label(ctx, "text");
    int kind = pack::Enum(ctx, "kind");
    if (kind < 0 || kind > 2) kind = 0;
    bool attach = pack::Bool(ctx, "attach");
    imb::disp::Badge(text.c_str(), kKinds[kind], pack::ExprInt(ctx, "count", 3), pack::Color(ctx, "color"), attach);
    if (attach) pack::DesignPlaceholder(ctx, "Badge");
    return false;
}

inline bool Avatar(DrawCtx& ctx) {
    std::string name = pack::Text(ctx, "name");
    int status = pack::Enum(ctx, "status");
    if (status < 0 || status > 4) status = 0;
    imb::disp::Avatar(name.c_str(), pack::Px(ctx, "size"), (imb::disp::Status)status, ctx.GetTexture("texture"));
    return false;
}

inline bool StatusDot(DrawCtx& ctx) {
    std::string label = pack::Text(ctx, "label");
    int status = pack::Enum(ctx, "status", 1);
    if (status < 0 || status > 4) status = 1;
    imb::disp::StatusDot((imb::disp::Status)status, OrNull(label));
    return false;
}

inline bool StatCard(DrawCtx& ctx) {
    std::string title = pack::Label(ctx, "title");
    std::string format = pack::Text(ctx, "format");
    int n = 0;
    const float* spark = Floats(ctx, "spark", n, sample::kSeries, IM_ARRAYSIZE(sample::kSeries));
    imb::disp::StatCard(title.c_str(), pack::ExprFloat(ctx, "value", 12840.0f), format.empty() ? "%.0f" : format.c_str(),
                        pack::Bool(ctx, "showDelta", true), pack::ExprFloat(ctx, "delta", 12.5f), spark, n);
    return false;
}

inline bool ProgressSteps(DrawCtx& ctx) {
    std::string label = pack::Label(ctx);
    pack::SeedMock(ctx, "current", Value(1));
    std::string steps = pack::ZeroList(ctx, "steps");
    int* current = pack::BindInt(ctx, "current", 1);
    return pack::Fire(ctx, "onChange", imb::disp::ProgressSteps(label.c_str(), steps.c_str(), current));
}

inline bool TimelineScrubber(DrawCtx& ctx) {
    pack::SeedMock(ctx, "time", Value(6.0f));
    std::string label = pack::Label(ctx);
    float* t = pack::BindFloat(ctx, "time", 6.0f);
    int n = 0;
    const float* markers = Floats(ctx, "markers", n, sample::kMarkers, IM_ARRAYSIZE(sample::kMarkers));
    bool changed = imb::disp::TimelineScrubber(label.c_str(), t, pack::ExprFloat(ctx, "duration", 30.0f), markers, n);
    return pack::Fire(ctx, "onChange", changed);
}

inline bool TooltipCard(DrawCtx& ctx) {
    std::string title = pack::Label(ctx, "title");
    std::string text = pack::Text(ctx, "text");
    std::string footer = pack::Text(ctx, "footer");
    imb::disp::TooltipCardStyle ts;
    ts.header = pack::Color(ctx, "header");
    ts.footer = OrNull(footer);
    if (ctx.Design()) {
        imb::disp::TooltipCardInline(title.c_str(), text.c_str(), ts);
        return false;
    }
    imb::disp::TooltipCard(title.c_str(), text.c_str(), ts);
    pack::DesignPlaceholder(ctx, "TooltipCard");
    return false;
}

inline bool ToastStack(DrawCtx& ctx) {
    static const imb::disp::ToastCorner kCorners[] = {
        imb::disp::ToastCorner::TopLeft,    imb::disp::ToastCorner::TopRight,  imb::disp::ToastCorner::BottomLeft,
        imb::disp::ToastCorner::BottomRight, imb::disp::ToastCorner::TopCenter, imb::disp::ToastCorner::BottomCenter};
    int corner = pack::Enum(ctx, "corner", 3);
    if (corner < 0 || corner > 5) corner = 3;
    imb::disp::ToastStack(kCorners[corner]);
    pack::DesignPlaceholder(ctx, "Toasts");
    return false;
}

inline bool Breadcrumbs(DrawCtx& ctx) {
    std::string label = pack::Label(ctx);
    std::string items = pack::ZeroList(ctx, "items");
    int* clicked = pack::OptInt(ctx, "clicked");
    return pack::Fire(ctx, "onClick", imb::disp::Breadcrumbs(label.c_str(), items.c_str(), clicked));
}

inline bool KeyOverlay(DrawCtx& ctx) {
    std::string label = pack::Label(ctx);
    imb::disp::KeyOverlay(label.c_str(), pack::Px(ctx, "keySize"), pack::Bool(ctx, "mouse", true));
    return false;
}

inline bool Divider(DrawCtx& ctx) {
    static const imb::disp::DividerLook kLooks[] = {imb::disp::DividerLook::Line, imb::disp::DividerLook::Fade,
                                                   imb::disp::DividerLook::Dashed, imb::disp::DividerLook::Dotted};
    static const imb::disp::DividerAlign kAligns[] = {imb::disp::DividerAlign::Left, imb::disp::DividerAlign::Center,
                                                     imb::disp::DividerAlign::Right};
    std::string label = pack::Text(ctx, "label");
    int look = pack::Enum(ctx, "look"), align = pack::Enum(ctx, "align", 1);
    if (look < 0 || look > 3) look = 0;
    if (align < 0 || align > 2) align = 1;
    imb::disp::Divider(label.c_str(), kLooks[look], kAligns[align]);
    return false;
}

inline bool Kbd(DrawCtx& ctx) {
    std::string keys = pack::Text(ctx, "keys");
    imb::disp::Kbd(keys.c_str());
    return false;
}

inline bool EmptyState(DrawCtx& ctx) {
    static const imb::disp::EmptyIcon kIcons[] = {imb::disp::EmptyIcon::None,   imb::disp::EmptyIcon::Inbox,
                                                 imb::disp::EmptyIcon::Search, imb::disp::EmptyIcon::Folder,
                                                 imb::disp::EmptyIcon::Alert,  imb::disp::EmptyIcon::Check};
    std::string title = pack::Label(ctx, "title");
    std::string hint = pack::Text(ctx, "hint");
    std::string button = pack::Text(ctx, "button");
    int icon = pack::Enum(ctx, "icon", 1);
    if (icon < 0 || icon > 5) icon = 1;
    bool clicked = imb::disp::EmptyState(title.c_str(), OrNull(hint), OrNull(button), kIcons[icon]);
    return pack::Fire(ctx, "onClick", clicked);
}

}  // namespace adapters_display

inline void RegisterPackWidgets_display(Interpreter& in) {
    namespace a = adapters_display;
    static const pack::Entry kEntries[] = {
        {"RingProgress", &a::RingProgress, nullptr},
        {"RadialGauge", &a::RadialGauge, nullptr},
        {"Sparkline", &a::Sparkline, nullptr},
        {"MiniBars", &a::MiniBars, nullptr},
        {"FrameGraph", &a::FrameGraph, nullptr},
        {"LoadingBar", &a::LoadingBar, nullptr},
        {"SkeletonBlock", &a::SkeletonBlock, nullptr},
        {"Badge", &a::Badge, nullptr},
        {"Avatar", &a::Avatar, nullptr},
        {"StatusDot", &a::StatusDot, nullptr},
        {"StatCard", &a::StatCard, nullptr},
        {"ProgressSteps", &a::ProgressSteps, nullptr},
        {"TimelineScrubber", &a::TimelineScrubber, nullptr},
        {"TooltipCard", &a::TooltipCard, nullptr},
        {"ToastStack", &a::ToastStack, nullptr},
        {"Breadcrumbs", &a::Breadcrumbs, nullptr},
        {"KeyOverlay", &a::KeyOverlay, nullptr},
        {"Divider", &a::Divider, nullptr},
        {"Kbd", &a::Kbd, nullptr},
        {"EmptyState", &a::EmptyState, nullptr},
    };
    pack::RegisterAll(in, kEntries);
}

}  // namespace imforge
