#include "emit_internal.h"
#include "emit_widgets_util.h"

namespace imforge {
namespace codegen {
namespace wdg {

namespace {

bool PlainLiteral(const FmtArgs& t) { return t.IsLiteral() && t.fmt.find('%') == std::string::npos; }

// typewriter style motions: format into a buffer and cut it like the interpreter does
std::string RevealBuffer(EmitCtx& ctx, const Node& n, const std::string& reveal, const std::string& printf_args) {
    std::string buf = Local(ctx, n, "txt");
    NeedInclude(ctx, "<cstdio>");
    Line(ctx, "char " + buf + "[1024];");
    Line(ctx, "std::snprintf(" + buf + ", sizeof(" + buf + "), " + printf_args + ");");
    Line(ctx, "ImbTextReveal(" + buf + ", " + reveal + ");");
    return buf;
}

void EmitText(EmitCtx& ctx, const Node& n) {
    FmtArgs t = ctx.Text(n, "text", "Text");
    bool bullet = Bool(ctx, n, "bullet", false);
    bool wrapped = Bool(ctx, n, "wrapped", false);
    bool disabled = Bool(ctx, n, "disabled", false);
    bool has_color = RawProp(n, "color") != nullptr;
    std::string color = has_color ? Color(ctx, n, "color", "#ffffffff") : std::string();
    std::string fa = t.CallArgs();
    std::string reveal = MotionTextReveal(ctx, n), buf;
    if (!reveal.empty()) {
        buf = RevealBuffer(ctx, n, reveal, fa);
        fa = "\"%s\", " + buf;
    }
    if (bullet || wrapped) {
        bool push = disabled || has_color;
        if (push)
            Line(ctx, "ImGui::PushStyleColor(ImGuiCol_Text, " +
                          (disabled ? std::string("ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled)") : color) +
                          ");");
        Line(ctx, std::string(bullet ? "ImGui::BulletText(" : "ImGui::TextWrapped(") + fa + ");");
        if (push) Line(ctx, "ImGui::PopStyleColor();");
    } else if (disabled) {
        Line(ctx, "ImGui::TextDisabled(" + fa + ");");
    } else if (has_color) {
        Line(ctx, "ImGui::TextColored(" + color + ", " + fa + ");");
    } else if (!buf.empty()) {
        Line(ctx, "ImGui::TextUnformatted(" + buf + ");");
    } else if (PlainLiteral(t)) {
        Line(ctx, "ImGui::TextUnformatted(" + t.fmt + ");");
    } else {
        Line(ctx, "ImGui::Text(" + fa + ");");
    }
}

void EmitSeparatorText(EmitCtx& ctx, const Node& n) {
    std::string text = Str(ctx, n, "text", "Section");
    std::string reveal = MotionTextReveal(ctx, n);
    if (!reveal.empty()) text = RevealBuffer(ctx, n, reveal, "\"%s\", " + text);
    if (Version(ctx) >= 18950) {
        Line(ctx, "ImGui::SeparatorText(" + text + ");");
        return;
    }
    Line(ctx, "#if IMGUI_VERSION_NUM >= 18950");
    Line(ctx, "ImGui::SeparatorText(" + text + ");");
    Line(ctx, "#else");
    Line(ctx, "ImGui::Separator();");
    Line(ctx, "ImGui::TextUnformatted(" + text + ");");
    Line(ctx, "#endif");
}

void EmitSpacing(EmitCtx& ctx, const Node& n) {
    if (!PropIsExpr(n, "count") && PropInt(n, "count", 1) <= 1) {
        Line(ctx, "ImGui::Spacing();");
        return;
    }
    std::string count = IntP(ctx, n, "count", 1);
    Line(ctx, "for (int k = 0; k < " + count + "; ++k) ImGui::Spacing();");
}

void EmitButtonRepeat(EmitCtx& ctx, bool push) {
    const char* modern = push ? "ImGui::PushItemFlag(ImGuiItemFlags_ButtonRepeat, true);" : "ImGui::PopItemFlag();";
    const char* legacy = push ? "ImGui::PushButtonRepeat(true);" : "ImGui::PopButtonRepeat();";
    if (Version(ctx) >= 19100) {
        Line(ctx, modern);
        return;
    }
    Line(ctx, "#if IMGUI_VERSION_NUM >= 19100");
    Line(ctx, modern);
    Line(ctx, "#else");
    Line(ctx, legacy);
    Line(ctx, "#endif");
}

void EmitButton(EmitCtx& ctx, const Node& n) {
    bool repeat = Bool(ctx, n, "repeat", false);
    if (repeat) EmitButtonRepeat(ctx, true);
    Args a;
    a.Add(Label(ctx, n));
    std::string size = Size(ctx, n);
    if (!size.empty()) a.Add(size);
    EmitCall(ctx, n, Call("ImGui::Button", a), "onClick");
    if (repeat) EmitButtonRepeat(ctx, false);
}

void EmitArrowButton(EmitCtx& ctx, const Node& n) {
    Args a;
    a.Add(IdLit(ctx, n));
    a.Add(EnumName(ctx, n, "dir", "ImGuiDir_", "Right"));
    EmitCall(ctx, n, Call("ImGui::ArrowButton", a), "onClick");
}

void EmitColorButton(EmitCtx& ctx, const Node& n) {
    Args a;
    a.Add(Label(ctx, n, "desc"));
    a.Add(Color(ctx, n, "color", "#ffffffff"));
    std::string flags = Flags(ctx, n, "flags", "ColorEditFlags");
    a.Add(flags, NoFlags(flags));
    a.Add(SizeOr0(ctx, n), IsAutoSize(n));
    EmitCall(ctx, n, Call("ImGui::ColorButton", a), "onClick");
}

void EmitCheckbox(EmitCtx& ctx, const Node& n) {
    Slot s = GetSlot(ctx, n, "value");
    std::string ptr = s.ok ? "&" + s.cpp : "&" + FallbackStorage(ctx, n, "value", "bool", 1, "false");
    Args a;
    a.Add(Label(ctx, n));
    a.Add(ptr);
    EmitCall(ctx, n, Call("ImGui::Checkbox", a), "onChange");
}

}  // namespace

bool EmitTextWidgets(EmitCtx& ctx, const Node& n) {
    const std::string& t = n.type;
    if (t == "Text") EmitText(ctx, n);
    else if (t == "SeparatorText") EmitSeparatorText(ctx, n);
    else if (t == "Separator") Line(ctx, "ImGui::Separator();");
    else if (t == "Spacing") EmitSpacing(ctx, n);
    else if (t == "NewLine") Line(ctx, "ImGui::NewLine();");
    else if (t == "Dummy") Line(ctx, "ImGui::Dummy(" + SizeOr0(ctx, n) + ");");
    else return false;
    return true;
}

bool EmitButtonWidgets(EmitCtx& ctx, const Node& n) {
    const std::string& t = n.type;
    if (t == "Button") EmitButton(ctx, n);
    else if (t == "SmallButton") EmitCall(ctx, n, "ImGui::SmallButton(" + Label(ctx, n) + ")", "onClick");
    else if (t == "ArrowButton") EmitArrowButton(ctx, n);
    else if (t == "ColorButton") EmitColorButton(ctx, n);
    else if (t == "Checkbox") EmitCheckbox(ctx, n);
    else return false;
    return true;
}

}  // namespace wdg

bool EmitStockWidget(EmitCtx& ctx, const Node& node) {
    return wdg::EmitTextWidgets(ctx, node) || wdg::EmitButtonWidgets(ctx, node) ||
           wdg::EmitListWidgets(ctx, node) || wdg::EmitNumberWidgets(ctx, node) ||
           wdg::EmitInputWidgets(ctx, node) || wdg::EmitDisplayWidgets(ctx, node);
}

}  // namespace codegen
}  // namespace imforge
