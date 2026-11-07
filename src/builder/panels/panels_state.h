#pragma once

#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include "imforge/imforge_core.h"

#include "../project_io.h"

namespace imforge::builder {

// Panel files removed or renamed in the editor only get deleted from disk on save. The Panels panel
// queues them here and FlushPendingDeletes runs after a successful save (via on_project_changed("saved")).
// Calling it again by hand is harmless.
struct PendingPanelDelete {
    std::string dir, file;
};

struct DeletedPanel {
    std::string dir;
    Panel panel;
    size_t index = 0;
};

struct PanelFileOps {
    std::vector<PendingPanelDelete> pending_deletes;
    std::vector<DeletedPanel> trash;
    bool request_new = false;
};

inline PanelFileOps& GetPanelFileOps() {
    static PanelFileOps ops;
    return ops;
}

inline void QueuePanelFileDelete(const std::string& dir, const std::string& file) {
    if (dir.empty() || file.empty()) return;
    PanelFileOps& ops = GetPanelFileOps();
    std::string nd = NormalizePath(dir);
    for (const PendingPanelDelete& d : ops.pending_deletes)
        if (d.dir == nd && d.file == file) return;
    ops.pending_deletes.push_back(PendingPanelDelete{nd, file});
}

inline void ClearPanelFileOps() {
    PanelFileOps& ops = GetPanelFileOps();
    ops.pending_deletes.clear();
    ops.trash.clear();
}

inline void FlushPendingDeletes(const Project& project) {
    if (project.dir.empty()) return;
    PanelFileOps& ops = GetPanelFileOps();
    std::string dir = NormalizePath(project.dir);
    static const std::string kSuffix = ".imb.json";
    std::vector<PendingPanelDelete> keep;
    for (const PendingPanelDelete& d : ops.pending_deletes) {
        if (d.dir != dir) {
            keep.push_back(d);
            continue;
        }
        std::string safe = SafeJoin(dir, d.file);
        if (safe.empty()) continue;
        std::string target = NormalizePath(safe);
        bool referenced = false;
        for (const Panel& p : project.panels)
            if (!p.file.empty() && NormalizePath(JoinPath(dir, p.file)) == target) referenced = true;
        for (const std::string& f : project.panel_files)
            if (NormalizePath(JoinPath(dir, f)) == target) referenced = true;
        bool panel_file = d.file.size() > kSuffix.size() && d.file.compare(d.file.size() - kSuffix.size(), kSuffix.size(), kSuffix) == 0;
        if (referenced || !panel_file) continue;
        std::error_code ec;
        std::filesystem::remove(std::filesystem::path(target), ec);
    }
    ops.pending_deletes = std::move(keep);
    std::vector<DeletedPanel> trash;
    for (DeletedPanel& t : ops.trash)
        if (t.dir != dir) trash.push_back(std::move(t));
    ops.trash = std::move(trash);
}

}  // namespace imforge::builder
