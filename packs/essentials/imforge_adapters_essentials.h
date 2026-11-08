#pragma once

// interpreter draw functions for the essentials widgets (builder canvas + live client)

#include "imforge/imforge_pack_adapter.h"
#include "imb_essentials.h"

namespace imforge {
namespace adapters_essentials {

inline bool Toggle(DrawCtx& ctx) {
    std::string label = pack::Label(ctx);
    bool* v = pack::BindBool(ctx, "value");
    bool changed = imb::ess::Toggle(label.c_str(), v, pack::Color(ctx, "onColor"), pack::Color(ctx, "offColor"));
    return pack::Fire(ctx, "onChange", changed);
}

inline bool KeybindPicker(DrawCtx& ctx) {
    std::string label = pack::Label(ctx);
    int* key = pack::BindInt(ctx, "key");
    return pack::Fire(ctx, "onChange", imb::ess::KeybindPicker(label.c_str(), key));
}

inline bool ColorSwatchRow(DrawCtx& ctx) {
    static const ImVec4 kSeed[] = {ImVec4(0.91f, 0.30f, 0.24f, 1), ImVec4(0.18f, 0.80f, 0.44f, 1),
                                   ImVec4(0.20f, 0.60f, 0.86f, 1), ImVec4(0.95f, 0.77f, 0.06f, 1),
                                   ImVec4(0.61f, 0.35f, 0.71f, 1), ImVec4(0.90f, 0.49f, 0.13f, 1)};
    std::string label = pack::Label(ctx);
    int count = pack::IntClamped(ctx, "count", 1, 64, 4);
    int* selected = pack::OptInt(ctx, "selected");
    ImVec4* colors = pack::BindVec4Array(ctx, "colors", count, kSeed, IM_ARRAYSIZE(kSeed));
    return pack::Fire(ctx, "onChange", imb::ess::ColorSwatchRow(label.c_str(), colors, count, selected));
}

inline bool HotkeyLabel(DrawCtx& ctx) {
    std::string text = pack::Text(ctx, "text");
    imb::ess::HotkeyLabel(text.c_str(), pack::ExprInt(ctx, "key"));
    return false;
}

inline bool Spinner(DrawCtx& ctx) {
    std::string id = pack::Label(ctx);
    imb::ess::Spinner(id.c_str(), pack::Px(ctx, "radius", 8), pack::Px(ctx, "thickness", 2), pack::Color(ctx, "color"));
    return false;
}

inline bool GradientHeader(DrawCtx& ctx) {
    std::string label = pack::Label(ctx);
    imb::ess::GradientHeader(label.c_str(), pack::Color(ctx, "c0"), pack::Color(ctx, "c1"), pack::Px(ctx, "height"),
                             pack::Bool(ctx, "centered"));
    return false;
}

inline bool AnimatedTabBar(DrawCtx& ctx) {
    std::string id = pack::Label(ctx);
    std::string labels = pack::ZeroList(ctx, "labels");
    int* index = pack::BindInt(ctx, "index");
    return pack::Fire(ctx, "onChange", imb::ess::AnimatedTabBar(id.c_str(), labels.c_str(), index));
}

inline bool AnimatedSideTabs(DrawCtx& ctx) {
    std::string id = pack::Label(ctx);
    std::string labels = pack::ZeroList(ctx, "labels");
    std::string icons = pack::IconZeroList(ctx, "icons");
    int* index = pack::BindInt(ctx, "index");
    return pack::Fire(ctx, "onChange", imb::ess::AnimatedSideTabs(id.c_str(), labels.c_str(), icons.c_str(), index));
}

inline bool GlowButton(DrawCtx& ctx) {
    std::string label = pack::Label(ctx);
    return pack::Fire(ctx, "onClick", imb::ess::GlowButton(label.c_str(), pack::Size(ctx), pack::Color(ctx, "color")));
}

inline bool SectionCard(DrawCtx& ctx) {
    pack::SeedMock(ctx, "open", Value(true));
    std::string title = pack::Label(ctx, "title");
    imb::ess::SectionCard(title.c_str(), pack::Size(ctx), pack::OptBool(ctx, "open"));
    return true;
}

inline void EndSectionCard(DrawCtx&) { imb::ess::EndSectionCard(); }

inline bool ToastHost(DrawCtx& ctx) {
    static const imb::ess::ToastCorner kCorners[] = {imb::ess::ToastCorner::TopLeft, imb::ess::ToastCorner::TopRight,
                                                      imb::ess::ToastCorner::BottomLeft, imb::ess::ToastCorner::BottomRight};
    int corner = pack::Enum(ctx, "corner", 3);
    if (corner < 0 || corner > 3) corner = 3;
    imb::ess::DrawToasts(kCorners[corner]);
    pack::DesignPlaceholder(ctx, "Toasts");
    return false;
}

}  // namespace adapters_essentials

inline void RegisterPackWidgets_essentials(Interpreter& in) {
    namespace a = adapters_essentials;
    static const pack::Entry kEntries[] = {
        {"Toggle", &a::Toggle, nullptr},
        {"KeybindPicker", &a::KeybindPicker, nullptr},
        {"ColorSwatchRow", &a::ColorSwatchRow, nullptr},
        {"HotkeyLabel", &a::HotkeyLabel, nullptr},
        {"Spinner", &a::Spinner, nullptr},
        {"GradientHeader", &a::GradientHeader, nullptr},
        {"AnimatedTabBar", &a::AnimatedTabBar, nullptr},
        {"AnimatedSideTabs", &a::AnimatedSideTabs, nullptr},
        {"GlowButton", &a::GlowButton, nullptr},
        {"SectionCard", &a::SectionCard, &a::EndSectionCard},
        {"ToastHost", &a::ToastHost, nullptr},
    };
    pack::RegisterAll(in, kEntries);
}

}  // namespace imforge
