#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "imforge/imforge_core.h"
#include "panels/live_status.h"
#include "undo.h"

namespace imforge::builder {

struct EditorContext;
struct Prefs;
struct LivePairingInfo;

// "Require pairing" is a per-user pref (prefs.extra.liveRequirePairing, default on) and never a project setting,
// otherwise a project from a stranger could switch authentication off
bool LiveRequirePairingPref(const Prefs& prefs);
void SetLiveRequirePairingPref(Prefs& prefs, bool on);
std::string LivePairingStateText(const LivePairingInfo& p);

struct LiveIssue {
    enum class Severity { Info, Warning, Error };
    Severity severity = Severity::Info;
    std::string title, detail;
};

struct LivePanelRow {
    std::string name;
    bool in_project = false;
    bool drawn_by_app = false;
    int registered_blocks = 0;
};

class LiveBridge {
public:
    static constexpr double kDebounceSeconds = 0.15;
    static constexpr double kStatusRefreshSeconds = 1.0;
    static constexpr double kFpsSampleSeconds = 0.5;
    static constexpr size_t kFpsSamples = 120;

    using BlockHashProvider = std::function<std::string(const Project&, const Panel&, const Node&)>;

    LiveBridge();
    ~LiveBridge();

    void Attach(EditorContext& ctx);
    void Update(EditorContext& ctx);

    bool Start(EditorContext& ctx, std::string* err);
    void Stop(EditorContext& ctx);
    bool Restart(EditorContext& ctx, std::string* err);
    bool NeedsRestart(const EditorContext& ctx) const;

    bool PushAll(EditorContext& ctx);
    bool PushPanel(EditorContext& ctx, const std::string& name);

    void SetAutoPush(bool on) { auto_push_ = on; }
    bool AutoPush() const { return auto_push_; }
    void SetPicker(EditorContext& ctx, bool on);
    bool ConsumePick(std::string& panel, std::string& id);

    void RefreshStatus(EditorContext& ctx);
    void SetBlockHashProvider(BlockHashProvider fn) { hash_provider_ = std::move(fn); }

    std::string ClientSnippet(const EditorContext& ctx) const;
    std::vector<LiveIssue> Troubleshoot(const EditorContext& ctx) const;
    std::vector<LivePanelRow> PanelRows(const EditorContext& ctx) const;

    double AppLogClearTime() const { return app_log_clear_time_; }
    void ClearAppLog(EditorContext& ctx);
    const std::string& LastError() const { return last_error_; }
    double LastHelloWaitSince() const { return connected_since_; }

private:
    struct Pending {
        DocKind kind = DocKind::None;
        std::string name;
        double time = 0;
    };

    void OnDocumentChanged(DocKind kind, const std::string& name);
    void OnSelectionChanged();
    void FlushPending(EditorContext& ctx);
    void TrackPickerSync(EditorContext& ctx);
    void SampleFps(EditorContext& ctx);
    void RecordPush(EditorContext& ctx, size_t bytes_before);
    void ScanExportedHashes(const EditorContext& ctx);
    void CollectBlocks(const EditorContext& ctx, const Panel& panel, const Node& node, std::vector<LiveBlockStatus>& out) const;
    std::string ExpectedHash(const EditorContext& ctx, const Panel& panel, const Node& node) const;

    std::shared_ptr<bool> alive_;
    EditorContext* attached_ = nullptr;
    bool auto_push_ = true;
    std::vector<Pending> pending_;
    double last_change_time_ = 0;
    bool pending_everything_ = false;
    double last_status_refresh_ = 0;
    double last_fps_sample_ = 0;
    unsigned last_received_ = 0;
    double connected_since_ = 0;
    bool was_connected_ = false;
    bool selection_changed_in_picker_ = false;
    bool pick_available_ = false;
    std::string pick_panel_, pick_id_;
    std::string last_mode_panel_;
    std::string last_error_;
    double app_log_clear_time_ = 0;
    std::map<std::string, std::map<std::string, std::string>> exported_hashes_;
    double last_export_scan_ = 0;
    BlockHashProvider hash_provider_;
};

LiveBridge& GlobalLiveBridge();

}  // namespace imforge::builder
