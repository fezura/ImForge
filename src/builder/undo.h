#pragma once

#include <string>
#include <vector>

#include "imforge/imforge_json.h"

namespace imforge::builder {

enum class DocKind { None, Panel, Component, Project, Components };

struct UndoEntry {
    std::string label;
    DocKind kind = DocKind::None;
    std::string name;
    Json before, after;
    double time = 0;
};

class UndoStack {
public:
    static constexpr double kCoalesceWindow = 0.3;
    static constexpr size_t kMaxEntries = 200;

    void Push(const std::string& label, DocKind kind, const std::string& name, Json before, Json after, double now) {
        if (before == after) return;
        entries_.resize(cursor_);
        if (cursor_ > 0) {
            UndoEntry& last = entries_[cursor_ - 1];
            if (last.label == label && last.kind == kind && last.name == name && now - last.time <= kCoalesceWindow) {
                last.after = std::move(after);
                last.time = now;
                if (last.before == last.after) {
                    entries_.pop_back();
                    --cursor_;
                }
                return;
            }
        }
        UndoEntry e;
        e.label = label;
        e.kind = kind;
        e.name = name;
        e.before = std::move(before);
        e.after = std::move(after);
        e.time = now;
        entries_.push_back(std::move(e));
        if (entries_.size() > kMaxEntries) entries_.erase(entries_.begin());
        cursor_ = entries_.size();
    }

    bool CanUndo() const { return cursor_ > 0; }
    bool CanRedo() const { return cursor_ < entries_.size(); }
    const UndoEntry* Undo() { return CanUndo() ? &entries_[--cursor_] : nullptr; }
    const UndoEntry* Redo() { return CanRedo() ? &entries_[cursor_++] : nullptr; }
    const UndoEntry* PeekUndo() const { return CanUndo() ? &entries_[cursor_ - 1] : nullptr; }
    const UndoEntry* PeekRedo() const { return CanRedo() ? &entries_[cursor_] : nullptr; }
    void Clear() {
        entries_.clear();
        cursor_ = 0;
    }
    size_t Size() const { return entries_.size(); }
    size_t Cursor() const { return cursor_; }
    const std::vector<UndoEntry>& Entries() const { return entries_; }

private:
    std::vector<UndoEntry> entries_;
    size_t cursor_ = 0;
};

}  // namespace imforge::builder
