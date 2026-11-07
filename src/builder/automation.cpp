#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS
#endif

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "automation.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "editor_context.h"
#include "export.h"
#include "live_bridge.h"
#include "platform/platform.h"
#include "png_write.h"

namespace imforge::builder {

namespace fs = std::filesystem;

namespace {

std::string Trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return std::string();
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

std::string ExpandEnv(const std::string& s) {
    std::string out;
    size_t i = 0;
    while (i < s.size()) {
        size_t end = s.compare(i, 2, "${") == 0 ? s.find('}', i + 2) : std::string::npos;
        if (end == std::string::npos) {
            out += s[i++];
            continue;
        }
        const char* v = std::getenv(s.substr(i + 2, end - i - 2).c_str());
        if (v) out += v;
        i = end + 1;
    }
    return out;
}

std::string RestOf(std::istringstream& is) {
    std::string r;
    std::getline(is, r);
    return Trim(r);
}

Node* Locate(EditorContext& ctx, const std::string& spec, std::string& panel_name) {
    std::string id = spec;
    std::string want_panel;
    size_t slash = spec.find('/');
    if (slash != std::string::npos) {
        want_panel = spec.substr(0, slash);
        id = spec.substr(slash + 1);
    }
    if (want_panel.empty() && ctx.active_kind == DocKind::Panel) {
        if (Node* n = ctx.FindActiveNode(id)) {
            panel_name = ctx.active_panel;
            return n;
        }
    }
    for (Panel& p : ctx.project.panels) {
        if (!want_panel.empty() && p.name != want_panel) continue;
        if (!FindNode(p.root, id)) continue;
        panel_name = p.name;
        ctx.SetActivePanel(p.name);
        return FindNode(ctx.FindPanelDoc(panel_name)->root, id);
    }
    return nullptr;
}

bool IsImGuiColName(const std::string& name) {
    for (int i = 0; i < ImGuiCol_COUNT; ++i)
        if (name == ImGui::GetStyleColorName(i)) return true;
    return false;
}

}  // namespace

void AutomationAttachConsole() {
#ifdef _WIN32
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (h != nullptr && h != INVALID_HANDLE_VALUE && GetFileType(h) != FILE_TYPE_UNKNOWN) return;
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        std::freopen("CONOUT$", "w", stdout);
        std::freopen("CONOUT$", "w", stderr);
    }
#endif
}

double Automation::Now() const {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

bool Automation::Load(const std::string& script_path, std::string* err) {
    std::ifstream in(script_path, std::ios::binary);
    if (!in) {
        if (err) *err = "cannot read script " + script_path;
        return false;
    }
    std::string line;
    while (std::getline(in, line)) lines_.push_back(line);
    loaded_ = true;
    std::printf("[script] loaded %s (%d lines)\n", script_path.c_str(), (int)lines_.size());
    std::fflush(stdout);
    return true;
}

void Automation::Ok(EditorContext& ctx, const std::string& msg) {
    double t = start_ < 0 ? 0.0 : Now() - start_;
    std::printf("[script %8.3f] L%-3d %-40s | ok: %s\n", t, line_no_, current_.c_str(), msg.c_str());
    std::fflush(stdout);
    ctx.LogSource(LogLevel::Info, "script", "L%d %s: %s", line_no_, current_.c_str(), msg.c_str());
}

bool Automation::Fail(EditorContext& ctx, const std::string& msg) {
    double t = start_ < 0 ? 0.0 : Now() - start_;
    std::printf("[script %8.3f] L%-3d %-40s | FAIL: %s\n", t, line_no_, current_.c_str(), msg.c_str());
    std::fflush(stdout);
    ctx.LogSource(LogLevel::Error, "script", "L%d %s: FAIL: %s", line_no_, current_.c_str(), msg.c_str());
    Finish(ctx, 1);
    return false;
}

void Automation::Finish(EditorContext& ctx, int code) {
    done_ = true;
    exit_code_ = code;
    std::printf("[script] finished, exit code %d\n", code);
    std::fflush(stdout);
    ctx.LogSource(code == 0 ? LogLevel::Info : LogLevel::Error, "script", "finished, exit code %d", code);
}

void Automation::Step(EditorContext& ctx, Platform& platform) {
    if (!loaded_ || done_) return;
    if (warmup_frames_ > 0) {
        --warmup_frames_;
        return;
    }
    if (start_ < 0) start_ = Now();
    {
        double t = Now();
        if (last_step_ >= 0) {
            double ms = (t - last_step_) * 1000.0;
            frame_sum_ms_ += ms;
            if (ms > frame_max_ms_) frame_max_ms_ = ms;
            ++frame_count_;
        }
        last_step_ = t;
    }
    switch (wait_) {
    case Wait::None: break;
    case Wait::Time:
        if (Now() < wait_until_) return;
        wait_ = Wait::None;
        break;
    case Wait::Client: {
        const LiveClientInfo& c = ctx.live.Client();
        if (ctx.live.IsConnected() && (c.pid != 0 || !c.app.empty())) {
            wait_ = Wait::None;
            Ok(ctx, "client '" + c.app + "' pid " + std::to_string(c.pid) + " backend " + c.backend + " on " + ctx.live.Url());
            break;
        }
        if (Now() >= wait_until_) Fail(ctx, "no paired live client sent its client_info on " + ctx.live.Url() + " (" + ctx.live.StatusText() + ")");
        return;
    }
    case Wait::Capture: {
        int w = 0, h = 0;
        std::vector<unsigned char> px;
        if (!platform.FetchCapture(w, h, px)) {
            if (++capture_frames_ > 30) Fail(ctx, "framebuffer capture did not complete");
            return;
        }
        wait_ = Wait::None;
        std::error_code ec;
        fs::path p = fs::absolute(fs::u8path(capture_path_), ec);
        if (p.has_parent_path()) fs::create_directories(p.parent_path(), ec);
        if (!png::WriteRGBA(p.string(), px.data(), w, h)) {
            Fail(ctx, "cannot write " + p.string());
            return;
        }
        Ok(ctx, "wrote " + std::to_string(w) + "x" + std::to_string(h) + " " + p.string());
        break;
    }
    }
    while (pc_ < lines_.size()) {
        std::string line = Trim(lines_[pc_++]);
        line_no_ = (int)pc_;
        if (line.empty() || line[0] == '#') continue;
        current_ = line;
        Execute(ctx, platform, line);
        return;
    }
    current_.clear();
    Finish(ctx, 0);
}

bool Automation::Execute(EditorContext& ctx, Platform& platform, const std::string& line) {
    std::istringstream is(line);
    std::string cmd;
    is >> cmd;
    char buf[256];

    if (cmd == "wait") {
        double s = -1;
        if (!(is >> s) || s < 0) return Fail(ctx, "usage: wait <seconds>");
        wait_ = Wait::Time;
        wait_until_ = Now() + s;
        std::snprintf(buf, sizeof(buf), "waiting %.2f s", s);
        Ok(ctx, buf);
        return true;
    }
    if (cmd == "wait_client") {
        double s = 10;
        is >> s;
        if (!ctx.live.IsRunning()) {
            std::string err;
            if (!GlobalLiveBridge().Start(ctx, &err)) return Fail(ctx, "cannot start the live server: " + err);
        }
        wait_ = Wait::Client;
        wait_until_ = Now() + s;
        std::snprintf(buf, sizeof(buf), "waiting up to %.1f s for a client on %s", s, ctx.live.Url().c_str());
        Ok(ctx, buf);
        return true;
    }
    if (cmd == "select") {
        std::string spec, panel;
        is >> spec;
        Node* n = Locate(ctx, spec, panel);
        if (!n) return Fail(ctx, "node '" + spec + "' not found");
        ctx.Select(n->id);
        Ok(ctx, "selected " + panel + "/" + n->id + " (" + n->type + ")");
        return true;
    }
    if (cmd == "set" || cmd == "bind") {
        std::string spec, key, panel;
        is >> spec >> key;
        std::string value = ExpandEnv(RestOf(is));
        if (spec.empty() || key.empty() || value.empty())
            return Fail(ctx, cmd == "set" ? "usage: set <nodeId> <prop> <json>" : "usage: bind <nodeId> <slot> <path>");
        Json v;
        if (cmd == "set") {
            JsonError je;
            if (!ParseJson(value, v, &je)) return Fail(ctx, "invalid JSON '" + value + "': " + je.message);
        } else {
            v = Json(value);
        }
        if (!Locate(ctx, spec, panel)) return Fail(ctx, "node '" + spec + "' not found");
        std::string id = spec.substr(spec.find('/') == std::string::npos ? 0 : spec.find('/') + 1);
        std::string label = "Script: " + cmd + " " + key;
        ctx.BeginEdit(label.c_str(), DocKind::Panel, panel);
        Node* n = FindNode(ctx.FindPanelDoc(panel)->root, id);
        if (cmd == "set") {
            n->SetProp(key.c_str(), v);
        } else {
            bool found = false;
            for (auto& b : n->bind)
                if (b.first == key) {
                    b.second = PropValue(v);
                    found = true;
                }
            if (!found) n->bind.emplace_back(key, PropValue(v));
        }
        ctx.EndEdit();
        Ok(ctx, panel + "/" + id + (cmd == "set" ? "." : " bind ") + key + " = " + DumpJson(v, 0));
        return true;
    }
    if (cmd == "theme_color") {
        std::string name, hex;
        is >> name >> hex;
        bool ok = false;
        Vec4 c = ParseColor(hex, &ok);
        if (!ok) return Fail(ctx, "invalid color '" + hex + "' (expected #rrggbb or #rrggbbaa)");
        if (!IsImGuiColName(name)) return Fail(ctx, "'" + name + "' is not an ImGuiCol name");
        Theme* theme = ctx.ActiveTheme();
        if (!theme) return Fail(ctx, "project has no theme");
        std::string theme_name = theme->name;
        ctx.EditProject("Script: theme color", [&] {
            Theme* t = ctx.ActiveTheme();
            for (auto& kv : t->colors)
                if (kv.first == name) {
                    kv.second = c;
                    return;
                }
            t->colors.emplace_back(name, c);
        });
        Ok(ctx, "theme " + theme_name + ": " + name + " = " + hex);
        return true;
    }
    if (cmd == "push") {
        size_t before = ctx.live.Stats().bytes_sent;
        if (!GlobalLiveBridge().PushAll(ctx)) return Fail(ctx, "push failed: " + ctx.status_message);
        Ok(ctx, "pushed " + std::to_string(ctx.live.Stats().bytes_sent - before) + " bytes");
        return true;
    }
    if (cmd == "picker") {
        std::string v;
        is >> v;
        if (v != "on" && v != "off") return Fail(ctx, "usage: picker on|off");
        GlobalLiveBridge().SetPicker(ctx, v == "on");
        Ok(ctx, std::string("picker ") + (ctx.PickerMode() ? "on" : "off") + (ctx.live.IsConnected() ? " (sent to client)" : " (no client)"));
        return true;
    }
    if (cmd == "screenshot") {
        std::string path = ExpandEnv(RestOf(is));
        if (path.empty()) return Fail(ctx, "usage: screenshot <path.png>");
        if (!platform.RequestCapture()) return Fail(ctx, std::string("framebuffer capture not supported by ") + platform.Name());
        capture_path_ = path;
        capture_frames_ = 0;
        wait_ = Wait::Capture;
        return true;
    }
    if (cmd == "export") {
        std::string dir = ExpandEnv(RestOf(is));
        if (dir.empty()) return Fail(ctx, "usage: export <dir>");
        if (!ctx.has_project) return Fail(ctx, "no project open");
        std::error_code ec;
        fs::path abs = fs::absolute(fs::u8path(dir), ec);
        fs::create_directories(abs, ec);
        std::string saved = ctx.project.codegen.output_dir;
        ctx.project.codegen.output_dir = abs.generic_string();
        std::string err;
        bool ok = ExportProject(ctx, &err, true);
        ctx.project.codegen.output_dir = saved;
        if (!ok) return Fail(ctx, "export failed: " + err);
        int files = 0;
        for (fs::recursive_directory_iterator it(abs, ec), end; !ec && it != end; it.increment(ec))
            if (it->is_regular_file(ec)) ++files;
        if (files == 0) return Fail(ctx, "export wrote no files to " + abs.string());
        Ok(ctx, std::to_string(files) + " files in " + abs.string() + ", " + std::to_string(ctx.conflicts.size()) + " conflicts");
        return true;
    }
    if (cmd == "log") {
        Ok(ctx, ExpandEnv(RestOf(is)));
        return true;
    }
    if (cmd == "quit") {
        int code = 0;
        is >> code;
        Ok(ctx, "quit " + std::to_string(code));
        Finish(ctx, code);
        return true;
    }
    if (cmd == "assert_client_fps_gt") {
        double n = 0;
        if (!(is >> n)) return Fail(ctx, "usage: assert_client_fps_gt <n>");
        if (!ctx.live.IsConnected()) return Fail(ctx, "no live client connected");
        float fps = ctx.live.Client().fps;
        std::snprintf(buf, sizeof(buf), "client fps %.1f (> %.1f)", fps, n);
        if (fps > n) Ok(ctx, buf);
        else return Fail(ctx, buf);
        return true;
    }
    if (cmd == "add") {
        std::string parent, type;
        int count = 1;
        is >> parent >> type >> count;
        if (parent.empty() || type.empty() || count < 1) return Fail(ctx, "usage: add <parentId> <type> [count]");
        std::string panel;
        if (!Locate(ctx, parent, panel)) return Fail(ctx, "node '" + parent + "' not found");
        std::string pid = parent.substr(parent.find('/') == std::string::npos ? 0 : parent.find('/') + 1);
        ctx.BeginEdit("Script: add");
        int added = 0;
        for (int i = 0; i < count; ++i) {
            Node* n = ctx.InsertNode(type, pid, -1, "Script: add");
            if (!n) break;
            ++added;
        }
        ctx.EndEdit();
        if (added != count) return Fail(ctx, "added only " + std::to_string(added) + " of " + std::to_string(count) + " " + type);
        Ok(ctx, "added " + std::to_string(added) + " " + type + " under " + pid);
        return true;
    }
    if (cmd == "undo" || cmd == "redo") {
        int n = 1;
        is >> n;
        for (int i = 0; i < n; ++i) {
            if (cmd == "undo") ctx.Undo();
            else ctx.Redo();
        }
        Ok(ctx, cmd + " x" + std::to_string(n) + (ctx.dirty ? " (dirty)" : ""));
        return true;
    }
    if (cmd == "delete" || cmd == "duplicate") {
        bool ok = cmd == "delete" ? ctx.DeleteSelection() : ctx.DuplicateSelection();
        if (!ok) return Fail(ctx, cmd + ": nothing selected");
        Ok(ctx, cmd);
        return true;
    }
    if (cmd == "assert_node_count") {
        int want = 0;
        if (!(is >> want)) return Fail(ctx, "usage: assert_node_count <n>");
        Node* root = ctx.ActiveRoot();
        if (!root) return Fail(ctx, "no active document");
        int have = 0;
        Walk(*root, [&](const Node&) { ++have; });
        std::snprintf(buf, sizeof(buf), "%d nodes (expected %d)", have, want);
        if (have != want) return Fail(ctx, buf);
        Ok(ctx, buf);
        return true;
    }
    if (cmd == "frame_reset") {
        frame_sum_ms_ = frame_max_ms_ = 0;
        frame_count_ = 0;
        Ok(ctx, "frame statistics reset");
        return true;
    }
    if (cmd == "assert_frame_ms_lt") {
        double limit = 0;
        if (!(is >> limit)) return Fail(ctx, "usage: assert_frame_ms_lt <n>");
        if (frame_count_ < 5) return Fail(ctx, "need at least 5 frames since frame_reset (have " + std::to_string(frame_count_) + "); add a wait");
        double avg = frame_sum_ms_ / frame_count_;
        std::snprintf(buf, sizeof(buf), "avg frame %.2f ms, max %.2f ms over %d frames (limit %.1f)", avg, frame_max_ms_, frame_count_, limit);
        if (avg >= limit) return Fail(ctx, buf);
        Ok(ctx, buf);
        return true;
    }
    if (cmd == "assert_no_errors") {
        if (ctx.validation_pending) ctx.Revalidate();
        int errors = 0;
        std::string first;
        for (const Diagnostic& d : ctx.diagnostics) {
            if (d.level != Diagnostic::Level::Error) continue;
            if (errors++ < 3) first += (first.empty() ? "" : "; ") + d.panel + "/" + d.node_id + ": " + d.message;
        }
        std::snprintf(buf, sizeof(buf), "%d diagnostics, %d errors, %u error log entries", (int)ctx.diagnostics.size(), errors,
                      ctx.log.Count(LogLevel::Error));
        if (errors > 0) return Fail(ctx, std::string(buf) + ": " + first);
        Ok(ctx, buf);
        return true;
    }
    if (cmd == "assert_paired") {
        LivePairingInfo pi = ctx.live.Pairing();
        if (!ctx.live.IsConnected() || !pi.paired) return Fail(ctx, "live client is not paired (" + LivePairingStateText(pi) + ")");
        Ok(ctx, LivePairingStateText(pi) + ", " + std::to_string(pi.pairings) + " pairing(s), file " + pi.file);
        return true;
    }
    if (cmd == "pairing_rotate") {
        int before = ctx.live.Pairing().generation;
        if (!ctx.live.RotatePairingCode()) return Fail(ctx, "live server is not running");
        if (ctx.live.Pairing().generation == before) return Fail(ctx, "pairing code did not change");
        Ok(ctx, "pairing code rotated (#" + std::to_string(ctx.live.Pairing().generation) + ")");
        return true;
    }
    if (cmd == "open_panel") {
        std::string name = RestOf(is);
        if (name.empty()) return Fail(ctx, "usage: open_panel <name>");
        if (!ctx.OpenPanel(name.c_str(), true)) return Fail(ctx, "no panel named '" + name + "'");
        Ok(ctx, "opened " + name);
        return true;
    }
    return Fail(ctx, "unknown command '" + cmd + "'");
}

}  // namespace imforge::builder
