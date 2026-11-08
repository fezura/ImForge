#pragma once

// interpreter draw functions for the controls widgets (builder canvas + live client)

#include "imforge/imforge_pack_adapter.h"
#include "imb_controls.h"

namespace imforge {
namespace adapters_controls {

// stringList prop as const char* array; keep the object alive while the pointers are used
struct Strings {
    std::vector<std::string> owned;
    std::vector<const char*> ptrs;
    const char* const* data() const { return ptrs.empty() ? nullptr : ptrs.data(); }
    int size() const { return static_cast<int>(ptrs.size()); }
};

inline void List(const DrawCtx& ctx, const char* prop, Strings& out, bool icons = false) {
    out.owned = pack::List(ctx, prop);
    if (icons)
        for (std::string& s : out.owned) s = pack::Icon(ctx, s);
    out.ptrs.clear();
    for (const std::string& s : out.owned) out.ptrs.push_back(s.c_str());
}

inline int* BindInts(const DrawCtx& ctx, const char* slot, int n, const int* init) {
    if (ctx.HasBinding(slot) || ctx.Design())
        if (int* v = interp_widgets::IntStorage(ctx, slot, n)) return v;
    int zero[4] = {0, 0, 0, 0};
    std::string& bytes = pack::LocalBytes(ctx, slot, sizeof(int) * static_cast<size_t>(n), init ? init : zero);
    return reinterpret_cast<int*>(&bytes[0]);
}

inline bool SegmentedControl(DrawCtx& ctx) {
    std::string label = pack::Label(ctx);
    Strings items;
    List(ctx, "items", items);
    int* index = pack::BindInt(ctx, "index");
    return pack::Fire(ctx, "onChange", imb::ctl::SegmentedControl(label.c_str(), index, items.data(), items.size(),
                                                                  pack::Px(ctx, "width")));
}

inline bool PillTabs(DrawCtx& ctx) {
    std::string label = pack::Label(ctx);
    Strings items;
    List(ctx, "items", items);
    int* index = pack::BindInt(ctx, "index");
    return pack::Fire(ctx, "onChange", imb::ctl::PillTabs(label.c_str(), index, items.data(), items.size()));
}

inline bool ToggleButtonGroup(DrawCtx& ctx) {
    std::string label = pack::Label(ctx);
    Strings items;
    List(ctx, "items", items);
    int* mask = pack::BindInt(ctx, "mask");
    return pack::Fire(ctx, "onChange", imb::ctl::ToggleButtonGroup(label.c_str(), mask, items.data(), items.size()));
}

inline bool Chips(DrawCtx& ctx) {
    std::string label = pack::Label(ctx);
    Strings items;
    List(ctx, "items", items);
    int* mask = pack::BindInt(ctx, "mask");
    return pack::Fire(ctx, "onChange", imb::ctl::Chips(label.c_str(), mask, items.data(), items.size()));
}

inline bool RangeSlider(DrawCtx& ctx) {
    std::string label = pack::Label(ctx);
    std::string fmt = pack::Text(ctx, "format");
    float lo = pack::Float(ctx, "min"), hi = pack::Float(ctx, "max", 100);
    float init[2] = {lo + (hi - lo) * 0.25f, lo + (hi - lo) * 0.75f};
    float* v = pack::BindFloats(ctx, "value", 2, init);
    if (ctx.Design() && v[0] == 0.0f && v[1] == 0.0f && hi > lo) {
        v[0] = init[0];
        v[1] = init[1];
    }
    return pack::Fire(ctx, "onChange", imb::ctl::RangeSlider(label.c_str(), v, lo, hi, fmt.c_str()));
}

inline bool RangeSliderInt(DrawCtx& ctx) {
    std::string label = pack::Label(ctx);
    std::string fmt = pack::Text(ctx, "format");
    int lo = pack::Int(ctx, "min"), hi = pack::Int(ctx, "max", 100);
    int init[2] = {lo + (hi - lo) / 4, lo + (hi - lo) * 3 / 4};
    int* v = BindInts(ctx, "value", 2, init);
    if (!v) return false;
    if (ctx.Design() && v[0] == 0 && v[1] == 0 && hi > lo) {
        v[0] = init[0];
        v[1] = init[1];
    }
    return pack::Fire(ctx, "onChange", imb::ctl::RangeSlider(label.c_str(), v, lo, hi, fmt.c_str()));
}

inline bool Knob(DrawCtx& ctx) {
    std::string label = pack::Label(ctx);
    std::string fmt = pack::Text(ctx, "format");
    float* v = pack::BindFloat(ctx, "value");
    return pack::Fire(ctx, "onChange",
                      imb::ctl::Knob(label.c_str(), v, pack::Float(ctx, "min"), pack::Float(ctx, "max", 1), fmt.c_str()));
}

inline bool TickSlider(DrawCtx& ctx) {
    std::string label = pack::Label(ctx);
    std::string fmt = pack::Text(ctx, "format");
    float* v = pack::BindFloat(ctx, "value");
    return pack::Fire(ctx, "onChange", imb::ctl::TickSlider(label.c_str(), v, pack::Float(ctx, "min"), pack::Float(ctx, "max", 10),
                                                            pack::IntClamped(ctx, "ticks", 2, 101, 11), fmt.c_str()));
}

inline bool Stepper(DrawCtx& ctx) {
    std::string label = pack::Label(ctx);
    int* v = pack::BindInt(ctx, "value");
    return pack::Fire(ctx, "onChange", imb::ctl::Stepper(label.c_str(), v, pack::Int(ctx, "min"), pack::Int(ctx, "max", 10),
                                                         pack::Int(ctx, "step", 1)));
}

inline bool SwitchToggle(DrawCtx& ctx) {
    static const imb::ctl::SwitchKind kKinds[] = {imb::ctl::SwitchKind::Ios, imb::ctl::SwitchKind::Square,
                                                  imb::ctl::SwitchKind::Icons};
    std::string label = pack::Label(ctx);
    bool* v = pack::BindBool(ctx, "value");
    int kind = pack::Enum(ctx, "kind");
    if (kind < 0 || kind > 2) kind = 0;
    return pack::Fire(ctx, "onChange", imb::ctl::SwitchToggle(label.c_str(), v, kKinds[kind]));
}

inline bool IconToggle(DrawCtx& ctx) {
    std::string label = pack::Label(ctx);
    std::string on = pack::Icon(ctx, pack::Text(ctx, "iconOn")), off = pack::Icon(ctx, pack::Text(ctx, "iconOff"));
    bool* v = pack::BindBool(ctx, "value");
    return pack::Fire(ctx, "onChange", imb::ctl::IconToggle(label.c_str(), v, on.c_str(), off.c_str()));
}

inline bool TriCheckbox(DrawCtx& ctx) {
    std::string label = pack::Label(ctx);
    int* state = pack::BindInt(ctx, "state");
    return pack::Fire(ctx, "onChange", imb::ctl::TriCheckbox(label.c_str(), state));
}

inline bool RatingStars(DrawCtx& ctx) {
    std::string label = pack::Label(ctx);
    float* v = pack::BindFloat(ctx, "value");
    return pack::Fire(ctx, "onChange", imb::ctl::RatingStars(label.c_str(), v, pack::IntClamped(ctx, "max", 1, 20, 5)));
}

inline bool SearchField(DrawCtx& ctx) {
    std::string label = pack::Label(ctx);
    std::string hint = pack::Text(ctx, "hint");
    std::string* text = pack::BindString(ctx, "text");
    return pack::Fire(ctx, "onChange", imb::ctl::SearchField(label.c_str(), text, hint.c_str()));
}

inline bool TagInput(DrawCtx& ctx) {
    std::string label = pack::Label(ctx);
    std::string* tags = pack::BindString(ctx, "tags");
    return pack::Fire(ctx, "onChange", imb::ctl::TagInput(label.c_str(), tags));
}

inline bool KeyChordPicker(DrawCtx& ctx) {
    std::string label = pack::Label(ctx);
    int* chord = pack::BindInt(ctx, "chord");
    return pack::Fire(ctx, "onChange", imb::ctl::KeyChordPicker(label.c_str(), chord));
}

inline bool DragNumber(DrawCtx& ctx) {
    std::string label = pack::Label(ctx);
    std::string fmt = pack::Text(ctx, "format");
    Strings units;
    List(ctx, "units", units);
    float* v = pack::BindFloat(ctx, "value");
    int* unit = pack::OptInt(ctx, "unit");
    return pack::Fire(ctx, "onChange",
                      imb::ctl::DragNumber(label.c_str(), v, units.data(), units.size(), unit, pack::Float(ctx, "speed", 1),
                                           pack::Float(ctx, "min"), pack::Float(ctx, "max"), fmt.c_str()));
}

inline bool Vec2Edit(DrawCtx& ctx) {
    std::string label = pack::Label(ctx);
    std::string fmt = pack::Text(ctx, "format");
    float* v = pack::BindFloats(ctx, "value", 2);
    return pack::Fire(ctx, "onChange", imb::ctl::Vec2Edit(label.c_str(), v, pack::Float(ctx, "speed", 0.1f),
                                                          pack::Float(ctx, "min"), pack::Float(ctx, "max"), fmt.c_str()));
}

inline bool Vec3Edit(DrawCtx& ctx) {
    std::string label = pack::Label(ctx);
    std::string fmt = pack::Text(ctx, "format");
    float* v = pack::BindFloats(ctx, "value", 3);
    return pack::Fire(ctx, "onChange", imb::ctl::Vec3Edit(label.c_str(), v, pack::Float(ctx, "speed", 0.1f),
                                                          pack::Float(ctx, "min"), pack::Float(ctx, "max"), fmt.c_str()));
}

inline bool ColorPalette(DrawCtx& ctx) {
    std::string label = pack::Label(ctx);
    Strings colors;
    List(ctx, "colors", colors);
    ImVec4* color = ctx.HasBinding("color") ? reinterpret_cast<ImVec4*>(pack::BindFloats(ctx, "color", 4)) : nullptr;
    int* selected = ctx.HasBinding("selected") ? pack::BindInt(ctx, "selected")
                                               : (color ? nullptr : pack::Local<int>(ctx, "selected", 0));
    return pack::Fire(ctx, "onChange", imb::ctl::ColorPalette(label.c_str(), selected, color, colors.data(), colors.size(),
                                                              pack::Int(ctx, "columns")));
}

inline bool HueStrip(DrawCtx& ctx) {
    std::string label = pack::Label(ctx);
    float* hue = pack::BindFloat(ctx, "hue");
    return pack::Fire(ctx, "onChange", imb::ctl::HueStrip(label.c_str(), hue));
}

inline bool SatValBox(DrawCtx& ctx) {
    std::string label = pack::Label(ctx);
    float* hue = pack::BindFloat(ctx, "hue");
    float* sat = pack::BindFloat(ctx, "sat", 1.0f);
    float* val = pack::BindFloat(ctx, "val", 1.0f);
    return pack::Fire(ctx, "onChange", imb::ctl::SatValBox(label.c_str(), *hue, sat, val));
}

inline bool ColorPickerMini(DrawCtx& ctx) {
    static const float kWhite[4] = {1, 1, 1, 1};
    std::string label = pack::Label(ctx);
    float* c = pack::BindFloats(ctx, "color", 4, kWhite);
    return pack::Fire(ctx, "onChange", imb::ctl::ColorPickerMini(label.c_str(), c));
}

inline bool GradientEditor(DrawCtx& ctx) {
    static const float kSeed[10] = {0.0f, 0.10f, 0.45f, 0.95f, 1.0f, 1.0f, 0.95f, 0.30f, 0.55f, 1.0f};
    std::string label = pack::Label(ctx);
    int n = 0;
    float* data = nullptr;
    if (ctx.HasBinding("stops")) data = ctx.BindFloatArray("stops", &n);
    if (!data || n < 10) {
        float init[40];
        for (int i = 0; i < 40; ++i) init[i] = i < 10 ? kSeed[i] : (i % 5 == 0 ? -1.0f : 0.0f);
        n = 40;
        data = reinterpret_cast<float*>(&pack::LocalBytes(ctx, "stops", sizeof(init), init)[0]);
    }
    return pack::Fire(ctx, "onChange", imb::ctl::GradientEditorPacked(label.c_str(), data, n));
}

inline bool SplitButton(DrawCtx& ctx) {
    std::string label = pack::Label(ctx);
    Strings items;
    List(ctx, "items", items);
    int* selected = ctx.HasBinding("selected") ? pack::BindInt(ctx, "selected") : pack::Local<int>(ctx, "selected", 0);
    return pack::Fire(ctx, "onClick", imb::ctl::SplitButton(label.c_str(), selected, items.data(), items.size()));
}

inline bool IconDropdown(DrawCtx& ctx) {
    std::string label = pack::Label(ctx);
    Strings items, icons;
    List(ctx, "items", items);
    List(ctx, "icons", icons, true);
    int* current = pack::BindInt(ctx, "current");
    return pack::Fire(ctx, "onChange", imb::ctl::IconDropdown(label.c_str(), current, items.data(), items.size(),
                                                              icons.data(), icons.size()));
}

inline bool CommandPalette(DrawCtx& ctx) {
    std::string label = pack::Label(ctx);
    Strings items, hints;
    List(ctx, "items", items);
    List(ctx, "hints", hints);
    bool* open = pack::OptBool(ctx, "open");
    int* selected = pack::BindInt(ctx, "selected");
    bool picked = imb::ctl::CommandPalette(label.c_str(), open, items.data(), items.size(), selected, hints.data(), hints.size());
    pack::DesignPlaceholder(ctx, "Command palette (Ctrl+P)");
    return pack::Fire(ctx, "onClick", picked);
}

}  // namespace adapters_controls

inline void RegisterPackWidgets_controls(Interpreter& in) {
    namespace a = adapters_controls;
    static const pack::Entry kEntries[] = {
        {"SegmentedControl", &a::SegmentedControl, nullptr},
        {"PillTabs", &a::PillTabs, nullptr},
        {"ToggleButtonGroup", &a::ToggleButtonGroup, nullptr},
        {"Chips", &a::Chips, nullptr},
        {"RangeSlider", &a::RangeSlider, nullptr},
        {"RangeSliderInt", &a::RangeSliderInt, nullptr},
        {"Knob", &a::Knob, nullptr},
        {"TickSlider", &a::TickSlider, nullptr},
        {"Stepper", &a::Stepper, nullptr},
        {"SwitchToggle", &a::SwitchToggle, nullptr},
        {"IconToggle", &a::IconToggle, nullptr},
        {"TriCheckbox", &a::TriCheckbox, nullptr},
        {"RatingStars", &a::RatingStars, nullptr},
        {"SearchField", &a::SearchField, nullptr},
        {"TagInput", &a::TagInput, nullptr},
        {"KeyChordPicker", &a::KeyChordPicker, nullptr},
        {"DragNumber", &a::DragNumber, nullptr},
        {"Vec2Edit", &a::Vec2Edit, nullptr},
        {"Vec3Edit", &a::Vec3Edit, nullptr},
        {"ColorPalette", &a::ColorPalette, nullptr},
        {"HueStrip", &a::HueStrip, nullptr},
        {"SatValBox", &a::SatValBox, nullptr},
        {"ColorPickerMini", &a::ColorPickerMini, nullptr},
        {"GradientEditor", &a::GradientEditor, nullptr},
        {"SplitButton", &a::SplitButton, nullptr},
        {"IconDropdown", &a::IconDropdown, nullptr},
        {"CommandPalette", &a::CommandPalette, nullptr},
    };
    pack::RegisterAll(in, kEntries);
}

}  // namespace imforge
