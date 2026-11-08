#pragma once

#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "imgui.h"
#if IMGUI_VERSION_NUM < 18600
#include "imgui_internal.h"
#endif

#include "imforge_core.h"
#include "imforge_expr.h"
#include "imb_runtime.h"
#include "imb_motion.h"
#include "imb_motion_presets.h"
#include "imforge_motion_json.h"
#include "imforge_interp_detail.h"

namespace imforge {

namespace interp_detail {
struct MotionScope {
    imb::MotionBinding bindings[imb::detail::kMaxMotionBindings];
    int count = 0;
    imb::MotionOptions options;
};
}  // namespace interp_detail

struct BindingRoot {
    std::string name;
    void* ptr = nullptr;
    const StructInfo* type = nullptr;
};

struct PanelInstance {
    std::vector<std::pair<std::string, Value>> state;
    std::vector<std::pair<std::string, Value>> mock;
    std::vector<std::pair<std::string, std::string>> pending_tab;
    std::vector<std::string> pending_popups;
    bool first_frame = true;

    Value* State(const std::string& name) {
        for (auto& kv : state)
            if (kv.first == name) return &kv.second;
        return nullptr;
    }
    Value* Mock(const std::string& key) {
        for (auto& kv : mock)
            if (kv.first == key) return &kv.second;
        return nullptr;
    }
    Value& MockOrCreate(const std::string& key, const Value& init) {
        if (Value* v = Mock(key)) return *v;
        mock.emplace_back(key, init);
        return mock.back().second;
    }
};

struct DrawCtx;
using WidgetDrawFn = bool (*)(DrawCtx& ctx);
using BlockFn = void (*)(DrawCtx& ctx);
using HandlerFn = void (*)(DrawCtx& ctx);
using WidgetEndFn = void (*)(DrawCtx& ctx);

struct Hooks {
    std::function<void(const Node&, const ImVec2& min, const ImVec2& max, bool hovered)> on_item;
    std::function<void(const Node&)> on_click;
    std::function<ImTextureID(const std::string& name)> texture;
    std::function<ImFont*(const std::string& name)> font;
    std::function<const char*(const std::string& name)> icon;
    std::function<bool(const Node&, const char* trigger, bool& forced_value)> force_trigger;
    std::function<void(const Node&, imb::MotionOptions&)> motion_options;
};

struct InterpOptions {
    bool design_mode = false;
    float dpi_scale = 1.0f;
    bool run_handlers = true;
    bool animations = true;
    bool fx = true;
    const std::string* selected_id = nullptr;
    const char* picker_mode = nullptr;
};

struct Interpreter {
    struct NodeRt {
        bool hovered = false, active = false, focused = false, open = false, visible = true;
        bool seen = false, shown = false;
        float width = 0;
        int tab = -1;
    };

    const Project* project = nullptr;
    WidgetCatalog catalog;
    std::vector<std::pair<std::string, WidgetDrawFn>> widgets;
    std::vector<std::pair<std::string, WidgetEndFn>> widget_ends;
    std::vector<std::pair<std::string, BlockFn>> blocks;
    std::vector<std::pair<std::string, HandlerFn>> handlers;
    Hooks hooks;
    std::vector<Diagnostic> last_diagnostics;

    std::map<ImGuiID, NodeRt> node_rt;
    std::map<std::string, interp_detail::CachedExpr> exprs;
    std::map<std::string, interp_detail::CachedInterp> interps;
    std::map<std::string, Json> prop_defaults;
    std::vector<std::unique_ptr<ImDrawListSplitter>> splitters;
    int fx_depth = 0;
    int draw_depth = 0;
    int tab_bar_depth = 0;
    int diag_frame = -1;
    bool click_consumed = false;
    const Project* anim_project = nullptr;
    unsigned long long anim_hash = 0;
    std::vector<std::string> anim_errors;
    std::map<std::string, int> anim_names;

    void DrawPanel(const Panel& panel, PanelInstance& inst, const std::vector<BindingRoot>& roots,
                   const InterpOptions& opts);
    // registers project->animations in the imb registry when they changed; DrawPanel calls it once per frame
    void SyncAnimations();
    void DrawNode(const Node& node, DrawCtx& parent_ctx);
    void ApplyTheme(const Theme& theme);

    void RegisterWidget(const char* type, WidgetDrawFn begin, WidgetEndFn end = nullptr) {
        interp_detail::SetEntry(widgets, type, begin);
        if (end) interp_detail::SetEntry(widget_ends, type, end);
    }
    void RegisterBlock(const char* key, BlockFn fn) { interp_detail::SetEntry(blocks, key, fn); }
    void RegisterHandler(const char* key, HandlerFn fn) { interp_detail::SetEntry(handlers, key, fn); }
    WidgetDrawFn FindWidgetFn(const std::string& type) const { return interp_detail::FindEntry(widgets, type); }
    WidgetEndFn FindWidgetEnd(const std::string& type) const { return interp_detail::FindEntry(widget_ends, type); }
    bool ForcedTrigger(const Node& n, const char* trigger, bool& out) const {
        return hooks.force_trigger && hooks.force_trigger(n, trigger, out);
    }
    BlockFn FindBlock(const std::string& key) const { return interp_detail::FindEntry(blocks, key); }
    HandlerFn FindHandler(const std::string& key) const { return interp_detail::FindEntry(handlers, key); }

    const WidgetDef* WidgetDefFor(const std::string& type) const {
        if (const WidgetDef* d = catalog.Find(type.c_str())) return d;
        if (project && project->widget_catalog)
            if (const WidgetDef* d = project->widget_catalog->Find(type.c_str())) return d;
        return FindWidget(type.c_str());
    }
    const Json* PropDefault(const std::string& type, const char* prop);
    const StyleOverride* StyleClass(const std::string& name) const {
        if (!project) return nullptr;
        for (const auto& kv : project->style_classes)
            if (kv.first == name) return &kv.second;
        return nullptr;
    }
    const Component* FindComponent(const std::string& name) const {
        return project ? project->FindComponent(name) : nullptr;
    }
    NodeRt& Rt(ImGuiID id) { return node_rt[id]; }
    void AddDiagnostic(Diagnostic::Level level, const std::string& msg, const std::string& panel,
                       const std::string& node_id, const char* prop);

    void DrawNodeImpl(DrawCtx& ctx);
    void DrawChildrenOf(DrawCtx& ctx);
    void EnsureState(const Panel& panel, PanelInstance& inst);
};

inline bool DrawStockWidget(DrawCtx& ctx);
inline bool IsStockLeafWidget(const char* type);
inline bool DrawContainer(DrawCtx& ctx);
inline bool IsContainerType(const std::string& type);

struct DrawCtx {
    Interpreter& interp;
    const Panel& panel;
    PanelInstance& inst;
    const std::vector<BindingRoot>& roots;
    const InterpOptions& opts;
    const Node& node;
    const Component* component = nullptr;
    const Node* instance_node = nullptr;
    DrawCtx* parent = nullptr;
    DrawCtx* outer = nullptr;

    ImGuiID id = 0;
    int sibling_index = 0;
    float fill_width = -1.0f;
    float stagger_delay = 0.0f;
    float size_scale_w = 1.0f, size_scale_h = 1.0f;
    bool visible = true, enabled = true;
    bool has_rect = false;
    ImVec2 rect_min, rect_max;
    int next_sibling = 0;
    int selected_child = -1;
    float child_fill_width = -1.0f;
    float child_stagger = 0.0f;
    ImGuiID motion_id = 0;
    interp_detail::MotionScope* content_motion = nullptr;
    mutable interp_detail::Shadow shadows[4];
    mutable int shadow_count = 0;

    DrawCtx(Interpreter& i, const Panel& p, PanelInstance& in, const std::vector<BindingRoot>& r,
            const InterpOptions& o, const Node& n)
        : interp(i), panel(p), inst(in), roots(r), opts(o), node(n) {}
    DrawCtx(DrawCtx& par, const Node& n)
        : interp(par.interp), panel(par.panel), inst(par.inst), roots(par.roots), opts(par.opts), node(n),
          component(par.component), instance_node(par.instance_node), parent(&par), outer(par.outer) {}

    float S() const { return opts.dpi_scale > 0 ? opts.dpi_scale : 1.0f; }
    bool Design() const { return opts.design_mode || opts.picker_mode != nullptr; }
    const WidgetDef* Def() const { return interp.WidgetDefFor(node.type); }
    const PropValue* RawProp(const char* prop) const { return node.Prop(prop); }
    bool HasProp(const char* prop) const { return node.Prop(prop) != nullptr; }
    bool HasBinding(const char* slot) const { return node.Bind(slot) != nullptr; }
    bool HasEvent(const char* event) const;

    bool GetValue(const char* prop, Value& out, Value::Kind hint = Value::Kind::Null) const;
    bool GetBool(const char* prop, bool def = false) const;
    int GetInt(const char* prop, int def = 0) const;
    float GetFloat(const char* prop, float def = 0) const;
    std::string GetString(const char* prop, const char* def = "") const;
    ImVec4 GetColor(const char* prop, ImVec4 def) const;
    ImVec2 GetVec2(const char* prop, ImVec2 def) const;
    bool GetSizeSpec(const char* prop, SizeSpec& w, SizeSpec& h) const;
    ImVec2 GetSize(const char* prop = "size") const;
    int GetFlags(const char* prop, const char* enum_name) const;
    int GetEnum(const char* prop, const char* values, int def = 0) const;
    std::vector<std::string> GetStringList(const char* prop) const;
    std::string Interpolate(const std::string& text) const;

    bool Binding(const char* slot, void** ptr, CType* type) const;
    std::string BindingPath(const char* slot) const;
    bool* BindBool(const char* slot) const;
    int* BindInt(const char* slot) const;
    float* BindFloat(const char* slot, int count = 1) const;
    float* BindFloatArray(const char* slot, int* count) const;
    std::string* BindStdString(const char* slot) const;
    char* BindCharArray(const char* slot, int* cap) const;
    ImTextureID GetTexture(const char* slot) const;

    bool ResolveStorage(const std::string& path, void** ptr, CType* type) const;
    bool ReadPath(const std::string& path, Value& out) const;
    bool WritePath(const std::string& path, const Value& v) const;
    bool Eval(const std::string& expr, Value& out) const;
    bool ResolveParamValue(const std::string& param, Value& out) const;
    bool ResolveParamStorage(const std::string& param, void** ptr, CType* type) const;

    void FireEvent(const char* event);
    void DrawChildren();
    void DrawChildrenInCells();
    void Diag(Diagnostic::Level level, const std::string& msg, const char* prop = "") const {
        interp.AddDiagnostic(level, msg, panel.name, node.id, prop);
    }
    std::string ScopeKey() const;
    void FlushShadows() const;
    bool MockStorage(const std::string& key, const char* slot, void** ptr, CType* type) const;
    void* ShadowFor(void* dst, const CType& t, bool as_int, int count) const;
};

namespace interp_motion {
inline std::string RevealText(const std::string& text, float reveal);
inline void TriggerFor(const DrawCtx& ctx, const char* event);
inline bool Listens(const Node& n, const char* event);
}  // namespace interp_motion

}  // namespace imforge

#define IMFORGE_INTERP_PARTS 1
#include "imforge_interp_ctx.h"
#include "imforge_interp_layout.h"
#include "imforge_interp_motion.h"
#include "imforge_interp_engine.h"
#include "imforge_interp_widgets.h"
