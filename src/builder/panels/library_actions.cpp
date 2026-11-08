#include "library_actions.h"

#include <cstring>

#include "imgui_internal.h"

#include "../editor_context.h"
#include "../ui/motion_preview.h"

#if IMFORGE_HAS_MOTION && __has_include("imforge/imforge_motion_json.h")
#include "imforge/imforge_motion_json.h"
#define IMFORGE_LIBRARY_MOTION_JSON 1
#else
#define IMFORGE_LIBRARY_MOTION_JSON 0
#endif

namespace imforge::builder {

namespace {

bool SplitPayload(const std::string& payload, std::string& name) {
    const size_t n = std::strlen(kCustomMotionPrefix);
    if (payload.compare(0, n, kCustomMotionPrefix) == 0) {
        name = payload.substr(n);
        return true;
    }
    name = payload;
    return false;
}

std::string PayloadText(const ImGuiPayload* p) {
    if (!p || !p->Data || p->DataSize <= 0) return std::string();
    const char* s = static_cast<const char*>(p->Data);
    size_t n = 0;
    while (n < (size_t)p->DataSize && s[n]) ++n;
    return std::string(s, n);
}

void Deliver(EditorContext& ctx, const std::string& node_id, const ImGuiPayload* p) {
    std::string payload = PayloadText(p);
    if (payload.empty()) return;
    AddMotionToNodes(ctx, {node_id}, payload, DefaultMotionTrigger(ctx, payload));
    ctx.Select(node_id);
}

}  // namespace

std::string MotionPayloadFor(const std::string& name, bool custom) { return custom ? kCustomMotionPrefix + name : name; }

std::string DefaultMotionTrigger(EditorContext& ctx, const std::string& payload) {
    std::string name;
    if (SplitPayload(payload, name)) {
        if (const Json* a = ctx.project.FindAnimation(name))
            if ((*a)["trigger"].IsString()) return (*a)["trigger"].AsString();
        return "enter";
    }
#if IMFORGE_LIBRARY_MOTION_JSON
    if (const imb::AnimDef* def = imb::FindAnim(name.c_str())) return MotionTriggerName(def->default_trigger);
#endif
    return "enter";
}

int AddMotionToNodes(EditorContext& ctx, const std::vector<std::string>& ids, const std::string& payload,
                     const std::string& trigger) {
    std::string name;
    const bool custom = SplitPayload(payload, name);
    if (name.empty()) return 0;
    std::vector<std::string> targets;
    for (const std::string& id : ids)
        if (ctx.FindActiveNode(id)) targets.push_back(id);
    if (targets.empty()) return 0;
    const std::string label = "Add motion " + name;
    int added = 0;
    ctx.BeginEdit(label.c_str());
    for (const std::string& id : targets) {
        Node* n = ctx.FindActiveNode(id);
        if (!n) continue;
        bool dup = false;
        for (const MotionSpec& m : n->anim.motions)
            dup = dup || (m.Name() == name && m.trigger == trigger && m.custom.empty() == !custom);
        if (dup) continue;
        MotionSpec ms;
        if (custom) ms.custom = name;
        else ms.preset = name;
        ms.trigger = trigger.empty() ? "enter" : trigger;
        n->anim.motions.push_back(ms);
        ++added;
    }
    ctx.EndEdit();
    if (added > 0) {
        ctx.SetStatus(name + " (" + trigger + ") added to " + std::to_string(added) + (added == 1 ? " node" : " nodes"));
        ctx.Log(LogLevel::Info, "motion %s on %s added to %d node%s", name.c_str(), trigger.c_str(), added, added == 1 ? "" : "s");
    } else
        ctx.SetStatus(name + " is already on " + (targets.size() == 1 ? targets.front() : std::string("those nodes")));
    return added;
}

bool AcceptMotionDrop(EditorContext& ctx, const std::string& node_id) {
    const ImGuiPayload* p = ImGui::GetDragDropPayload();
    if (!p || !p->IsDataType(kPayloadMotion)) return false;
    if (node_id.empty() || !ctx.FindActiveNode(node_id)) return true;
    if (const ImGuiPayload* acc = ImGui::AcceptDragDropPayload(kPayloadMotion))
        if (acc->IsDelivery()) Deliver(ctx, node_id, acc);
    return true;
}

bool AcceptMotionDropRect(EditorContext& ctx, const std::string& node_id, const ImVec2& min, const ImVec2& max) {
    const ImGuiPayload* p = ImGui::GetDragDropPayload();
    if (!p || !p->IsDataType(kPayloadMotion)) return false;
    if (!ImGui::BeginDragDropTargetCustom(ImRect(min, max), ImGui::GetID("##motion_drop"))) return true;
    if (!node_id.empty() && ctx.FindActiveNode(node_id)) {
        if (const ImGuiPayload* acc = ImGui::AcceptDragDropPayload(kPayloadMotion, ImGuiDragDropFlags_AcceptNoDrawDefaultRect))
            if (acc->IsDelivery()) Deliver(ctx, node_id, acc);
    } else {
        ImGui::SetMouseCursor(ImGuiMouseCursor_NotAllowed);
    }
    ImGui::EndDragDropTarget();
    return true;
}

}  // namespace imforge::builder
