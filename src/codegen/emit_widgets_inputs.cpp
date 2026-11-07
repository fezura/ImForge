#include "emit_widgets_util.h"

namespace imforge {
namespace codegen {
namespace wdg {

namespace {

struct TextBuf {
    std::string arg;        // buffer argument (`&str`, `cfg.name`, `buf_<id>`)
    std::string size_arg;   // empty for the std::string overload
    std::string copy_back;  // assignment after an edit (non-stdlib std::string bindings)
};

std::string StaticBuffer(EmitCtx& ctx, const Node& n) {
    std::string name = Local(ctx, n, "buf");
    int max_len = PropInt(n, "maxLength", 256);
    if (max_len < 1) max_len = 1;
    Line(ctx, "static char " + name + "[" + Marked(ctx, n, "maxLength", IntLit(ctx, max_len)) + "] = \"\";");
    return name;
}

TextBuf ResolveBuffer(EmitCtx& ctx, const Node& n, const std::string& label) {
    TextBuf b;
    Slot s = GetSlot(ctx, n, "value");
    if (s.ok && s.Kind() == CKind::StdString) {
        if (ctx.opts.settings.std_string) {
            NeedStdlib(ctx);
            b.arg = "&" + s.cpp;
            return b;
        }
        b.arg = StaticBuffer(ctx, n);
        NeedInclude(ctx, "<cstdio>");
        NeedInclude(ctx, "\"imgui_internal.h\"");
        Line(ctx, "if (ImGui::GetActiveID() != ImGui::GetID(" + Writer::StripMarks(label) + ")) std::snprintf(" + b.arg + ", sizeof(" + b.arg + "), \"%s\", " + s.cpp + ".c_str());");
        b.copy_back = s.cpp + " = " + b.arg + ";";
    } else if (s.ok && (s.Kind() == CKind::CharArray || s.Kind() == CKind::Array)) {
        b.arg = s.cpp;
        if (s.Count() <= 0 && s.count.empty()) {
            b.size_arg = Marked(ctx, n, "maxLength", IntLit(ctx, PropInt(n, "maxLength", 256)));
            return b;
        }
    } else {
        if (s.ok) Warn(ctx, n, "'value' must be std::string or char[N]; using a static buffer", "value");
        else Warn(ctx, n, "'value' is not bound; using a static buffer", "value");
        b.arg = StaticBuffer(ctx, n);
    }
    b.size_arg = "IM_ARRAYSIZE(" + b.arg + ")";
    return b;
}

void EmitInputText(EmitCtx& ctx, const Node& n) {
    std::string label = Label(ctx, n);
    bool has_hint = !IsDefaultStr(n, "hint", "");
    std::string hint = has_hint ? Str(ctx, n, "hint", "") : std::string();
    TextBuf b = ResolveBuffer(ctx, n, label);
    bool enter = HasEvent(ctx, n, "onEnter");
    std::string flags = Flags(ctx, n, "flags", "InputTextFlags");
    if (enter) flags = OrFlags(flags, "ImGuiInputTextFlags_EnterReturnsTrue");
    Args a;
    a.Add(label);
    if (has_hint) a.Add(hint);
    a.Add(b.arg);
    if (!b.size_arg.empty()) a.Add(b.size_arg);
    a.Add(flags, NoFlags(flags));
    std::string call = Call(has_hint ? "ImGui::InputTextWithHint" : "ImGui::InputText", a);
    if (!enter) {
        EmitCallAssign(ctx, n, call, b.copy_back, "onChange");
        return;
    }
    EmitCallAssign(ctx, n, call, b.copy_back, "onEnter");
    bool change = HasEvent(ctx, n, "onChange");
    if (change || !b.copy_back.empty()) {
        Line(ctx, "if (ImGui::IsItemEdited()) {");
        Indent(ctx);
        if (!b.copy_back.empty()) Line(ctx, b.copy_back);
        if (change) Fire(ctx, n, "onChange");
        Unindent(ctx);
        Line(ctx, "}");
    }
}

void EmitInputTextMultiline(EmitCtx& ctx, const Node& n) {
    std::string label = Label(ctx, n);
    TextBuf b = ResolveBuffer(ctx, n, label);
    std::string flags = Flags(ctx, n, "flags", "InputTextFlags");
    Args a;
    a.Add(label);
    a.Add(b.arg);
    if (!b.size_arg.empty()) a.Add(b.size_arg);
    a.Add(SizeOr0(ctx, n), IsAutoSize(n));
    a.Add(flags, NoFlags(flags));
    EmitCallAssign(ctx, n, Call("ImGui::InputTextMultiline", a), b.copy_back, "onChange");
}

}  // namespace

bool EmitInputWidgets(EmitCtx& ctx, const Node& n) {
    const std::string& t = n.type;
    if (t == "InputText") EmitInputText(ctx, n);
    else if (t == "InputTextMultiline") EmitInputTextMultiline(ctx, n);
    else return false;
    return true;
}

}  // namespace wdg
}  // namespace codegen
}  // namespace imforge
