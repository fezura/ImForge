#pragma once

#include <set>
#include <string>
#include <vector>

namespace imforge::builder {

struct LiveBlockStatus {
    std::string panel, node_id, event;
    std::string key;
    std::string function_name;
    std::string expected_hash, app_hash;
    bool is_handler = false;
    bool registered = false;
    bool stale = false;
};

struct LiveStatus {
    bool transport_available = true;
    bool listening = false;
    bool connected = false;
    bool client_info_received = false;
    bool picker = false;
    bool auto_push = true;
    std::string url, app, backend, platform;
    int pid = 0;
    int imgui_version = 0;
    float fps = 0;
    std::vector<float> fps_history;
    std::vector<std::string> registered_widgets, texture_names, app_panels, missing_panels;
    std::string hover_panel, hover_id;
    std::set<std::string> stale_ids, unregistered_ids;
    std::vector<LiveBlockStatus> blocks;
    double last_push_time = 0;
    size_t last_push_bytes = 0;
    unsigned stale_count = 0, unregistered_count = 0;
    bool pairing_required = true;
    bool paired = false;
    bool pairing_in_progress = false;
    bool unauthenticated = false;
    int pairing_failures = 0;
    int security_events = 0;
    std::string pairing_state, pairing_file, last_security_event;

    static std::string Key(const std::string& panel, const std::string& id) { return panel + "." + id; }
    bool IsStale(const std::string& panel, const std::string& id) const { return stale_ids.count(Key(panel, id)) != 0; }
    bool IsUnregistered(const std::string& panel, const std::string& id) const {
        return unregistered_ids.count(Key(panel, id)) != 0;
    }
    bool HasWidget(const std::string& type) const {
        for (const std::string& w : registered_widgets)
            if (w == type) return true;
        return false;
    }
    bool HasTexture(const std::string& name) const {
        for (const std::string& t : texture_names)
            if (t == name) return true;
        return false;
    }
    bool AppDrawsPanel(const std::string& name) const {
        for (const std::string& p : app_panels)
            if (p == name) return true;
        return false;
    }
};

inline LiveStatus& GetLiveStatus() {
    static LiveStatus status;
    return status;
}

}  // namespace imforge::builder
