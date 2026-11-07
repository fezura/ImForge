#pragma once

#include <map>
#include <string>
#include <vector>

namespace imforge::builder {

struct ConflictItem {
    std::string panel, file;
    std::string node_id, reason, user_code, generated_code;
    bool resolved = false, keep_user = false;
};

struct ConflictsState {
    std::vector<ConflictItem> conflicts;
    std::map<std::string, std::string> merged_files;
    std::vector<std::string> report;
    std::vector<std::string> updated_nodes;
    std::string output_dir;
    unsigned generation = 0;

    void Clear() {
        conflicts.clear();
        merged_files.clear();
        report.clear();
        updated_nodes.clear();
        output_dir.clear();
        ++generation;
    }
    void Touch() { ++generation; }
    int Unresolved() const {
        int n = 0;
        for (const ConflictItem& c : conflicts)
            if (!c.resolved) ++n;
        return n;
    }
};

inline ConflictsState& GetConflictsState() {
    static ConflictsState state;
    return state;
}

}  // namespace imforge::builder
