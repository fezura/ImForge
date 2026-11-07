#include "export.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>

#include "builder_fonts.h"
#include "editor_context.h"
#include "panels/conflicts_state.h"
#include "project_io.h"

namespace imforge::builder {

namespace fs = std::filesystem;

namespace {

const char* const kDiagTag = "[export] ";

bool Fail(std::string* err, const std::string& msg) {
    if (err) *err = msg;
    return false;
}

std::string RelativeTo(const std::string& abs, const std::string& base) {
    std::error_code ec;
    fs::path rel = fs::path(abs).lexically_relative(fs::path(base));
    std::string s = rel.generic_string();
    if (s.empty() || s.compare(0, 2, "..") == 0) return fs::path(abs).filename().generic_string();
    return s;
}

void ForwardDiagnostics(EditorContext& ctx, const std::vector<Diagnostic>& diags) {
    ctx.diagnostics.erase(std::remove_if(ctx.diagnostics.begin(), ctx.diagnostics.end(),
                                         [](const Diagnostic& d) {
                                             return d.message.compare(0, std::strlen(kDiagTag), kDiagTag) == 0;
                                         }),
                          ctx.diagnostics.end());
    for (Diagnostic d : diags) {
        LogLevel lvl = d.level == Diagnostic::Level::Error     ? LogLevel::Error
                       : d.level == Diagnostic::Level::Warning ? LogLevel::Warning
                                                               : LogLevel::Info;
        ctx.LogSource(lvl, "export", "%s%s%s", d.panel.empty() ? "" : (d.panel + ": ").c_str(),
                      d.node_id.empty() ? "" : (d.node_id + ": ").c_str(), d.message.c_str());
        d.message = kDiagTag + d.message;
        ctx.diagnostics.push_back(std::move(d));
    }
}

void ApplyPanelUpdate(EditorContext& ctx, const std::string& name, const Panel& updated, const char* label) {
    DocKind prev_kind = ctx.active_kind;
    std::string prev = ctx.active_panel;
    bool switched = !(prev_kind == DocKind::Panel && prev == name);
    if (switched) ctx.SetActivePanel(name);
    ctx.BeginEdit(label, DocKind::Panel, name);
    if (Panel* dst = ctx.FindPanelDoc(name)) {
        dst->root = updated.root;
        dst->params = updated.params;
        dst->state = updated.state;
    }
    ctx.EndEdit();
    if (switched) {
        if (prev_kind == DocKind::Panel) ctx.SetActivePanel(prev);
        else if (prev_kind == DocKind::Component) ctx.SetActiveComponent(prev);
    }
}

bool LockNode(EditorContext& ctx, const std::string& panel, const std::string& node_id) {
    Panel* p = ctx.FindPanelDoc(panel);
    if (!p) return false;
    Node* n = FindNode(p->root, node_id);
    if (!n) return false;
    if (n->locked) return true;
    Panel copy = *p;
    FindNode(copy.root, node_id)->locked = true;
    ApplyPanelUpdate(ctx, panel, copy, "Lock node (keep my code)");
    return true;
}

void MirrorConflicts(EditorContext& ctx) {
    ctx.conflicts.clear();
    for (const ConflictItem& c : GetConflictsState().conflicts)
        if (!c.resolved) ctx.conflicts.push_back({c.panel, c.node_id, c.reason, c.user_code, c.generated_code});
}

std::string LocateShippedFile(const std::string& rel) {
    std::error_code ec;
    std::vector<fs::path> candidates;
#ifdef IMFORGE_SOURCE_DIR
    candidates.push_back(fs::path(IMFORGE_SOURCE_DIR) / "include" / "imforge" / rel);
    candidates.push_back(fs::path(IMFORGE_SOURCE_DIR) / "packs" / "essentials" / rel);
#endif
    fs::path exe = ExeDir();
    for (const char* base : {"share/imforge", "../share/imforge", "../../share/imforge"}) {
        candidates.push_back(exe / base / rel);
        candidates.push_back(exe / base / "include" / "imforge" / rel);
        candidates.push_back(exe / base / "packs" / "essentials" / rel);
    }
    for (const fs::path& p : candidates)
        if (fs::is_regular_file(p, ec)) return p.lexically_normal().generic_string();
    return std::string();
}

struct OutFile {
    std::string rel, abs, content, existing;
    bool existed = false, merged = false;
};

void AddRuntimeCopies(const Project& project, const std::string& out_dir, std::vector<OutFile>& outs,
                      std::vector<Diagnostic>& diags) {
    std::vector<std::pair<std::string, std::string>> wanted;
    bool anim = project.Feature("animations", true), dfx = project.Feature("drawlistFx", true),
         sfx = project.Feature("shaderFx", false);
    if (anim || dfx || sfx) {
        const std::string& rh = project.codegen.runtime_header;
        bool bare = !rh.empty() && rh.find('/') == std::string::npos && rh.find('\\') == std::string::npos;
        if (bare) {
            wanted.push_back({"imb_runtime.h", rh});
            wanted.push_back({"imb_anim.h", "imb_anim.h"});
            wanted.push_back({"imb_effects.h", "imb_effects.h"});
            if (sfx) {
                wanted.push_back({"fx/imb_fx.h", "fx/imb_fx.h"});
                static const char* renderers[] = {"dx9", "dx11", "dx12", "gl3", "vulkan"};
                for (const char* r : renderers) {
                    if (project.renderer != r) continue;
                    wanted.push_back({"fx/imb_fx_" + project.renderer + ".h", "fx/imb_fx_" + project.renderer + ".h"});
                    if (project.renderer == "vulkan") wanted.push_back({"fx/imb_fx_vulkan_shaders.h", "fx/imb_fx_vulkan_shaders.h"});
                }
            }
        } else {
            Diagnostic d;
            d.level = Diagnostic::Level::Info;
            d.message = "runtimeHeader \"" + rh + "\" is a path: imb_runtime.h is not copied next to the generated code";
            diags.push_back(d);
        }
    }
    bool ess = false;
    for (const std::string& lib : project.libraries) ess = ess || lib.find("essentials") != std::string::npos;
    if (ess) {
        for (const char* n : {"imb_essentials.h", "imb_ess_core.h", "imb_ess_controls.h", "imb_ess_layout.h", "imb_ess_toast.h"})
            wanted.push_back({n, n});
    }
    for (const auto& w : wanted) {
        std::string src = LocateShippedFile(w.first);
        std::string text;
        if (src.empty() || !ReadTextFile(src, text)) {
            Diagnostic d;
            d.level = Diagnostic::Level::Warning;
            d.message = "shipped header " + w.first + " not found (set IMFORGE_SOURCE_DIR or install share/imforge)";
            diags.push_back(d);
            continue;
        }
        OutFile o;
        o.rel = w.second;
        o.abs = SafeJoin(out_dir, w.second);
        if (o.abs.empty()) continue;
        o.content = text;
        o.existed = ReadTextFile(o.abs, o.existing);
        outs.push_back(std::move(o));
    }
}

bool WriteManifest(const std::string& out_dir, const std::string& project_name,
                   const std::map<std::string, std::string>& files, std::string* err) {
    std::vector<std::pair<std::string, std::string>> entries;
    for (const auto& kv : files) entries.push_back({RelativeTo(kv.first, out_dir), Fnv1a32HexOf(kv.second)});
    std::sort(entries.begin(), entries.end());
    Json arr = Json::Array();
    for (const auto& e : entries) {
        Json o = Json::Object();
        o.Set("hash", Json(e.second));
        o.Set("path", Json(e.first));
        arr.Push(std::move(o));
    }
    Json root = Json::Object();
    root.Set("schemaVersion", Json(1));
    root.Set("kind", Json("export"));
    root.Set("project", Json(project_name));
    root.Set("files", std::move(arr));
    return WriteFileAtomic(JoinPath(out_dir, "imforge_export.json"), DumpJson(root) + "\n", err);
}

#if IMFORGE_EXPORT_HAS_CODEGEN
std::vector<OutFile> CollectOutputs(const codegen::Result& all, const std::string& out_dir, std::vector<Diagnostic>& diags) {
    std::vector<OutFile> outs;
    for (const codegen::FileOut& f : all.files) {
        OutFile o;
        o.rel = f.path;
        o.abs = SafeJoin(out_dir, f.path);
        if (o.abs.empty()) {
            Diagnostic d;
            d.level = Diagnostic::Level::Error;
            d.message = "skipped output file with an unsafe path: " + SanitizeFileStem(f.path.substr(0, 80));
            diags.push_back(d);
            continue;
        }
        o.content = f.content;
        o.existed = ReadTextFile(o.abs, o.existing);
        outs.push_back(std::move(o));
    }
    return outs;
}

#if IMFORGE_EXPORT_HAS_ROUNDTRIP
int IndexOfFile(const codegen::Result& r, const std::string& rel) {
    for (size_t i = 0; i < r.files.size(); ++i)
        if (r.files[i].path == rel || fs::path(r.files[i].path).filename() == fs::path(rel).filename()) return (int)i;
    return -1;
}

OutFile* FindOut(std::vector<OutFile>& outs, const std::string& rel) {
    for (OutFile& o : outs)
        if (o.rel == rel) return &o;
    for (OutFile& o : outs)
        if (fs::path(o.rel).filename() == fs::path(rel).filename()) return &o;
    return nullptr;
}

const OutFile* FileOfBlock(const std::vector<OutFile>& outs, const std::vector<size_t>& indices, const std::string& node_id) {
    for (size_t i : indices)
        if (outs[i].content.find("id=" + node_id + " ") != std::string::npos ||
            outs[i].content.find("id=" + node_id + "\n") != std::string::npos)
            return &outs[i];
    return indices.empty() ? nullptr : &outs[indices.front()];
}

size_t BeginMarkerHashPos(const std::string& text, const std::string& node_id) {
    std::string key = "@imb:begin id=" + node_id + " h=";
    size_t p = text.find(key);
    return p == std::string::npos ? p : p + key.size();
}

// Merge rewrites every h= to the hash of the body it kept, so a conflicted block would look unedited to the next
// merge (pass 2 or the next export) and the user's code would get thrown away. Put the old mismatching hash back
// so it stays a conflict until it's resolved in the Conflicts panel (ApplyResolution rehashes).
void KeepConflictsEdited(std::string& merged, const std::string& original, const std::vector<roundtrip::Conflict>& conflicts) {
    for (const roundtrip::Conflict& c : conflicts) {
        size_t m = BeginMarkerHashPos(merged, c.node_id);
        if (m == std::string::npos) continue;
        size_t o = BeginMarkerHashPos(original, c.node_id);
        std::string h = o == std::string::npos ? std::string() : original.substr(o, 8);
        if (h.size() != 8 || merged.compare(m, 8, h) == 0) h = merged.compare(m, 8, "00000000") == 0 ? "ffffffff" : "00000000";
        merged.replace(m, 8, h);
    }
}

// Two passes per panel. If the first merge pulled literal edits back into the panel we apply them (undoable),
// regenerate and merge the pass-1 output again. Its hashes match the kept bodies by then, so the regenerated code
// wins and every copy of an edited literal agrees. User regions carry over and pass-1 conflicts stay conflicts.
// Only the final merge reports conflicts. See src/roundtrip/roundtrip.h.
void MergeOutputs(EditorContext& ctx, const codegen::Options& opts, std::vector<OutFile>& outs, std::vector<Diagnostic>& diags) {
    ConflictsState& cs = GetConflictsState();
    std::map<std::string, std::vector<size_t>> by_panel;
    for (size_t i = 0; i < outs.size(); ++i) {
        OutFile& o = outs[i];
        if (!o.existed || o.existing == o.content) continue;
        std::string pname = FileMarkerValue(o.content, "panel");
        if (!pname.empty() && ctx.FindPanelDoc(pname)) {
            by_panel[pname].push_back(i);
            continue;
        }
        roundtrip::ParsedFile parsed;
        Error perr;
        if (roundtrip::ParseGenerated(o.existing, parsed, &perr)) {
            std::vector<std::string> carried;
            o.content = roundtrip::CarryUserRegions(o.content, parsed, &carried);
            for (const std::string& id : carried) cs.report.push_back(o.rel + ": carried user region " + id);
        }
    }
    for (auto& kv : by_panel) {
        const std::string& pname = kv.first;
        std::vector<std::pair<std::string, std::string>> existing;
        for (size_t i : kv.second) existing.emplace_back(outs[i].rel, outs[i].existing);
        auto merge_once = [&](const std::vector<std::pair<std::string, std::string>>& input,
                              std::vector<std::pair<std::string, std::string>>& merged, Panel& work) {
            Panel* src = ctx.FindPanelDoc(pname);
            work = *src;
            merged.clear();
            return roundtrip::MergeFiles(work, input, codegen::GeneratePanel(ctx.project, *src, opts), merged);
        };
        Panel work;
        std::vector<std::pair<std::string, std::string>> merged;
        roundtrip::MergeReport mr = merge_once(existing, merged, work);
        std::vector<Diagnostic> all_diags = mr.diagnostics;
        if (!mr.updated_nodes.empty()) {
            for (const std::string& id : mr.updated_nodes) {
                cs.updated_nodes.push_back(pname + "/" + id);
                cs.report.push_back(pname + "/" + id + ": literal edits pulled from code");
            }
            ApplyPanelUpdate(ctx, pname, work, "Pull literal edits from code");
            ctx.Log(LogLevel::Info, "export: %d node(s) of panel %s updated from hand-edited code, regenerating",
                    (int)mr.updated_nodes.size(), pname.c_str());
            std::vector<std::pair<std::string, std::string>> pass2_input;
            for (const auto& e : existing) {
                std::string text = e.second;
                for (const auto& m : merged)
                    if (m.first == e.first || fs::path(m.first).filename() == fs::path(e.first).filename()) text = m.second;
                KeepConflictsEdited(text, e.second, mr.conflicts);
                pass2_input.emplace_back(e.first, std::move(text));
            }
            roundtrip::MergeReport again = merge_once(pass2_input, merged, work);
            all_diags.insert(all_diags.end(), again.diagnostics.begin(), again.diagnostics.end());
            mr.conflicts = again.conflicts;
            if (!again.updated_nodes.empty()) {
                Diagnostic d;
                d.level = Diagnostic::Level::Warning;
                d.panel = pname;
                d.message = "second merge pass still reports literal edits (" + std::to_string(again.updated_nodes.size()) +
                            " node(s)); the generated code wins for those";
                all_diags.push_back(d);
            }
        }
        for (Diagnostic d : all_diags) {
            if (d.panel.empty()) d.panel = pname;
            diags.push_back(std::move(d));
        }
        for (const auto& m : merged) {
            OutFile* o = FindOut(outs, m.first);
            if (!o) continue;
            bool was_existing = false;
            for (size_t i : kv.second) was_existing = was_existing || &outs[i] == o;
            if (!was_existing) continue;
            std::string text = m.second;
            KeepConflictsEdited(text, o->existing, mr.conflicts);
            o->merged = o->content != text || o->merged;
            o->content = std::move(text);
        }
        for (const roundtrip::Conflict& c : mr.conflicts) {
            const OutFile* f = FileOfBlock(outs, kv.second, c.node_id);
            std::string abs = f ? f->abs : std::string();
            cs.conflicts.push_back({pname, abs, c.node_id, c.reason, c.user_code, c.generated_code, false, false});
            cs.report.push_back((f ? f->rel : pname) + ": conflict in " + c.node_id + " (" + c.reason + ")");
        }
    }
}
#endif
#endif

}  // namespace

#if IMFORGE_EXPORT_HAS_CODEGEN
codegen::Options MakeCodegenOptions(const Project& project) { return codegen::OptionsFromProject(project); }
#endif

bool CodegenAvailable() { return IMFORGE_EXPORT_HAS_CODEGEN != 0; }
bool RoundtripAvailable() { return IMFORGE_EXPORT_HAS_ROUNDTRIP != 0; }

std::string ExportOutputDir(const Project& project) {
    std::string rel = project.codegen.output_dir.empty() ? "generated" : project.codegen.output_dir;
    return NormalizePath(JoinPath(project.dir, rel));
}

std::string FileMarkerValue(const std::string& content, const char* key) {
    size_t p = content.find("@imb:file");
    if (p == std::string::npos || p > 4096) return std::string();
    size_t eol = content.find('\n', p);
    std::string line = content.substr(p, eol == std::string::npos ? std::string::npos : eol - p);
    std::string k = std::string(key) + "=";
    size_t q = line.find(k);
    if (q == std::string::npos) return std::string();
    q += k.size();
    size_t e = q;
    while (e < line.size() && !std::isspace((unsigned char)line[e])) ++e;
    return line.substr(q, e - q);
}

bool HasFileMarker(const std::string& content) {
    size_t p = content.find("@imb:file");
    return p != std::string::npos && p < 4096;
}

std::string Fnv1a32HexOf(const std::string& text) {
    unsigned h = 2166136261u;
    for (unsigned char c : text) {
        h ^= c;
        h *= 16777619u;
    }
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%08x", h);
    return buf;
}

bool WriteFileAtomic(const std::string& path, const std::string& content, std::string* err) {
    std::error_code ec;
    fs::path p(path);
    if (p.has_parent_path()) fs::create_directories(p.parent_path(), ec);
    std::string tmp = path + ".imforge.tmp";
    fs::remove(tmp, ec);
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return Fail(err, "cannot write " + tmp);
        out.write(content.data(), (std::streamsize)content.size());
        out.close();
        if (!out) return Fail(err, "write failed for " + tmp);
    }
    fs::rename(tmp, p, ec);
    if (ec) {
        ec.clear();
        fs::remove(p, ec);
        ec.clear();
        fs::rename(tmp, p, ec);
        if (ec) {
            fs::remove(tmp, ec);
            return Fail(err, "cannot replace " + path);
        }
    }
    return true;
}

std::vector<ExportPreviewEntry> PreviewExport(EditorContext& ctx, std::vector<Diagnostic>* diagnostics) {
    std::vector<ExportPreviewEntry> out;
    if (!ctx.has_project) return out;
#if IMFORGE_EXPORT_HAS_CODEGEN
    std::string out_dir = ExportOutputDir(ctx.project);
    codegen::Result all = codegen::GenerateAll(ctx.project, MakeCodegenOptions(ctx.project));
    std::vector<Diagnostic> diags = all.diagnostics;
    std::vector<OutFile> outs = CollectOutputs(all, out_dir, diags);
    AddRuntimeCopies(ctx.project, out_dir, outs, diags);
    for (const OutFile& o : outs) {
        ExportPreviewEntry e;
        e.rel = o.rel;
        e.abs = o.abs;
        e.exists = o.existed;
        e.changed = !o.existed || o.existing != o.content;
        e.unmarked = o.existed && ctx.project.codegen.markers && HasFileMarker(o.content) && !HasFileMarker(o.existing);
        out.push_back(std::move(e));
    }
    if (diagnostics) *diagnostics = std::move(diags);
#else
    (void)diagnostics;
#endif
    return out;
}

bool ExportProject(EditorContext& ctx, std::string* err, bool overwrite_unmarked) {
    if (!ctx.has_project || ctx.project.dir.empty()) return Fail(err, "no project open");
#if !IMFORGE_EXPORT_HAS_CODEGEN
    (void)overwrite_unmarked;
    return Fail(err, "code generation is not available in this build (src/codegen not linked)");
#else
    ConflictsState& cs = GetConflictsState();
    cs.Clear();
    const std::string out_dir = ExportOutputDir(ctx.project);
    if (!OutputDirAllowed(ctx.project, out_dir))
        return Fail(err, "the output folder " + out_dir + " is outside the project folder; confirm it with \"Allow this folder\" in the export dialog");
    cs.output_dir = out_dir;
    codegen::Options opts = MakeCodegenOptions(ctx.project);
    codegen::Result all = codegen::GenerateAll(ctx.project, opts);
    std::vector<Diagnostic> diags = all.diagnostics;
    std::vector<OutFile> outs = CollectOutputs(all, out_dir, diags);
    if (ctx.project.codegen.markers) {
        std::vector<std::string> unmarked;
        for (const OutFile& o : outs)
            if (o.existed && HasFileMarker(o.content) && !HasFileMarker(o.existing)) unmarked.push_back(o.rel);
        if (!unmarked.empty() && !overwrite_unmarked) {
            ForwardDiagnostics(ctx, diags);
            ctx.Log(LogLevel::Warning, "export paused: %d file(s) in %s carry no @imb:file marker, confirmation required",
                    (int)unmarked.size(), out_dir.c_str());
            RequestOverwriteConfirm(ctx, unmarked);
            return true;
        }
    }
#if IMFORGE_EXPORT_HAS_ROUNDTRIP
    if (ctx.project.codegen.markers) MergeOutputs(ctx, opts, outs, diags);
#else
    if (ctx.project.codegen.markers) {
        bool any_existing = false;
        for (const OutFile& o : outs) any_existing = any_existing || (o.existed && o.existing != o.content);
        if (any_existing) {
            Diagnostic d;
            d.level = Diagnostic::Level::Warning;
            d.message = "round-trip is not available in this build: existing files are overwritten without merging";
            diags.push_back(d);
        }
    }
#endif
    AddRuntimeCopies(ctx.project, out_dir, outs, diags);
    std::vector<std::string> written;
    int unchanged = 0;
    std::map<std::string, std::string> manifest_files;
    for (const OutFile& o : outs) {
        manifest_files[o.abs] = o.content;
        cs.merged_files[o.abs] = o.content;
        if (o.existed && o.existing == o.content) {
            ++unchanged;
            continue;
        }
        if (!WriteFileAtomic(o.abs, o.content, err)) {
            ForwardDiagnostics(ctx, diags);
            return false;
        }
        written.push_back(o.rel);
        cs.report.push_back(o.rel + (o.merged ? ": written (merged)" : (o.existed ? ": written (updated)" : ": written (new)")));
    }
    if (!WriteManifest(out_dir, ctx.project.name, manifest_files, err)) {
        ForwardDiagnostics(ctx, diags);
        return false;
    }
    for (const std::string& id : cs.updated_nodes) ctx.Log(LogLevel::Info, "export: updated node %s from hand-edited code", id.c_str());
    ForwardDiagnostics(ctx, diags);
    MirrorConflicts(ctx);
    cs.Touch();
    ctx.Log(LogLevel::Info, "exported %d file(s) to %s (%d unchanged, %d conflict(s))", (int)written.size(),
            out_dir.c_str(), unchanged, (int)cs.conflicts.size());
    ctx.SetStatus("Exported " + std::to_string(written.size()) + " file(s)" +
                  (cs.conflicts.empty() ? "" : ", " + std::to_string(cs.conflicts.size()) + " conflict(s)"));
    if (!cs.conflicts.empty()) ctx.OpenPanel("Conflicts");
    return true;
#endif
}

bool ImportFromCode(EditorContext& ctx, const std::string& cpp_path, std::string* err) {
    if (!ctx.has_project) return Fail(err, "no project open");
#if !(IMFORGE_EXPORT_HAS_CODEGEN && IMFORGE_EXPORT_HAS_ROUNDTRIP)
    return Fail(err, "importing hand-edited code needs codegen and roundtrip, which are not available in this build (" + cpp_path + ")");
#else
    std::string text;
    if (!ReadTextFile(cpp_path, text)) return Fail(err, "cannot read " + cpp_path);
    roundtrip::ParsedFile parsed;
    Error perr;
    if (!roundtrip::ParseGenerated(text, parsed, &perr))
        return Fail(err, cpp_path + ": not a generated file (" + perr.message + ")");
    std::string pname = parsed.panel.empty() ? FileMarkerValue(text, "panel") : parsed.panel;
    if (pname.empty()) pname = FileStem(cpp_path);
    Panel* src = ctx.FindPanelDoc(pname);
    if (!src) return Fail(err, "the project has no panel named \"" + pname + "\"");
    codegen::Options opts = MakeCodegenOptions(ctx.project);
    codegen::Result fresh = codegen::GeneratePanel(ctx.project, *src, opts);
    int idx = roundtrip::FindFreshFile(parsed, fresh, pname);
    if (idx < 0) idx = IndexOfFile(fresh, fs::path(cpp_path).filename().generic_string());
    if (idx < 0) return Fail(err, "no generated counterpart for " + cpp_path);
    Panel work = *src;
    std::string merged;
    roundtrip::MergeReport mr = roundtrip::Merge(work, text, fresh, (size_t)idx, merged);
    ConflictsState& cs = GetConflictsState();
    std::string abs = NormalizePath(cpp_path);
    cs.merged_files[abs] = merged.empty() ? text : merged;
    cs.conflicts.erase(std::remove_if(cs.conflicts.begin(), cs.conflicts.end(),
                                      [&](const ConflictItem& c) { return c.file == abs; }),
                       cs.conflicts.end());
    for (const roundtrip::Conflict& c : mr.conflicts) {
        cs.conflicts.push_back({pname, abs, c.node_id, c.reason, c.user_code, c.generated_code, false, false});
        cs.report.push_back(fs::path(cpp_path).filename().generic_string() + ": conflict in " + c.node_id + " (" + c.reason + ")");
    }
    for (const std::string& id : mr.updated_nodes) {
        cs.updated_nodes.push_back(pname + "/" + id);
        cs.report.push_back(fs::path(cpp_path).filename().generic_string() + ": literal edits pulled into " + pname + "/" + id);
        ctx.Log(LogLevel::Info, "import: updated node %s/%s from %s", pname.c_str(), id.c_str(), cpp_path.c_str());
    }
    std::vector<Diagnostic> diags;
    for (Diagnostic d : mr.diagnostics) {
        if (d.panel.empty()) d.panel = pname;
        diags.push_back(std::move(d));
    }
    if (!mr.updated_nodes.empty()) ApplyPanelUpdate(ctx, pname, work, "Import edits from code");
    ForwardDiagnostics(ctx, diags);
    MirrorConflicts(ctx);
    cs.Touch();
    ctx.Log(LogLevel::Info, "imported %s into panel %s: %d node(s) updated, %d conflict(s)", cpp_path.c_str(), pname.c_str(),
            (int)mr.updated_nodes.size(), (int)mr.conflicts.size());
    ctx.SetStatus("Imported " + fs::path(cpp_path).filename().generic_string() + ": " + std::to_string(mr.updated_nodes.size()) +
                  " node(s) updated, " + std::to_string(mr.conflicts.size()) + " conflict(s)");
    if (!mr.conflicts.empty()) ctx.OpenPanel("Conflicts");
    bool has_error = false;
    for (const Diagnostic& d : mr.diagnostics) has_error = has_error || d.level == Diagnostic::Level::Error;
    if (has_error) return Fail(err, "merge reported errors, see Diagnostics");
    return true;
#endif
}

namespace {

const Node* FindLiteralLabelNode(const Node& n, const std::string& exclude = std::string()) {
    if (const PropValue* pv = n.Prop("label")) {
        bool plain = pv->raw.IsString() && !pv->raw.AsString().empty() &&
                     pv->raw.AsString().find_first_of("{}%#\\\"") == std::string::npos;
        if (!n.locked && !pv->IsExpr() && !pv->IsParam() && plain && n.id != exclude) return &n;
    }
    for (const Node& c : n.children)
        if (const Node* r = FindLiteralLabelNode(c, exclude)) return r;
    for (const auto& s : n.slots)
        for (const Node& c : s.second)
            if (const Node* r = FindLiteralLabelNode(c, exclude)) return r;
    return nullptr;
}

void CollectLiteralLabelNodes(const Node& n, std::vector<const Node*>& out) {
    if (const Node* self = FindLiteralLabelNode(n); self == &n) out.push_back(&n);
    for (const Node& c : n.children) CollectLiteralLabelNodes(c, out);
    for (const auto& s : n.slots)
        for (const Node& c : s.second) CollectLiteralLabelNodes(c, out);
}

bool EditLiteralInBlock(std::string& text, const std::string& node_id, const std::string& from, const std::string& to) {
    size_t b = text.find("@imb:begin id=" + node_id + " ");
    if (b == std::string::npos) return false;
    size_t e = text.find("@imb:end", b);
    std::string quoted = "\"" + from + "\"";
    size_t q = text.find(quoted, b);
    if (q == std::string::npos || (e != std::string::npos && q > e)) return false;
    text.replace(q, quoted.size(), "\"" + to + "\"");
    return true;
}

}  // namespace

bool ExportSelfCheck(const std::string& project_dir, const std::string& out_dir, std::string& report) {
    auto fail = [&](const std::string& m) {
        report += "FAIL: " + m + "\n";
        std::string e;
        WriteFileAtomic(JoinPath(out_dir, "imforge_selfcheck.txt"), report, &e);
        return false;
    };
#if !(IMFORGE_EXPORT_HAS_CODEGEN && IMFORGE_EXPORT_HAS_ROUNDTRIP)
    (void)project_dir;
    return fail("codegen/roundtrip are not linked into this build");
#else
    EditorContext ctx;
    std::string err;
    if (!ctx.LoadProjectDir(project_dir, &err)) return fail("load " + project_dir + ": " + err);
    ctx.live.Stop();
    std::error_code ec;
    fs::remove_all(out_dir, ec);
    ctx.project.codegen.output_dir = NormalizePath(out_dir);
    ApproveExternalOutputDir(ctx.project.codegen.output_dir);
    ctx.project.codegen.markers = true;
    if (!ExportProject(ctx, &err, true)) return fail("first export: " + err);
    ConflictsState& cs = GetConflictsState();
    report += "export 1: " + std::to_string(cs.merged_files.size()) + " file(s) in " + out_dir + "\n";
    std::string panel_name, node_id, label;
    for (const Panel& p : ctx.project.panels) {
        if (const Node* n = FindLiteralLabelNode(p.root)) {
            panel_name = p.name;
            node_id = n->id;
            label = n->Prop("label")->raw.AsString();
            break;
        }
    }
    if (node_id.empty()) return fail("no panel node with a literal label");
    std::string cpp_path, text;
    for (const auto& kv : cs.merged_files) {
        if (fs::path(kv.first).extension() != ".cpp" || FileMarkerValue(kv.second, "panel") != panel_name) continue;
        if (kv.second.find("@imb:begin id=" + node_id + " ") == std::string::npos) continue;
        cpp_path = kv.first;
        text = kv.second;
        break;
    }
    if (cpp_path.empty()) return fail("no generated .cpp containing block " + panel_name + "/" + node_id);
    size_t locked_before = 0;
    for (size_t p = text.find("@imb:locked"); p != std::string::npos; p = text.find("@imb:locked", p + 1)) ++locked_before;
    std::string quoted = "\"" + label + "\"";
    std::string edited = label + " (edited)";
    if (!EditLiteralInBlock(text, node_id, label, edited)) return fail("literal " + quoted + " not found inside block " + node_id);
    if (!WriteFileAtomic(cpp_path, text, &err)) return fail(err);
    report += "edited " + RelativeTo(cpp_path, out_dir) + ": " + panel_name + "/" + node_id + " label " + quoted + " -> \"" + edited + "\"\n";
    if (!ImportFromCode(ctx, cpp_path, &err)) return fail("import: " + err);
    const Panel* pp = ctx.FindPanelDoc(panel_name);
    const Node* after = pp ? FindNode(pp->root, node_id) : nullptr;
    const PropValue* lv = after ? after->Prop("label") : nullptr;
    if (!lv || !lv->raw.IsString() || lv->raw.AsString() != edited)
        return fail("panel label was not updated by ImportFromCode (" + (lv ? DumpJson(lv->raw, 0) : std::string("missing")) + ")");
    report += "import: panel updated, undo entries " + std::to_string(ctx.undo.Size()) + "\n";
    if (!ExportProject(ctx, &err, true)) return fail("second export: " + err);
    std::string regenerated;
    if (!ReadTextFile(cpp_path, regenerated)) return fail("cannot read " + cpp_path + " after re-export");
    if (regenerated.find("\"" + edited + "\"") == std::string::npos) return fail("re-exported file does not contain the new literal");
    auto count_locked = [](const std::string& s) {
        size_t n = 0;
        for (size_t p = s.find("@imb:locked"); p != std::string::npos; p = s.find("@imb:locked", p + 1)) ++n;
        return n;
    };
    if (count_locked(regenerated) != locked_before)
        return fail("re-export changed the number of locked blocks (" + std::to_string(locked_before) + " -> " +
                    std::to_string(count_locked(regenerated)) + ")");
    if (regenerated.find("@imb:begin id=" + node_id + " ") == std::string::npos) return fail("block " + node_id + " is no longer a regular begin block");
    if (!cs.conflicts.empty()) return fail(std::to_string(cs.conflicts.size()) + " conflict(s) after re-export: " + cs.conflicts.front().reason);
    if (!ExportProject(ctx, &err, true)) return fail("third export: " + err);
    std::string stable;
    ReadTextFile(cpp_path, stable);
    if (stable != regenerated) return fail("third export changed the file again (merge is not stable)");
    report += "re-export: new literal present, no conflicts, stable on a further export\n";

    const Node* second = FindLiteralLabelNode(ctx.FindPanelDoc(panel_name)->root, node_id);
    std::string node2 = second ? second->id : std::string();
    std::string label2 = second ? second->Prop("label")->raw.AsString() : std::string();
    if (node2.empty() || stable.find("@imb:begin id=" + node2 + " ") == std::string::npos) {
        report += "direct re-export: skipped (no second literal label in " + RelativeTo(cpp_path, out_dir) + ")\nOK\n";
    } else {
        std::string edited2 = label2 + " (direct)";
        if (!EditLiteralInBlock(stable, node2, label2, edited2)) return fail("literal \"" + label2 + "\" not found inside block " + node2);
        if (!WriteFileAtomic(cpp_path, stable, &err)) return fail(err);
        report += "edited " + RelativeTo(cpp_path, out_dir) + ": " + panel_name + "/" + node2 + " label \"" + label2 + "\" -> \"" + edited2 +
                  "\", exporting without import\n";
        if (!ExportProject(ctx, &err, true)) return fail("direct export: " + err);
        const Node* n2 = FindNode(ctx.FindPanelDoc(panel_name)->root, node2);
        const PropValue* l2 = n2 ? n2->Prop("label") : nullptr;
        if (!l2 || !l2->raw.IsString() || l2->raw.AsString() != edited2) return fail("direct export did not pull the literal into the panel");
        std::string out2, out3;
        ReadTextFile(cpp_path, out2);
        if (out2.find("\"" + edited2 + "\"") == std::string::npos || out2.find("\"" + edited + "\"") == std::string::npos)
            return fail("direct export lost an edited literal");
        if (!cs.conflicts.empty()) return fail(std::to_string(cs.conflicts.size()) + " conflict(s) after direct export: " + cs.conflicts.front().reason);
        if (!ExportProject(ctx, &err, true)) return fail("export after direct export: " + err);
        ReadTextFile(cpp_path, out3);
        if (out3 != out2) return fail("export after direct export changed the file again (second merge pass missing?)");
        report += "direct re-export: panel updated, both literals present, no conflicts, stable; undo entries " +
                  std::to_string(ctx.undo.Size()) + "\n";

        std::vector<const Node*> labelled;
        CollectLiteralLabelNodes(ctx.FindPanelDoc(panel_name)->root, labelled);
        const Node* third = nullptr;
        for (const Node* n : labelled)
            if (n->id != node_id && n->id != node2 && out3.find("@imb:begin id=" + n->id + " ") != std::string::npos) third = n;
        if (!third) {
            report += "conflict keep: skipped (no third literal label)\nOK\n";
        } else {
            std::string text3 = out3;
            const std::string marker = "// imforge selfcheck structural edit";
            size_t b = text3.find("@imb:begin id=" + node_id + " ");
            size_t eol = text3.find('\n', b);
            text3.insert(eol + 1, "                            " + marker + "\n                            (void)0;\n");
            std::string label3 = third->Prop("label")->raw.AsString();
            if (!EditLiteralInBlock(text3, third->id, label3, label3 + " (third)")) return fail("literal \"" + label3 + "\" not found in block " + third->id);
            if (!WriteFileAtomic(cpp_path, text3, &err)) return fail(err);
            report += "structural edit in " + node_id + " + literal edit in " + third->id + ", exporting\n";
            if (!ExportProject(ctx, &err, true)) return fail("conflict export: " + err);
            std::string out4, out5;
            ReadTextFile(cpp_path, out4);
            if (cs.conflicts.empty()) return fail("structural edit in " + node_id + " was not reported as a conflict");
            if (out4.find(marker) == std::string::npos) return fail("structural edit lost by the export (conflict not kept)");
            if (out4.find("\"" + label3 + " (third)\"") == std::string::npos) return fail("literal edit next to a conflict lost");
            if (!ExportProject(ctx, &err, true)) return fail("export with unresolved conflict: " + err);
            ReadTextFile(cpp_path, out5);
            if (out5.find(marker) == std::string::npos) return fail("unresolved conflict lost on the next export");
            if (cs.conflicts.empty()) return fail("unresolved conflict no longer reported on the next export");
            report += "conflict keep: " + std::to_string(cs.conflicts.size()) + " conflict(s) (" + cs.conflicts.front().node_id +
                      "), user code kept across two exports, neighbouring literal edit applied\nOK\n";
        }
    }
    std::string e2;
    WriteFileAtomic(JoinPath(out_dir, "imforge_selfcheck.txt"), report, &e2);
    return true;
#endif
}

bool ResolveConflict(EditorContext& ctx, size_t index, bool keep_user_code, std::string* err) {
    ConflictsState& cs = GetConflictsState();
    if (index >= cs.conflicts.size()) return Fail(err, "conflict index out of range");
    ConflictItem& c = cs.conflicts[index];
    if (c.resolved) return true;
#if !IMFORGE_EXPORT_HAS_ROUNDTRIP
    (void)ctx;
    (void)keep_user_code;
    return Fail(err, "round-trip is not available in this build");
#else
    std::string& source = cs.merged_files[c.file];
    if (source.empty() && !ReadTextFile(c.file, source)) return Fail(err, "cannot read " + c.file);
    roundtrip::Conflict rc;
    rc.node_id = c.node_id;
    rc.reason = c.reason;
    rc.user_code = c.user_code;
    rc.generated_code = c.generated_code;
    std::string resolved = roundtrip::ApplyResolution(source, rc, keep_user_code);
    if (keep_user_code && !LockNode(ctx, c.panel, c.node_id))
        ctx.Log(LogLevel::Warning, "conflict %s/%s: node not found, code kept but node not locked", c.panel.c_str(), c.node_id.c_str());
    if (!WriteFileAtomic(c.file, resolved, err)) return false;
    source = resolved;
    c.resolved = true;
    c.keep_user = keep_user_code;
    cs.report.push_back(fs::path(c.file).filename().generic_string() + ": " + c.node_id +
                        (keep_user_code ? " kept user code (node locked)" : " regenerated"));
    if (!cs.output_dir.empty()) {
        std::map<std::string, std::string> files;
        for (const auto& kv : cs.merged_files)
            if (kv.first.compare(0, cs.output_dir.size(), cs.output_dir) == 0) files[kv.first] = kv.second;
        std::string merr;
        if (!files.empty() && !WriteManifest(cs.output_dir, ctx.project.name, files, &merr))
            ctx.Log(LogLevel::Warning, "imforge_export.json not updated: %s", merr.c_str());
    }
    MirrorConflicts(ctx);
    cs.Touch();
    ctx.Log(LogLevel::Info, "conflict %s/%s resolved: %s", c.panel.c_str(), c.node_id.c_str(),
            keep_user_code ? "kept user code, node locked" : "regenerated");
    return true;
#endif
}

}  // namespace imforge::builder
