#ifndef IMFORGE_INTERP_PARTS
#include "imforge_interp.h"
#else
#pragma once

namespace imforge {
namespace interp_detail {

constexpr int kMaxDrawDepth = 64;

inline bool TriggerState(const DrawCtx& ctx, const Interpreter::NodeRt& rt, const std::string& on, bool visible,
                         bool enabled) {
    bool forced = false;
    if (ctx.interp.ForcedTrigger(ctx.node, on.c_str(), forced)) return forced;
    if (on == "hover") return rt.hovered;
    if (on == "active") return rt.active;
    if (on == "focus") return rt.focused;
    if (on == "open") return rt.open;
    if (on == "visible") return visible;
    if (on == "disabled") return !enabled;
    if (on == "enabled") return enabled;
    return on.empty() || on == "always";
}

struct StylePushes {
    int colors = 0, vars = 0;
};

// newer-imgui names get an Info and are skipped, anything else is a typo
inline void UnavailableStyle(const DrawCtx& ctx, const std::string& what, int since, const char* prop) {
    if (since > IMGUI_VERSION_NUM) InfoOnce(ctx, what + " needs Dear ImGui " + ImGuiVersionText(since) + "; skipped", prop);
    else ctx.Diag(Diagnostic::Level::Warning, "unknown " + what, prop);
}

inline void PushOverride(DrawCtx& ctx, const StyleOverride& so, StylePushes& p) {
    for (const auto& c : so.colors) {
        int idx = ColorIndex(c.first.c_str());
        if (idx < 0) {
            UnavailableStyle(ctx, "style color '" + c.first + "'", StyleColorSince(c.first.c_str()), "style");
            continue;
        }
        ImGui::PushStyleColor(idx, ImVec4(c.second.x, c.second.y, c.second.z, c.second.w));
        ++p.colors;
    }
    for (const auto& v : so.vars) {
        int since = 0;
        if (PushStyleVarJson(v.first.c_str(), v.second, ctx.S(), &since)) ++p.vars;
        else UnavailableStyle(ctx, "style var '" + v.first + "'", since, "style");
    }
}

inline void PushTransitions(DrawCtx& ctx, const Interpreter::NodeRt& rt, StylePushes& p, ImVec2& offset) {
    const ImGuiStyle& st = ImGui::GetStyle();
    const float S = ctx.S();
    for (const Transition& tr : ctx.node.anim.transitions) {
        bool on = TriggerState(ctx, rt, tr.on, ctx.visible, ctx.enabled);
        ImGui::PushID(tr.on.c_str());
        ImGuiID tid = ImGui::GetID(tr.prop.c_str());
        ImGui::PopID();
        bool spring = tr.stiffness > 0 || IsSpringEaseName(tr.ease.c_str());
        float t = spring ? imb::Spring(tid, on ? 1.0f : 0.0f, tr.stiffness > 0 ? tr.stiffness : 170.0f,
                                       tr.damping > 0 ? tr.damping : 26.0f)
                         : imb::Transition(tid, on, tr.duration, EaseFromName(tr.ease.c_str()));
        if (t <= 0.0f) continue;
        if (t > 1.0f && !spring) t = 1.0f;
        float to = static_cast<float>(tr.to.AsDouble());
        if (tr.prop.compare(0, 6, "color.") == 0) {
            int idx = ColorIndex(tr.prop.c_str() + 6);
            if (idx < 0) {
                UnavailableStyle(ctx, "transition color '" + tr.prop + "'", StyleColorSince(tr.prop.c_str() + 6), "anim");
                continue;
            }
            ImVec4 base = ImGui::GetStyleColorVec4(idx);
            ImVec4 target = base;
            if (tr.to.IsString()) {
                bool ok = false;
                Vec4 c = ParseColor(tr.to.AsString(), &ok);
                if (ok) target = ImVec4(c.x, c.y, c.z, c.w);
            }
            ImGui::PushStyleColor(idx, imb::Lerp(base, target, t));
            ++p.colors;
        } else if (tr.prop == "alpha") {
            float a = imb::Lerp(st.Alpha, to, t);
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, a < 0.0f ? 0.0f : a > 1.0f ? 1.0f : a);
            ++p.vars;
        } else if (tr.prop == "rounding") {
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, imb::Lerp(st.FrameRounding, to * S, t));
            ++p.vars;
        } else if (tr.prop == "border") {
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, imb::Lerp(st.FrameBorderSize, to * S, t));
            ++p.vars;
        } else if (tr.prop == "offset.x") {
            offset.x += to * S * t;
        } else if (tr.prop == "offset.y") {
            offset.y += to * S * t;
        } else if (tr.prop == "size.w") {
            ctx.size_scale_w *= imb::Lerp(1.0f, to, t);
        } else if (tr.prop == "size.h") {
            ctx.size_scale_h *= imb::Lerp(1.0f, to, t);
        } else if (tr.prop == "scale") {
            float k = imb::Lerp(1.0f, to, t);
            ctx.size_scale_w *= k;
            ctx.size_scale_h *= k;
        } else {
            ctx.Diag(Diagnostic::Level::Warning, "unknown transition prop '" + tr.prop + "'", "anim");
        }
    }
}

inline const AnimSpec* EnterSource(const DrawCtx& ctx) {
    if (!ctx.opts.animations) return nullptr;
    const AnimSpec& a = ctx.node.anim;
    bool delegates = a.stagger > 0 && !ctx.node.children.empty();
    if (!delegates && (!a.enter_preset.empty() || !a.exit_preset.empty())) return &a;
    if (ctx.parent) {
        const AnimSpec& pa = ctx.parent->node.anim;
        if (pa.stagger > 0 && !pa.enter_preset.empty()) return &pa;
    }
    return nullptr;
}

}  // namespace interp_detail

inline void Interpreter::EnsureState(const Panel& panel, PanelInstance& inst) {
    for (const StateVar& sv : panel.state) {
        if (inst.State(sv.name)) continue;
        Value::Kind k = interp_detail::KindForTypeName(sv.type);
        inst.state.emplace_back(sv.name, Value::FromJson(sv.def, k));
    }
}

inline void Interpreter::DrawPanel(const Panel& panel, PanelInstance& inst, const std::vector<BindingRoot>& roots,
                                   const InterpOptions& opts) {
    int frame = ImGui::GetFrameCount();
    if (frame != diag_frame) {
        last_diagnostics.clear();
        diag_frame = frame;
        click_consumed = false;
        SyncAnimations();
        for (const std::string& e : anim_errors) AddDiagnostic(Diagnostic::Level::Error, e, panel.name, "", "animations");
    }
    EnsureState(panel, inst);
    DrawCtx root(*this, panel, inst, roots, opts, panel.root);
    // root ids are usually "window" in every panel and a Window root takes its id outside any window,
    // so without this all panels of a project share motion and runtime state
    ImGui::PushID(panel.name.c_str());
    DrawNodeImpl(root);
    ImGui::PopID();
    inst.first_frame = false;
}

inline void Interpreter::DrawNode(const Node& node, DrawCtx& parent_ctx) {
    DrawCtx c(parent_ctx, node);
    c.sibling_index = parent_ctx.next_sibling;
    c.fill_width = parent_ctx.child_fill_width;
    c.stagger_delay = parent_ctx.child_stagger;
    DrawNodeImpl(c);
}

inline void Interpreter::DrawChildrenOf(DrawCtx& ctx) {
    interp_detail::MotionScope* motion = ctx.content_motion;
    ctx.content_motion = nullptr;
    if (motion && !interp_motion::BeginScope(ctx, *motion)) return;
    const int n = static_cast<int>(ctx.node.children.size());
    for (int i = 0; i < n; ++i) {
        ctx.next_sibling = i;
        ctx.child_fill_width = -1.0f;
        ctx.child_stagger = ctx.node.anim.stagger > 0 ? ctx.node.anim.stagger * static_cast<float>(i) : 0.0f;
        DrawNode(ctx.node.children[static_cast<size_t>(i)], ctx);
    }
    if (motion) imb::EndMotion();
}

inline void Interpreter::ApplyTheme(const Theme& theme) {
    ImGuiStyle& st = ImGui::GetStyle();
    for (const auto& c : theme.colors) {
        int idx = interp_detail::ColorIndex(c.first.c_str());
        if (idx >= 0) st.Colors[idx] = ImVec4(c.second.x, c.second.y, c.second.z, c.second.w);
    }
    for (const auto& v : theme.vars) interp_detail::SetStyleVarJson(st, v.first.c_str(), v.second);
}

inline void Interpreter::DrawNodeImpl(DrawCtx& ctx) {
    using namespace interp_detail;
    if (draw_depth >= kMaxDrawDepth) {
        ctx.Diag(Diagnostic::Level::Error, "node tree nested deeper than " + std::to_string(kMaxDrawDepth) + " levels", "depth");
        return;
    }
    struct DepthGuard {
        int& d;
        explicit DepthGuard(int& v) : d(v) { ++d; }
        ~DepthGuard() { --d; }
    } depth_guard(draw_depth);
    const Node& n = ctx.node;
    const bool is_window = n.type == "Window" && !ctx.opts.design_mode;

    bool vis = true;
    if (!n.visible_if.empty()) {
        Value v;
        if (ctx.Eval(n.visible_if, v)) vis = v.ToBool();
    }
    {
        bool forced = false;
        if (ForcedTrigger(n, "visible", forced)) vis = forced;
    }
    ctx.visible = vis;

    ImGui::PushID(n.id.c_str());
    ctx.id = ImGui::GetID("##imforge_node");
    NodeRt& rt = Rt(ctx.id);

    // motions wrap everything else so exit motions keep drawing a node whose visibleIf went false
    MotionScope motion;
    const interp_motion::Mode motion_mode = interp_motion::Prepare(ctx, motion);
    const bool outer_motion = motion_mode == interp_motion::Mode::Outer;
    bool motion_open = false;
    if (outer_motion) {
        motion_open = interp_motion::BeginScope(ctx, motion);
        if (motion_open) ctx.motion_id = ctx.id;
    } else if (motion_mode == interp_motion::Mode::Content) {
        ctx.content_motion = &motion;
    }

    const AnimSpec* enter_src = EnterSource(ctx);
    bool entered = outer_motion ? motion_open : vis;
    bool legacy_open = false;
    if (enter_src) {
        const std::string& preset = vis ? (enter_src->enter_preset.empty() ? enter_src->exit_preset : enter_src->enter_preset)
                                        : (enter_src->exit_preset.empty() ? enter_src->enter_preset : enter_src->exit_preset);
        float dur = vis ? enter_src->enter_duration : enter_src->exit_duration;
        legacy_open = imb::BeginEnter(ctx.id, vis, dur, PresetFromName(preset.c_str()), ctx.stagger_delay);
        entered = outer_motion ? (motion_open || legacy_open) : legacy_open;
    }
    if (!entered) {
        rt.visible = false;
        rt.hovered = rt.active = rt.focused = false;
        ctx.content_motion = nullptr;
        ImGui::PopID();
        return;
    }
    rt.visible = vis;

    bool enabled = true;
    if (!n.enabled_if.empty()) {
        Value v;
        if (ctx.Eval(n.enabled_if, v)) enabled = v.ToBool();
    }
    {
        bool forced = false;
        if (ForcedTrigger(n, "disabled", forced)) enabled = !forced;
    }
    ctx.enabled = enabled;
    if (!enabled) {
#if IMGUI_VERSION_NUM >= 18600
        ImGui::BeginDisabled();
#else
        ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.6f);
#endif
    }

    ImFont* font = nullptr;
    if (!n.font.empty() && hooks.font) font = hooks.font(n.font);
    if (font) {
#if IMGUI_VERSION_NUM >= 19200
        ImGui::PushFont(font, 0.0f);
#else
        ImGui::PushFont(font);
#endif
    }

    StylePushes pushes;
    for (const std::string& cls : n.style.classes) {
        if (const StyleOverride* so = StyleClass(cls)) PushOverride(ctx, *so, pushes);
        else ctx.Diag(Diagnostic::Level::Warning, "unknown style class '" + cls + "'", "style");
    }
    PushOverride(ctx, n.style, pushes);

    ImVec2 offset(0, 0);
    if (ctx.opts.animations && !n.anim.transitions.empty()) PushTransitions(ctx, rt, pushes, offset);
    if (!is_window && (offset.x != 0.0f || offset.y != 0.0f))
        ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() + offset.x, ImGui::GetCursorPosY() + offset.y));

    if (ctx.Design()) {
        if (const WidgetDef* d = WidgetDefFor(n.type)) {
            if (d->bind_count > 1) {
                for (int i = 0; i < d->bind_count; ++i) {
                    void* p = nullptr;
                    CType t;
                    ctx.Binding(d->binds[i].name, &p, &t);
                }
            }
        }
    }

    const bool want_fx = ctx.opts.fx && !n.fx.empty();
    ImDrawList* dl = nullptr;
    ImDrawListSplitter* splitter = nullptr;
    if (want_fx && !is_window) {
        dl = ImGui::GetWindowDrawList();
        size_t depth = static_cast<size_t>(fx_depth++);
        if (depth >= splitters.size()) splitters.emplace_back(new ImDrawListSplitter());
        splitter = splitters[depth].get();
        splitter->Split(dl, 2);
        splitter->SetCurrentChannel(dl, 1);
    }

    const bool container = IsContainerType(n.type);
    if (WidgetDrawFn fn = FindWidgetFn(n.type)) {
        WidgetEndFn end = FindWidgetEnd(n.type);
        fn(ctx);
        if (end) {
            ctx.DrawChildren();
            end(ctx);
        }
        if (container || end) {
            if (!ctx.has_rect) interp_layout::RectFromItem(ctx);
        }
    } else if (container) {
        DrawContainer(ctx);
    } else if (IsStockLeafWidget(n.type.c_str())) {
        DrawStockWidget(ctx);
    } else {
        ctx.Diag(Diagnostic::Level::Error, "unknown widget type '" + n.type + "'", "type");
        interp_layout::Placeholder(ctx, ("<" + n.type + ">").c_str(), IM_COL32(255, 80, 80, 255));
    }

    if (!container && !ctx.has_rect) interp_layout::RectFromItem(ctx);
    bool item_hovered = container ? (ctx.has_rect && ImGui::IsMouseHoveringRect(ctx.rect_min, ctx.rect_max, false))
                                  : ImGui::IsItemHovered();
    if (!container && !item_hovered && ctx.Design() && ctx.has_rect)
        item_hovered = ImGui::IsMouseHoveringRect(ctx.rect_min, ctx.rect_max, false);

    if (want_fx) {
        const float rounding = ImGui::GetStyle().FrameRounding;
        if (splitter) {
            splitter->SetCurrentChannel(dl, 0);
            if (ctx.has_rect)
                for (const FxSpec& fx : n.fx)
                    if (IsFxBehind(fx.type)) DrawFxBehind(dl, fx, ctx.rect_min, ctx.rect_max, ctx.S(), ctx.id, rounding);
            splitter->Merge(dl);
            --fx_depth;
            dl = ImGui::GetWindowDrawList();
        } else if (ctx.has_rect) {
            for (const FxSpec& fx : n.fx)
                if (IsFxBehind(fx.type))
                    DrawFxBehind(ImGui::GetBackgroundDrawList(), fx, ctx.rect_min, ctx.rect_max, ctx.S(), ctx.id, rounding);
            dl = ImGui::GetForegroundDrawList();
        }
        if (ctx.has_rect)
            for (const FxSpec& fx : n.fx) {
                if (IsFxOver(fx.type)) DrawFxOver(dl, fx, ctx.rect_min, ctx.rect_max, ctx.S(), rounding);
                else if (!IsFxBehind(fx.type)) ctx.Diag(Diagnostic::Level::Warning, "unknown fx type '" + fx.type + "'", "fx");
            }
    }

    if (pushes.colors) ImGui::PopStyleColor(pushes.colors);
    if (pushes.vars) ImGui::PopStyleVar(pushes.vars);
    if (font) ImGui::PopFont();
    if (!enabled) {
#if IMGUI_VERSION_NUM >= 18600
        ImGui::EndDisabled();
#else
        ImGui::PopStyleVar();
        ImGui::PopItemFlag();
#endif
    }
    if (legacy_open) imb::EndEnter();

    rt.hovered = container ? item_hovered : ImGui::IsItemHovered();
    rt.active = !container && ImGui::IsItemActive();
    rt.focused = !container && ImGui::IsItemFocused();
    if (!container) rt.seen = true;
    ctx.content_motion = nullptr;
    if (motion_open) imb::EndMotion();

    if (!n.tooltip.empty() && item_hovered && !ctx.opts.design_mode) {
        std::string tip = ctx.Interpolate(n.tooltip);
        ImGui::SetTooltip("%s", tip.c_str());
    }
    if (ctx.has_rect) {
        if (hooks.on_item) hooks.on_item(n, ctx.rect_min, ctx.rect_max, item_hovered);
        if (ctx.Design()) {
            if (item_hovered && ImGui::IsMouseClicked(0) && !click_consumed) {
                click_consumed = true;
                if (hooks.on_click) hooks.on_click(n);
            }
            if (ctx.opts.selected_id && *ctx.opts.selected_id == n.id)
#if IMGUI_VERSION_NUM >= 19280
                ImGui::GetForegroundDrawList()->AddRect(ctx.rect_min, ctx.rect_max, IM_COL32(255, 180, 0, 255), 0.0f, 2.0f, 0);
#else
                ImGui::GetForegroundDrawList()->AddRect(ctx.rect_min, ctx.rect_max, IM_COL32(255, 180, 0, 255), 0.0f, 0, 2.0f);
#endif
        }
    }
    ImGui::PopID();
    ctx.FlushShadows();
}

}  // namespace imforge

#endif
