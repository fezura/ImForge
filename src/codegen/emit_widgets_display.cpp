#include "emit_widgets_util.h"

namespace imforge {
namespace codegen {
namespace wdg {

namespace {

const char* const kWhite = "#ffffffff";
const char* const kClear = "#00000000";
const char* const kUv0 = R"({"x":0,"y":0})";
const char* const kUv1 = R"({"x":1,"y":1})";

void EmitProgressBar(EmitCtx& ctx, const Node& n) {
    Slot s = GetSlot(ctx, n, "value");
    std::string fraction;
    if (s.ok) fraction = s.Kind() == CKind::Float ? s.cpp : "(float)" + s.cpp;
    else fraction = FloatP(ctx, n, "fraction", 0);
    Args a;
    a.Add(fraction);
    a.Add(SizeOr0(ctx, n));
    if (RawProp(n, "overlay")) {
        FmtArgs t = ctx.Text(n, "overlay", "");
        if (t.IsLiteral()) {
            a.Add(t.fmt);
        } else {
            std::string buf = Local(ctx, n, "overlay");
            NeedInclude(ctx, "<cstdio>");
            Line(ctx, "char " + buf + "[64];");
            Line(ctx, "std::snprintf(" + buf + ", sizeof(" + buf + "), " + t.CallArgs() + ");");
            a.Add(buf);
        }
    }
    Line(ctx, Call("ImGui::ProgressBar", a) + ";");
}

void EmitImage(EmitCtx& ctx, const Node& n) {
    std::string size = SizeWithDefaults(ctx, n, Px(ctx, 64), Px(ctx, 64));
    Slot tex = GetSlot(ctx, n, "texture");
    if (!tex.ok) {
        Warn(ctx, n, "'texture' is not bound; emitting a placeholder", "texture");
        Line(ctx, "ImGui::Dummy(" + size + ");");
        return;
    }
    bool def_uv0 = IsDefault(n, "uv0", Vec2Json(0, 0));
    bool def_uv1 = IsDefault(n, "uv1", Vec2Json(1, 1));
    bool def_tint = IsDefaultStr(n, "tint", kWhite);
    bool def_border = IsDefaultStr(n, "border", kClear);
    std::string uv0 = Vec2P(ctx, n, "uv0", kUv0), uv1 = Vec2P(ctx, n, "uv1", kUv1);
    std::string tint = Color(ctx, n, "tint", kWhite);
    std::string border = Color(ctx, n, "border", kClear);
    std::string texture = TextureExpr(tex);
    if (def_tint && def_border) {
        Args a;
        a.Add(texture).Add(size).Add(uv0, def_uv0).Add(uv1, def_uv1);
        Line(ctx, Call("ImGui::Image", a) + ";");
        return;
    }
    bool guard = Version(ctx) < 19190;
    if (guard) Line(ctx, "#if IMGUI_VERSION_NUM >= 19190");
    if (!def_border) {
        Line(ctx, "ImGui::PushStyleVar(ImGuiStyleVar_ImageBorderSize, 1.0f);");
        Line(ctx, "ImGui::PushStyleColor(ImGuiCol_Border, " + border + ");");
    }
    Args bg;
    bg.Add(texture).Add(size).Add(uv0).Add(uv1).Add("ImVec4(0, 0, 0, 0)").Add(tint);
    Line(ctx, Call("ImGui::ImageWithBg", bg) + ";");
    if (!def_border) {
        Line(ctx, "ImGui::PopStyleColor();");
        Line(ctx, "ImGui::PopStyleVar();");
    }
    if (!guard) return;
    Line(ctx, "#else");
    Args old;
    old.Add(texture).Add(size).Add(uv0).Add(uv1).Add(tint).Add(border, def_border);
    Line(ctx, Call("ImGui::Image", old) + ";");
    Line(ctx, "#endif");
}

void EmitImageButton(EmitCtx& ctx, const Node& n) {
    std::string size = SizeWithDefaults(ctx, n, Px(ctx, 64), Px(ctx, 64));
    Slot tex = GetSlot(ctx, n, "texture");
    if (!tex.ok) {
        Warn(ctx, n, "'texture' is not bound; emitting a plain button", "texture");
        EmitCall(ctx, n, "ImGui::Button(" + Quote(ctx, "##" + n.id) + ", " + size + ")", "onClick");
        return;
    }
    bool def_uv0 = IsDefault(n, "uv0", Vec2Json(0, 0));
    bool def_uv1 = IsDefault(n, "uv1", Vec2Json(1, 1));
    bool def_bg = IsDefaultStr(n, "bg", kClear);
    bool def_tint = IsDefaultStr(n, "tint", kWhite);
    std::string uv0 = Vec2P(ctx, n, "uv0", kUv0), uv1 = Vec2P(ctx, n, "uv1", kUv1);
    std::string bg = Color(ctx, n, "bg", kClear);
    std::string tint = Color(ctx, n, "tint", kWhite);
    std::string texture = TextureExpr(tex);
    Args a;
    a.Add(IdLit(ctx, n)).Add(texture).Add(size).Add(uv0, def_uv0).Add(uv1, def_uv1).Add(bg, def_bg).Add(tint, def_tint);
    if (Version(ctx) >= 18900) {
        EmitCall(ctx, n, Call("ImGui::ImageButton", a), "onClick");
        return;
    }
    Args old;
    old.Add(texture).Add(size).Add(uv0, def_uv0).Add(uv1, def_uv1).Add("-1", true).Add(bg, def_bg).Add(tint, def_tint);
    bool has_event = HasEvent(ctx, n, "onClick");
    std::string var = Local(ctx, n, "clicked");
    std::string prefix = has_event ? "const bool " + var + " = " : std::string();
    Line(ctx, "#if IMGUI_VERSION_NUM >= 18900");
    Line(ctx, prefix + Call("ImGui::ImageButton", a) + ";");
    Line(ctx, "#else");
    Line(ctx, prefix + Call("ImGui::ImageButton", old) + ";");
    Line(ctx, "#endif");
    if (has_event) {
        Line(ctx, "if (" + var + ") {");
        Indent(ctx);
        Fire(ctx, n, "onClick");
        Unindent(ctx);
        Line(ctx, "}");
    }
}

void EmitSelectable(EmitCtx& ctx, const Node& n) {
    Slot sel = GetSlot(ctx, n, "selected");
    Args a;
    a.Add(Label(ctx, n));
    a.Add(sel.ok ? "&" + sel.cpp : std::string("false"), !sel.ok);
    std::string flags = Flags(ctx, n, "flags", "SelectableFlags");
    a.Add(flags, NoFlags(flags));
    a.Add(SizeOr0(ctx, n), IsAutoSize(n));
    EmitCall(ctx, n, Call("ImGui::Selectable", a), "onClick");
}

void EmitMenuItem(EmitCtx& ctx, const Node& n) {
    Slot sel = GetSlot(ctx, n, "selected");
    bool has_shortcut = !IsDefaultStr(n, "shortcut", "");
    Args a;
    a.Add(Label(ctx, n));
    a.Add(has_shortcut ? Str(ctx, n, "shortcut", "") : std::string("nullptr"), !has_shortcut);
    a.Add(sel.ok ? "&" + sel.cpp : std::string("false"), !sel.ok);
    a.Add(BoolExpr(ctx, n, "enabled", true), IsDefault(n, "enabled", Json(true)));
    EmitCall(ctx, n, Call("ImGui::MenuItem", a), "onClick");
}

void EmitPlot(EmitCtx& ctx, const Node& n, bool histogram) {
    Slot v = GetSlot(ctx, n, "values");
    std::string values, count;
    if (v.ok && v.IsVector()) {
        values = v.cpp + ".data()";
        count = CountExpr(v);
    } else if (v.ok && v.IsFloatArray()) {
        values = v.cpp;
        count = CountExpr(v);
    } else {
        if (v.ok) Warn(ctx, n, "'values' must be float[N] or std::vector<float>", "values");
        values = FallbackStorage(ctx, n, "values", "float", 8, "{0}");
        count = "IM_ARRAYSIZE(" + values + ")";
    }
    bool has_overlay = !IsDefaultStr(n, "overlay", "");
    bool min_zero = !PropIsExpr(n, "scaleMin") && IsDefaultNum(n, "scaleMin", 0);
    bool max_zero = !PropIsExpr(n, "scaleMax") && IsDefaultNum(n, "scaleMax", 0);
    bool auto_scale = min_zero && max_zero;
    Args a;
    a.Add(Label(ctx, n));
    a.Add(values);
    a.Add(count);
    a.Add("0", true);
    a.Add(has_overlay ? Str(ctx, n, "overlay", "") : std::string("nullptr"), !has_overlay);
    a.Add(auto_scale ? std::string("FLT_MAX") : FloatP(ctx, n, "scaleMin", 0), auto_scale);
    a.Add(auto_scale ? std::string("FLT_MAX") : FloatP(ctx, n, "scaleMax", 0), auto_scale);
    a.Add(SizeOr0(ctx, n), IsAutoSize(n));
    Line(ctx, Call(histogram ? "ImGui::PlotHistogram" : "ImGui::PlotLines", a) + ";");
}

}  // namespace

bool EmitDisplayWidgets(EmitCtx& ctx, const Node& n) {
    const std::string& t = n.type;
    if (t == "ProgressBar") EmitProgressBar(ctx, n);
    else if (t == "Image") EmitImage(ctx, n);
    else if (t == "ImageButton") EmitImageButton(ctx, n);
    else if (t == "Selectable") EmitSelectable(ctx, n);
    else if (t == "MenuItem") EmitMenuItem(ctx, n);
    else if (t == "PlotLines") EmitPlot(ctx, n, false);
    else if (t == "PlotHistogram") EmitPlot(ctx, n, true);
    else return false;
    return true;
}

}  // namespace wdg
}  // namespace codegen
}  // namespace imforge
