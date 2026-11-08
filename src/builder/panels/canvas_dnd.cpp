#include "canvas_panel.h"

#if IMFORGE_HAS_INTERP

#include <algorithm>
#include <cmath>
#include <cstring>

#include "imgui_internal.h"

#include "../builder_style.h"
#include "../ui/canvas_overlay.h"
#include "../ui/tree_ops.h"
#include "library_actions.h"

namespace imforge::builder {

namespace {

const char* const kPayloadNode = "IMFORGE_NODE";
const char* const kPayloadNew = "IMFORGE_NEW";
const char* const kComponentPrefix = "Component:";

std::string PayloadString(const ImGuiPayload* p) {
    if (!p || !p->Data || p->DataSize <= 0) return std::string();
    const char* s = static_cast<const char*>(p->Data);
    size_t n = 0;
    while (n < (size_t)p->DataSize && s[n]) ++n;
    return std::string(s, n);
}

bool Contains(const ImVec2& mn, const ImVec2& mx, const ImVec2& p) {
    return p.x >= mn.x && p.y >= mn.y && p.x < mx.x && p.y < mx.y;
}

bool IsHorizontalFlow(const std::string& type) { return type == "HStack" || type == "MenuBar"; }

}  // namespace

void CanvasPanel::HandleDragDrop(EditorContext& ctx, Node& root, const ImVec2& region_min, const ImVec2& region_max) {
    const ImGuiPayload* p = ImGui::GetDragDropPayload();
    if (!p || AcceptMotionDropRect(ctx, hovered_id_, region_min, region_max)) return;
    const bool is_new = p->IsDataType(kPayloadNew);
    const bool is_node = p->IsDataType(kPayloadNode);
    if (!is_new && !is_node) return;
    std::string data = PayloadString(p);
    if (data.empty()) return;

    std::string type, moving_id;
    if (is_new) {
        type = data.compare(0, std::strlen(kComponentPrefix), kComponentPrefix) == 0 ? "Component" : data;
    } else {
        moving_id = data;
        const Node* n = FindNode(root, moving_id);
        if (!n || n == &root) return;
        type = n->type;
    }

    ImRect bb(region_min, region_max);
    if (!ImGui::BeginDragDropTargetCustom(bb, ImGui::GetID("##canvas_drop"))) return;
    DropTarget target = ComputeDropTarget(ctx, root, type, moving_id, ImGui::GetIO().MousePos);
    ImGuiDragDropFlags drop_flags = ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect;
    if (const ImGuiPayload* acc = ImGui::AcceptDragDropPayload(p->DataType, drop_flags)) {
        if (target.Valid()) {
            ImDrawList* dl = ImGui::GetForegroundDrawList();
            dl->PushClipRect(clip_min_, clip_max_, true);
            DrawDropMarker(dl, target);
            dl->PopClipRect();
            if (acc->IsDelivery()) {
                ApplyDrop(ctx, root, target, is_new, data);
                tree_changed_ = true;
            }
        } else {
            ImGui::SetMouseCursor(ImGuiMouseCursor_NotAllowed);
        }
    }
    ImGui::EndDragDropTarget();
}

DropTarget CanvasPanel::ComputeDropTarget(EditorContext& ctx, const Node& root, const std::string& type,
                                          const std::string& moving_id, const ImVec2& mouse) const {
    DropTarget t;
    if (!Contains(clip_min_, clip_max_, mouse)) return t;
    const Node* moving = moving_id.empty() ? nullptr : FindNode(root, moving_id);

    std::vector<const CanvasItem*> containers;
    for (const CanvasItem& it : items_) {
        if (!it.node || !IsContainerNode(ctx, *it.node) || it.node->locked) continue;
        if (moving && (it.id == moving_id || FindNode(*moving, it.id))) continue;
        if (!Contains(it.emin, it.emax, mouse)) continue;
        if (!ctx.CanAcceptChild(*it.node, type)) continue;
        containers.push_back(&it);
    }
    if (containers.empty()) {
        const CanvasItem* ri = Item(root.id);
        if (!ri || !Contains(frame_min_, frame_max_, mouse) || !ctx.CanAcceptChild(root, type)) return t;
        containers.push_back(ri);
    }
    std::sort(containers.begin(), containers.end(), [](const CanvasItem* a, const CanvasItem* b) {
        if (a->depth != b->depth) return a->depth > b->depth;
        return (a->emax.x - a->emin.x) * (a->emax.y - a->emin.y) < (b->emax.x - b->emin.x) * (b->emax.y - b->emin.y);
    });
    const CanvasItem& parent = *containers.front();
    const Node& pn = *parent.node;
    t.parent_id = pn.id;

    if (pn.type == "Canvas") {
        t.kind = DropTarget::Kind::Free;
        t.index = (int)pn.children.size();
        t.offset.x = (mouse.x - parent.min.x - canvas_pad_.x) / scale_;
        t.offset.y = (mouse.y - parent.min.y - canvas_pad_.y) / scale_;
        if (ImGui::GetIO().KeyShift) {
            t.offset.x = std::round(t.offset.x / kSnapStep) * kSnapStep;
            t.offset.y = std::round(t.offset.y / kSnapStep) * kSnapStep;
        }
        t.rect_min = ImVec2(parent.min.x + canvas_pad_.x + t.offset.x * scale_, parent.min.y + canvas_pad_.y + t.offset.y * scale_);
        t.rect_max = ImVec2(t.rect_min.x + 48.0f * scale_, t.rect_min.y + 20.0f * scale_);
        return t;
    }

    struct Vis {
        int index;
        const CanvasItem* item;
    };
    std::vector<Vis> visible;
    for (size_t i = 0; i < pn.children.size(); ++i) {
        const Node& c = pn.children[i];
        if (c.id == moving_id) continue;
        if (const CanvasItem* ci = Item(c.id)) visible.push_back(Vis{(int)i, ci});
    }
    if (visible.empty()) {
        t.kind = DropTarget::Kind::Empty;
        t.index = (int)pn.children.size();
        t.rect_min = parent.emin;
        t.rect_max = parent.emax;
        return t;
    }

    const bool horizontal = IsHorizontalFlow(pn.type);
    if (pn.type == "Grid") {
        const Vis* hit = nullptr;
        for (const Vis& v : visible)
            if (Contains(v.item->emin, v.item->emax, mouse)) hit = &v;
        t.kind = DropTarget::Kind::Cell;
        if (hit) {
            float cx = (hit->item->emin.x + hit->item->emax.x) * 0.5f;
            bool after = mouse.x > cx;
            t.index = hit->index + (after ? 1 : 0);
            t.rect_min = hit->item->emin;
            t.rect_max = hit->item->emax;
            float x = after ? hit->item->emax.x : hit->item->emin.x;
            t.a = ImVec2(x, hit->item->emin.y);
            t.b = ImVec2(x, hit->item->emax.y);
        } else {
            const Vis& last = visible.back();
            t.index = (int)pn.children.size();
            t.rect_min = last.item->emin;
            t.rect_max = last.item->emax;
            t.a = ImVec2(last.item->emax.x, last.item->emin.y);
            t.b = ImVec2(last.item->emax.x, last.item->emax.y);
        }
        return t;
    }

    t.kind = DropTarget::Kind::Line;
    const Vis* prev = nullptr;
    const Vis* next = nullptr;
    for (const Vis& v : visible) {
        float center = horizontal ? (v.item->emin.x + v.item->emax.x) * 0.5f : (v.item->emin.y + v.item->emax.y) * 0.5f;
        float m = horizontal ? mouse.x : mouse.y;
        if (m < center) {
            next = &v;
            break;
        }
        prev = &v;
    }
    t.index = next ? next->index : (int)pn.children.size();
    if (horizontal) {
        float x = prev && next ? (prev->item->emax.x + next->item->emin.x) * 0.5f
                  : next       ? next->item->emin.x - 2.0f
                               : prev->item->emax.x + 2.0f;
        t.a = ImVec2(x, parent.emin.y);
        t.b = ImVec2(x, parent.emax.y);
    } else {
        float y = prev && next ? (prev->item->emax.y + next->item->emin.y) * 0.5f
                  : next       ? next->item->emin.y - 2.0f
                               : prev->item->emax.y + 2.0f;
        t.a = ImVec2(parent.emin.x, y);
        t.b = ImVec2(parent.emax.x, y);
    }
    t.rect_min = parent.emin;
    t.rect_max = parent.emax;
    return t;
}

void CanvasPanel::DrawDropMarker(ImDrawList* dl, const DropTarget& t) const {
    const BuilderColors& c = Colors();
    const ImU32 accent = ToU32(c.accent);
    switch (t.kind) {
    case DropTarget::Kind::Line:
        overlay::DashedRect(dl, t.rect_min, t.rect_max, ToU32(c.accent, 0.35f), 4.0f, 3.0f, 1.0f);
        overlay::InsertMarker(dl, t.a, t.b, accent, 2.0f, 5.0f);
        break;
    case DropTarget::Kind::Cell:
        overlay::DropHighlight(dl, t.rect_min, t.rect_max, ToU32(c.accent, 0.12f), ToU32(c.accent, 0.6f), 2.0f, 1.0f);
        overlay::InsertMarker(dl, t.a, t.b, accent, 3.0f, 5.0f);
        break;
    case DropTarget::Kind::Empty:
        overlay::DropHighlight(dl, t.rect_min, t.rect_max, ToU32(c.accent, 0.15f), accent, 2.0f, 2.0f);
        break;
    case DropTarget::Kind::Free:
        overlay::DashedRect(dl, t.rect_min, t.rect_max, accent, 4.0f, 3.0f, 1.5f);
        dl->AddCircleFilled(t.rect_min, 3.0f, accent);
        break;
    default: break;
    }
}

void CanvasPanel::ApplyDrop(EditorContext& ctx, Node& root, const DropTarget& t, bool is_new, const std::string& payload) {
    Node* parent = t.parent_id == root.id ? &root : FindNode(root, t.parent_id);
    if (!parent) return;
    const bool free = t.kind == DropTarget::Kind::Free;
    if (is_new) {
        std::string new_id;
        if (payload.compare(0, std::strlen(kComponentPrefix), kComponentPrefix) == 0) {
            const Component* def = ctx.project.FindComponent(payload.substr(std::strlen(kComponentPrefix)));
            if (!def) return;
            Node n = tree::MakeComponentInstance(*def, root);
            new_id = n.id;
            ctx.BeginEdit("Insert");
            size_t at = t.index < 0 || (size_t)t.index > parent->children.size() ? parent->children.size() : (size_t)t.index;
            parent->children.insert(parent->children.begin() + (long)at, std::move(n));
            if (free) {
                parent->children[at].layout.anchor = "TopLeft";
                parent->children[at].layout.offset = t.offset;
            }
            ctx.EndEdit();
            ctx.Select(new_id);
            return;
        }
        ctx.BeginEdit("Insert");
        Node* n = ctx.InsertNode(payload, t.parent_id, t.index, "Insert");
        if (n && free) {
            n->layout.anchor = "TopLeft";
            n->layout.offset = t.offset;
        }
        ctx.EndEdit();
        return;
    }
    ctx.BeginEdit("Move");
    const bool moved = ctx.MoveNode(payload, t.parent_id, t.index);
    if (moved && free) {
        if (Node* n = FindNode(root, payload)) {
            n->layout.anchor = "TopLeft";
            n->layout.offset = t.offset;
        }
    }
    ctx.EndEdit();
    if (moved) ctx.Select(payload);
}

}  // namespace imforge::builder

#endif
