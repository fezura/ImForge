#include "emit_widgets_util.h"

namespace imforge {
namespace codegen {
namespace wdg {

namespace {

bool ContiguousEnum(const EnumInfo& e) {
    for (size_t i = 0; i < e.values.size(); ++i)
        if (e.values[i].second != static_cast<long long>(i)) return false;
    return true;
}

// Index model shared by Combo and ListBox: `lvalue` is the int the widget edits, `assign` writes
// the result back into an enum binding, `items`/`count` name the static item array.
struct IndexModel {
    std::string lvalue, assign, items, count;
    std::vector<std::string> pre;
    void Prologue(EmitCtx& ctx) const {
        for (const std::string& l : pre) Line(ctx, l);
    }
};

IndexModel BuildIndex(EmitCtx& ctx, const Node& n) {
    IndexModel m;
    Slot s = GetSlot(ctx, n, "value");
    std::vector<std::string> items = ctx.StringList(n, "items");
    bool enum_names = s.IsEnum() && items.empty() && s.type.enm;
    if (enum_names)
        for (const auto& kv : s.type.enm->values) items.push_back(kv.first);
    if (items.empty()) {
        Warn(ctx, n, "'items' is empty", "items");
        items.push_back("");
    }
    m.items = Local(ctx, n, "items");
    m.count = "IM_ARRAYSIZE(" + m.items + ")";
    std::vector<std::string> quoted;
    for (const std::string& it : items) quoted.push_back(Quote(ctx, it));
    if (!enum_names && !quoted.empty()) quoted[0] = Marked(ctx, n, "items", quoted[0]);
    Line(ctx, "static const char* " + m.items + "[] = {" + Join(quoted) + "};");

    if (!s.ok) {
        m.lvalue = FallbackStorage(ctx, n, "value", "int", 1, "0");
        return m;
    }
    if (!s.IsEnum()) {
        m.lvalue = s.cpp;
        return m;
    }
    std::string idx = Local(ctx, n, "idx");
    m.lvalue = idx;
    if (enum_names && !ContiguousEnum(*s.type.enm)) {
        std::string vals = Local(ctx, n, "values");
        std::vector<std::string> nums;
        for (const auto& kv : s.type.enm->values) nums.push_back(IntLit(ctx, kv.second));
        Line(ctx, "static const int " + vals + "[] = {" + Join(nums) + "};");
        m.pre.push_back("int " + idx + " = 0;");
        m.pre.push_back("for (int k = 0; k < " + m.count + "; ++k)");
        m.pre.push_back("    if (" + vals + "[k] == (int)" + s.cpp + ") " + idx + " = k;");
        m.assign = s.cpp + " = " + EnumCast(s) + vals + "[" + idx + "];";
    } else {
        m.pre.push_back("int " + idx + " = (int)" + s.cpp + ";");
        m.assign = s.cpp + " = " + EnumCast(s) + idx + ";";
    }
    return m;
}

void EmitCombo(EmitCtx& ctx, const Node& n) {
    IndexModel m = BuildIndex(ctx, n);
    std::string label = Label(ctx, n);
    std::string flags = Flags(ctx, n, "flags", "ComboFlags");
    m.Prologue(ctx);
    if (NoFlags(flags)) {
        Args a;
        a.Add(label);
        a.Add("&" + m.lvalue);
        a.Add(m.items);
        a.Add(m.count);
        EmitCallAssign(ctx, n, Call("ImGui::Combo", a), m.assign, "onChange");
        return;
    }
    std::string preview = "(" + m.lvalue + " >= 0 && " + m.lvalue + " < " + m.count + ") ? " +
                          m.items + "[" + m.lvalue + "] : \"\"";
    Line(ctx, "if (ImGui::BeginCombo(" + label + ", " + preview + ", " + flags + ")) {");
    Indent(ctx);
    Line(ctx, "for (int k = 0; k < " + m.count + "; ++k) {");
    Indent(ctx);
    Line(ctx, "if (ImGui::Selectable(" + m.items + "[k], k == " + m.lvalue + ")) {");
    Indent(ctx);
    Line(ctx, m.lvalue + " = k;");
    if (!m.assign.empty()) Line(ctx, m.assign);
    if (HasEvent(ctx, n, "onChange")) Fire(ctx, n, "onChange");
    Unindent(ctx);
    Line(ctx, "}");
    Line(ctx, "if (k == " + m.lvalue + ") ImGui::SetItemDefaultFocus();");
    Unindent(ctx);
    Line(ctx, "}");
    Line(ctx, "ImGui::EndCombo();");
    Unindent(ctx);
    Line(ctx, "}");
}

void EmitListBox(EmitCtx& ctx, const Node& n) {
    IndexModel m = BuildIndex(ctx, n);
    std::string label = Label(ctx, n);
    m.Prologue(ctx);
    Args a;
    a.Add(label);
    a.Add("&" + m.lvalue);
    a.Add(m.items);
    a.Add(m.count);
    bool def_h = !PropIsExpr(n, "heightItems") && PropInt(n, "heightItems", -1) < 0;
    if (!def_h) a.Add(IntP(ctx, n, "heightItems", -1));
    EmitCallAssign(ctx, n, Call("ImGui::ListBox", a), m.assign, "onChange");
}

void EmitRadioButton(EmitCtx& ctx, const Node& n) {
    Slot s = GetSlot(ctx, n, "value");
    std::string label = Label(ctx, n);
    std::string button_value = IntP(ctx, n, "value", 0);
    if (!s.ok) {
        std::string v = FallbackStorage(ctx, n, "value", "int", 1, "0");
        EmitCall(ctx, n, "ImGui::RadioButton(" + label + ", &" + v + ", " + button_value + ")", "onChange");
        return;
    }
    if (!s.IsEnum()) {
        EmitCall(ctx, n, "ImGui::RadioButton(" + label + ", &" + s.cpp + ", " + button_value + ")", "onChange");
        return;
    }
    std::string tmp = Local(ctx, n, "radio");
    Line(ctx, "int " + tmp + " = (int)" + s.cpp + ";");
    EmitCallAssign(ctx, n, "ImGui::RadioButton(" + label + ", &" + tmp + ", " + button_value + ")",
                   s.cpp + " = " + EnumCast(s) + tmp + ";", "onChange");
}

}  // namespace

bool EmitListWidgets(EmitCtx& ctx, const Node& n) {
    const std::string& t = n.type;
    if (t == "RadioButton") EmitRadioButton(ctx, n);
    else if (t == "Combo") EmitCombo(ctx, n);
    else if (t == "ListBox") EmitListBox(ctx, n);
    else return false;
    return true;
}

}  // namespace wdg
}  // namespace codegen
}  // namespace imforge
