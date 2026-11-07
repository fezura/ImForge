#include <algorithm>
#include <cstdio>
#include <string>

#include "imgui.h"

#include "imforge/imb_runtime.h"

#include "../builder_style.h"
#include "../editor_context.h"
#include "../ui/ui_helpers.h"
#include "anim_timeline_editors.h"

namespace imforge::builder::anim_timeline {

namespace {

const ImVec2 kSwatchSize(118.0f, 66.0f);
const float kSwatchInset = 16.0f;
const float kSampleRounding = 6.0f;

ImU32 ColorParam(const Json& j, ImU32 def) {
    if (!j.IsString()) return def;
    bool ok = false;
    Vec4 c = ParseColor(j.AsString(), &ok);
    return ok ? ImGui::ColorConvertFloat4ToU32(ImVec4(c.x, c.y, c.z, c.w)) : def;
}

float NumParam(const Json& p, const char* key, float def) {
    const Json& j = p[key];
    return j.IsNumber() ? (float)j.AsDouble() : def;
}

bool IsBehind(const std::string& t) {
    return t == "shadow" || t == "glow" || t == "gradient" || t == "radialGradient" || t == "noise" ||
           t == "particles" || t == "blur";
}

void DrawEffect(ImDrawList* dl, const FxSpec& fx, const ImVec2& mn, const ImVec2& mx, ImGuiID id) {
    const Json& p = fx.params;
    float r = NumParam(p, "rounding", kSampleRounding);
    const std::string& t = fx.type;
    if (t == "shadow") {
        ImVec2 off(NumParam(p["offset"], "x", 0.0f), NumParam(p["offset"], "y", 2.0f));
        imb::Shadow(dl, mn, mx, r, ColorParam(p["color"], IM_COL32(0, 0, 0, 128)), NumParam(p, "size", 12.0f), off);
    } else if (t == "glow") {
        imb::Glow(dl, mn, mx, r, ColorParam(p["color"], IM_COL32(255, 255, 255, 96)), NumParam(p, "size", 8.0f));
    } else if (t == "gradient") {
        const Json& c0 = p.Has("c0") ? p["c0"] : p["from"];
        const Json& c1 = p.Has("c1") ? p["c1"] : p["to"];
        imb::GradientRect(dl, mn, mx, ColorParam(c0, IM_COL32(255, 255, 255, 32)), ColorParam(c1, IM_COL32(0, 0, 0, 32)),
                          p["vertical"].AsBool(true), r);
    } else if (t == "radialGradient") {
        ImVec2 c((mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f);
        float rad = NumParam(p, "radius", std::max(mx.x - mn.x, mx.y - mn.y) * 0.5f);
        imb::RadialGradient(dl, c, rad, ColorParam(p["inner"], IM_COL32(255, 255, 255, 64)),
                            ColorParam(p["outer"], IM_COL32(0, 0, 0, 0)), p["segments"].AsInt(48));
    } else if (t == "noise") {
        imb::NoiseBackground(dl, mn, mx, ColorParam(p["color"], IM_COL32(255, 255, 255, 24)), NumParam(p, "density", 0.1f), id);
    } else if (t == "particles") {
        imb::ParticleStyle ps;
        ps.count = p["count"].AsInt(ps.count);
        ps.speed = NumParam(p, "speed", ps.speed);
        ps.size = NumParam(p, "size", ps.size);
        ps.color = ColorParam(p["color"], ps.color);
        ps.connect = p["connect"].AsBool(ps.connect);
        ps.connect_dist = NumParam(p, "connectDist", ps.connect_dist);
        imb::Particles(dl, id, mn, mx, ps);
    } else if (t == "border") {
        const std::string st = p["style"].AsString("Solid");
        imb::BorderStyle bs = st == "Rainbow" ? imb::BorderStyle::Rainbow
                            : st == "Sweep"   ? imb::BorderStyle::Sweep
                            : st == "Pulse"   ? imb::BorderStyle::Pulse
                                              : imb::BorderStyle::Solid;
        imb::AnimatedBorder(dl, mn, mx, r, NumParam(p, "thickness", 1.0f), bs,
                            ColorParam(p["color"], IM_COL32(255, 255, 255, 255)), NumParam(p, "speed", 1.0f));
    } else if (t == "cursorHighlight") {
        imb::CursorHighlight(dl, mn, mx, r, ColorParam(p["color"], IM_COL32(255, 255, 255, 40)), NumParam(p, "radius", 80.0f));
    }
}

void DrawSwatch(const FxSpec& fx, int index) {
    const BuilderColors& c = Colors();
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImVec2 p1(p0.x + kSwatchSize.x, p0.y + kSwatchSize.y);
    ImGui::PushID(index);
    ImGui::InvisibleButton("##swatch", kSwatchSize);
    ImGuiID id = ImGui::GetItemID();
    ImGui::PopID();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p0, p1, ToU32(c.bg_frame), 4.0f);
    dl->PushClipRect(p0, p1, true);
    ImVec2 mn(p0.x + kSwatchInset, p0.y + kSwatchInset), mx(p1.x - kSwatchInset, p1.y - kSwatchInset);
    bool behind = IsBehind(fx.type);
    if (fx.type == "blur") {
        dl->AddRectFilled(mn, mx, ToU32(c.bg_hover), kSampleRounding);
        const char* msg = "blur (needs live app\nor GPU preview)";
        ImVec2 ts = ImGui::CalcTextSize(msg);
        dl->AddText(ImVec2(p0.x + (kSwatchSize.x - ts.x) * 0.5f, p0.y + (kSwatchSize.y - ts.y) * 0.5f), ToU32(c.text_dim), msg);
    } else {
        if (behind) DrawEffect(dl, fx, mn, mx, id);
        if (fx.type != "gradient" && fx.type != "radialGradient" && fx.type != "noise" && fx.type != "particles")
            dl->AddRectFilled(mn, mx, ImGui::GetColorU32(ImGuiCol_Button), kSampleRounding);
        if (!behind) DrawEffect(dl, fx, mn, mx, id);
    }
    dl->PopClipRect();
}

}  // namespace

std::string FxSummary(const FxSpec& fx) {
    std::string s;
    if (!fx.params.IsObject()) return s;
    for (const auto& kv : fx.params.obj) {
        if (!s.empty()) s += "  ";
        s += kv.first + "=";
        if (kv.second.IsString()) s += kv.second.AsString();
        else if (kv.second.IsNumber()) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%g", kv.second.AsDouble());
            s += buf;
        } else if (kv.second.IsBool()) s += kv.second.AsBool() ? "true" : "false";
        else if (kv.second.IsObject()) {
            char buf[48];
            std::snprintf(buf, sizeof(buf), "(%g, %g)", kv.second["x"].AsDouble(), kv.second["y"].AsDouble());
            s += buf;
        }
    }
    return s;
}

void DrawFxPreview(EditorContext& ctx, Node& node) {
    ImGui::TextUnformatted("Effects");
    ImGui::SameLine();
    ui::TextDim("%d", (int)node.fx.size());
    if (node.fx.empty()) {
        ui::TextDim("No fx on this node. Use \"Rainbow border\" or \"Soft shadow\" above, or the Properties panel.");
        return;
    }
    int remove = -1;
    for (size_t i = 0; i < node.fx.size(); ++i) {
        const FxSpec& fx = node.fx[i];
        ImGui::PushID((int)i);
        DrawSwatch(fx, (int)i);
        ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::TextUnformatted(fx.type.c_str());
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + std::max(ImGui::GetContentRegionAvail().x - 8.0f, 80.0f));
        ui::TextDim("%s", FxSummary(fx).c_str());
        ImGui::PopTextWrapPos();
        if (ImGui::SmallButton("Remove")) remove = (int)i;
        ImGui::EndGroup();
        ImGui::PopID();
        ImGui::Spacing();
    }
    if (remove >= 0) ctx.Edit("Remove fx", [&] { node.fx.erase(node.fx.begin() + remove); });
}

}  // namespace imforge::builder::anim_timeline
