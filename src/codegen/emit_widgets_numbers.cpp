#include "emit_widgets_util.h"

namespace imforge {
namespace codegen {
namespace wdg {

namespace {

int Components(const Node& n) {
    int c = PropInt(n, "components", 1);
    return c < 1 ? 1 : (c > 4 ? 4 : c);
}

std::string FnN(const char* base, int comps) {
    return comps > 1 ? std::string(base) + std::to_string(comps) : std::string(base);
}

std::string ValuePtr(EmitCtx& ctx, const Node& n, bool is_float, int comps) {
    Slot s = GetSlot(ctx, n, "value");
    if (s.ok) return ScalarPtr(s);
    std::string name = FallbackStorage(ctx, n, "value", is_float ? "float" : "int", comps,
                                       comps > 1 ? "{0}" : (is_float ? "0.0f" : "0"));
    return comps > 1 ? name : "&" + name;
}

void AddFormat(EmitCtx& ctx, const Node& n, Args& a, const char* def) {
    a.Add(Str(ctx, n, "format", def), IsDefaultStr(n, "format", def));
}

void AddFlags(EmitCtx& ctx, const Node& n, Args& a, const char* enum_name) {
    std::string flags = Flags(ctx, n, "flags", enum_name);
    a.Add(flags, NoFlags(flags));
}

void EmitSlider(EmitCtx& ctx, const Node& n, bool is_float) {
    int comps = Components(n);
    Args a;
    a.Add(Label(ctx, n));
    a.Add(ValuePtr(ctx, n, is_float, comps));
    a.Add(Num(ctx, n, "min", 0, is_float));
    a.Add(Num(ctx, n, "max", is_float ? 1 : 100, is_float));
    AddFormat(ctx, n, a, is_float ? "%.3f" : "%d");
    AddFlags(ctx, n, a, "SliderFlags");
    std::string fn = FnN(is_float ? "ImGui::SliderFloat" : "ImGui::SliderInt", comps);
    EmitCall(ctx, n, Call(fn.c_str(), a), "onChange");
}

void EmitVSlider(EmitCtx& ctx, const Node& n, bool is_float) {
    std::string size = SizeWithDefaults(ctx, n, "ImGui::GetFrameHeight()", Px(ctx, 100));
    Args a;
    a.Add(Label(ctx, n));
    a.Add(size);
    a.Add(ValuePtr(ctx, n, is_float, 1));
    a.Add(Num(ctx, n, "min", 0, is_float));
    a.Add(Num(ctx, n, "max", is_float ? 1 : 100, is_float));
    AddFormat(ctx, n, a, is_float ? "%.3f" : "%d");
    AddFlags(ctx, n, a, "SliderFlags");
    EmitCall(ctx, n, Call(is_float ? "ImGui::VSliderFloat" : "ImGui::VSliderInt", a), "onChange");
}

void EmitDrag(EmitCtx& ctx, const Node& n, bool is_float) {
    int comps = Components(n);
    Args a;
    a.Add(Label(ctx, n));
    a.Add(ValuePtr(ctx, n, is_float, comps));
    a.Add(FloatP(ctx, n, "speed", 1), IsDefaultNum(n, "speed", 1));
    a.Add(Num(ctx, n, "min", 0, is_float), IsDefaultNum(n, "min", 0));
    a.Add(Num(ctx, n, "max", 0, is_float), IsDefaultNum(n, "max", 0));
    AddFormat(ctx, n, a, is_float ? "%.3f" : "%d");
    AddFlags(ctx, n, a, "SliderFlags");
    std::string fn = FnN(is_float ? "ImGui::DragFloat" : "ImGui::DragInt", comps);
    EmitCall(ctx, n, Call(fn.c_str(), a), "onChange");
}

void EmitInputInt(EmitCtx& ctx, const Node& n) {
    int comps = Components(n);
    Args a;
    a.Add(Label(ctx, n));
    a.Add(ValuePtr(ctx, n, false, comps));
    if (comps == 1) {
        a.Add(IntP(ctx, n, "step", 1), IsDefaultNum(n, "step", 1));
        a.Add(IntP(ctx, n, "stepFast", 100), IsDefaultNum(n, "stepFast", 100));
    }
    AddFlags(ctx, n, a, "InputTextFlags");
    std::string fn = FnN("ImGui::InputInt", comps);
    EmitCall(ctx, n, Call(fn.c_str(), a), "onChange");
}

void EmitInputFloat(EmitCtx& ctx, const Node& n) {
    int comps = Components(n);
    Args a;
    a.Add(Label(ctx, n));
    a.Add(ValuePtr(ctx, n, true, comps));
    if (comps == 1) {
        a.Add(FloatP(ctx, n, "step", 0), IsDefaultNum(n, "step", 0));
        a.Add(FloatP(ctx, n, "stepFast", 0), IsDefaultNum(n, "stepFast", 0));
    }
    AddFormat(ctx, n, a, "%.3f");
    AddFlags(ctx, n, a, "InputTextFlags");
    std::string fn = FnN("ImGui::InputFloat", comps);
    EmitCall(ctx, n, Call(fn.c_str(), a), "onChange");
}

void EmitColor(EmitCtx& ctx, const Node& n, bool picker) {
    Slot s = GetSlot(ctx, n, "value");
    bool alpha = Bool(ctx, n, "alpha", true);
    int comps = 4;
    std::string ptr;
    if (!s.ok) {
        ptr = FallbackStorage(ctx, n, "value", "float", 4, "{0, 0, 0, 1}");
    } else if (s.IsFloatArray() && s.Count() == 3) {
        comps = 3;
        ptr = s.cpp;
    } else if (s.IsFloatArray() || s.IsVec() || s.IsVector()) {
        ptr = ScalarPtr(s);
    } else {
        Info(ctx, n, "'value' type is not a known color type; assuming a struct with .x", "value");
        ptr = "&" + s.cpp + ".x";
    }
    std::string flags = Flags(ctx, n, "flags", "ColorEditFlags");
    if (!alpha && comps == 4) flags = OrFlags(flags, "ImGuiColorEditFlags_NoAlpha");
    Args a;
    a.Add(Label(ctx, n));
    a.Add(ptr);
    a.Add(flags, NoFlags(flags));
    std::string fn = std::string(picker ? "ImGui::ColorPicker" : "ImGui::ColorEdit") + (comps == 3 ? "3" : "4");
    EmitCall(ctx, n, Call(fn.c_str(), a), "onChange");
}

}  // namespace

bool EmitNumberWidgets(EmitCtx& ctx, const Node& n) {
    const std::string& t = n.type;
    if (t == "SliderFloat") EmitSlider(ctx, n, true);
    else if (t == "SliderInt") EmitSlider(ctx, n, false);
    else if (t == "VSliderFloat") EmitVSlider(ctx, n, true);
    else if (t == "VSliderInt") EmitVSlider(ctx, n, false);
    else if (t == "DragFloat") EmitDrag(ctx, n, true);
    else if (t == "DragInt") EmitDrag(ctx, n, false);
    else if (t == "InputInt") EmitInputInt(ctx, n);
    else if (t == "InputFloat") EmitInputFloat(ctx, n);
    else if (t == "ColorEdit") EmitColor(ctx, n, false);
    else if (t == "ColorPicker") EmitColor(ctx, n, true);
    else return false;
    return true;
}

}  // namespace wdg
}  // namespace codegen
}  // namespace imforge
