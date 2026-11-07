#include "imblib.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

#if __has_include("miniz.h")
#include "miniz.h"
#define IMFORGE_IMBLIB_HAS_MINIZ 1
#else
#define IMFORGE_IMBLIB_HAS_MINIZ 0
#endif

namespace imforge::builder {

namespace fs = std::filesystem;

namespace {

using Entries = std::map<std::string, std::string>;

bool Fail(std::string* err, const std::string& msg) {
    if (err) *err = msg;
    return false;
}

bool EndsWith(const std::string& s, const char* suffix) {
    size_t n = std::strlen(suffix);
    return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

std::string BaseName(const std::string& p) {
    size_t i = p.find_last_of("/\\");
    return i == std::string::npos ? p : p.substr(i + 1);
}

std::string Stem(const std::string& p) {
    std::string b = BaseName(p);
    size_t dot = b.find('.');
    return dot == std::string::npos ? b : b.substr(0, dot);
}

std::string Join(const std::string& dir, const std::string& rel) {
    if (dir.empty()) return rel;
    fs::path r(rel);
    if (r.is_absolute()) return r.generic_string();
    return (fs::path(dir) / r).generic_string();
}

bool ReadFile(const std::string& path, std::string& out) {
    std::ifstream in(fs::path(path), std::ios::binary);
    if (!in) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}

bool WriteFile(const std::string& path, const std::string& content, std::string* err) {
    std::error_code ec;
    fs::path p(path);
    if (p.has_parent_path()) fs::create_directories(p.parent_path(), ec);
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    if (!out) return Fail(err, "cannot write " + path);
    out.write(content.data(), (std::streamsize)content.size());
    return out.good() ? true : Fail(err, "cannot write " + path);
}

std::string NormalizeEntryName(std::string n) {
    std::replace(n.begin(), n.end(), '\\', '/');
    while (n.compare(0, 2, "./") == 0) n.erase(0, 2);
    while (!n.empty() && n[0] == '/') n.erase(0, 1);
    return n;
}

bool SamePath(const std::string& a, const std::string& b) {
    std::error_code ec;
    fs::path pa = fs::absolute(a, ec).lexically_normal(), pb = fs::absolute(b, ec).lexically_normal();
    return pa == pb;
}

constexpr size_t kMaxEntryBytes = size_t(16) << 20;
constexpr size_t kMaxTotalBytes = size_t(64) << 20;
constexpr unsigned kMaxEntries = 4096;

bool IsSafeEntryName(const std::string& n) {
    if (n.empty() || n.size() > 260 || n[0] == '/') return false;
    for (char ch : n) {
        unsigned char c = (unsigned char)ch;
        if (c < 0x20 || c == 0x7F || c == '\\' || c == ':') return false;
    }
    size_t start = 0;
    while (start <= n.size()) {
        size_t end = n.find('/', start);
        if (end == std::string::npos) end = n.size();
        std::string seg = n.substr(start, end - start);
        bool last = end == n.size();
        if (seg == ".." || (seg.empty() && !last)) return false;
        start = end + 1;
    }
    return true;
}

bool HasAllowedExtension(const std::string& n) {
    static const char* const kExt[] = {".json", ".h", ".hpp", ".inl", ".md", ".txt", ".png"};
    std::string lower = n;
    for (char& c : lower) c = (char)std::tolower((unsigned char)c);
    for (const char* e : kExt)
        if (EndsWith(lower, e)) return true;
    return false;
}

std::string SafeFileName(const std::string& name) {
    std::string out;
    for (char c : name) {
        bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-' ||
                  c == '.' || c == ' ' || ((unsigned char)c >= 0x80);
        out.push_back(ok ? c : '_');
    }
    if (out.empty() || out == "." || out == "..") out.insert(0, "_");
    return out;
}

bool IsUnder(const fs::path& base, const fs::path& target) {
    fs::path rel = target.lexically_relative(base);
    if (rel.empty() || rel.is_absolute()) return false;
    return *rel.begin() != "..";
}

bool WriteInside(const std::string& dir, const std::string& rel, const std::string& content, std::string* err) {
    std::error_code ec;
    fs::path base = fs::weakly_canonical(fs::absolute(fs::path(dir.empty() ? "." : dir), ec), ec);
    fs::path target = fs::weakly_canonical(base / fs::path(rel), ec);
    if (ec || !IsUnder(base, target)) return Fail(err, "refusing to write outside the project: " + rel);
    if (fs::is_symlink(fs::symlink_status(target, ec))) return Fail(err, "refusing to write through a symlink: " + rel);
    return WriteFile(target.string(), content, err);
}

bool ReadZip(const std::string& path, Entries& out, std::string* err) {
#if IMFORGE_IMBLIB_HAS_MINIZ
    mz_zip_archive zip;
    std::memset(&zip, 0, sizeof(zip));
    if (!mz_zip_reader_init_file(&zip, path.c_str(), 0)) return Fail(err, "cannot open library " + path);
    mz_uint n = mz_zip_reader_get_num_files(&zip);
    auto reject = [&](const std::string& why) {
        mz_zip_reader_end(&zip);
        return Fail(err, path + ": " + why);
    };
    if (n > kMaxEntries) return reject("too many entries");
    size_t declared_total = 0, actual_total = 0;
    for (mz_uint i = 0; i < n; ++i) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&zip, i, &st)) return reject("corrupt archive directory");
        std::string raw_name = st.m_filename;
        if (!IsSafeEntryName(raw_name)) return reject("unsafe entry name \"" + SafeFileName(raw_name.substr(0, 80)) + "\"");
        if ((st.m_version_made_by >> 8) == 3 && ((st.m_external_attr >> 16) & 0170000u) == 0120000u)
            return reject("symlink entry " + raw_name);
        if (st.m_is_directory) continue;
        if ((st.m_bit_flag & 1) != 0) return reject("encrypted entry " + raw_name);
        if (!HasAllowedExtension(raw_name)) continue;
        if (st.m_uncomp_size > kMaxEntryBytes) return reject("entry too large " + raw_name);
        declared_total += (size_t)st.m_uncomp_size;
        if (declared_total > kMaxTotalBytes) return reject("archive expands to more than 64 MB");
        std::string key = NormalizeEntryName(raw_name);
        if (out.count(key)) return reject("duplicate entry " + raw_name);
        size_t size = 0;
        void* data = mz_zip_reader_extract_to_heap(&zip, i, &size, 0);
        if (!data) return reject("cannot extract " + raw_name);
        actual_total += size;
        if (size > kMaxEntryBytes || actual_total > kMaxTotalBytes) {
            mz_free(data);
            return reject("archive expands to more than 64 MB");
        }
        out[key].assign((const char*)data, size);
        mz_free(data);
    }
    mz_zip_reader_end(&zip);
    return true;
#else
    (void)out;
    return Fail(err, "miniz is not available in this build; cannot read " + path);
#endif
}

std::vector<std::string> ListedOrFallback(const Json& manifest, const char* key, const Entries& entries,
                                          std::initializer_list<const char*> suffixes) {
    std::vector<std::string> out;
    const Json& list = manifest[key];
    for (const Json& j : list.arr)
        if (j.IsString() && !j.AsString().empty()) out.push_back(NormalizeEntryName(j.AsString()));
    if (list.IsArray()) return out;
    for (const auto& kv : entries) {
        for (const char* s : suffixes) {
            if (EndsWith(kv.first, s)) {
                out.push_back(kv.first);
                break;
            }
        }
    }
    return out;
}

bool ParseManifest(const Entries& entries, const std::string& where, Json& out, std::string* err) {
    auto it = entries.find("manifest.json");
    if (it == entries.end()) return Fail(err, where + ": missing manifest.json");
    JsonError je;
    if (!ParseJson(it->second, out, &je)) return Fail(err, where + ": manifest.json: " + je.message);
    if (!out.IsObject()) return Fail(err, where + ": manifest.json must be an object");
    if (out["kind"].AsString("imblib") != "imblib") return Fail(err, where + ": manifest kind is not \"imblib\"");
    return true;
}

std::string FindHeaderFile(const Project& project, const std::string& inc, std::string& content) {
    std::vector<std::string> cands;
    if (fs::path(inc).is_absolute()) cands.push_back(inc);
    cands.push_back(Join(project.dir, inc));
    cands.push_back(Join(project.dir, "include/" + inc));
    for (const std::string& p : project.include_paths) cands.push_back(Join(Join(project.dir, p), inc));
    std::error_code ec;
    fs::path libs = fs::path(project.dir.empty() ? "." : project.dir) / "libs";
    if (fs::is_directory(libs, ec))
        for (const auto& e : fs::directory_iterator(libs, ec))
            if (e.is_directory(ec)) cands.push_back((e.path() / inc).generic_string());
    for (const std::string& c : cands)
        if (fs::is_regular_file(c, ec) && ReadFile(c, content)) return c;
    return std::string();
}

void PatchTimestamps(std::string& bytes) {
#if IMFORGE_IMBLIB_HAS_MINIZ
    mz_zip_archive r;
    std::memset(&r, 0, sizeof(r));
    if (!mz_zip_reader_init_mem(&r, bytes.data(), bytes.size(), 0)) return;
    static const unsigned char kDosEpoch[4] = {0x00, 0x00, 0x21, 0x00};
    mz_uint n = mz_zip_reader_get_num_files(&r);
    for (mz_uint i = 0; i < n; ++i) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&r, i, &st)) continue;
        size_t local = (size_t)st.m_local_header_ofs + 10;
        size_t central = (size_t)(r.m_central_directory_file_ofs + st.m_central_dir_ofs) + 12;
        if (local + 4 <= bytes.size()) std::memcpy(&bytes[local], kDosEpoch, 4);
        if (central + 4 <= bytes.size()) std::memcpy(&bytes[central], kDosEpoch, 4);
    }
    mz_zip_reader_end(&r);
#else
    (void)bytes;
#endif
}

bool ZipEntries(const Entries& entries, std::string& bytes, std::string* err) {
#if IMFORGE_IMBLIB_HAS_MINIZ
    mz_zip_archive zip;
    std::memset(&zip, 0, sizeof(zip));
    if (!mz_zip_writer_init_heap(&zip, 0, 64 * 1024)) return Fail(err, "miniz: cannot initialise archive");
    MZ_TIME_T fixed_time;
    std::memset(&fixed_time, 0, sizeof(fixed_time));
    for (const auto& kv : entries) {
        if (!mz_zip_writer_add_mem_ex_v2(&zip, kv.first.c_str(), kv.second.data(), kv.second.size(), nullptr, 0,
                                         MZ_DEFAULT_LEVEL, 0, 0, &fixed_time, nullptr, 0, nullptr, 0)) {
            mz_zip_writer_end(&zip);
            return Fail(err, "miniz: cannot add " + kv.first);
        }
    }
    void* buf = nullptr;
    size_t size = 0;
    if (!mz_zip_writer_finalize_heap_archive(&zip, &buf, &size)) {
        mz_zip_writer_end(&zip);
        return Fail(err, "miniz: cannot finalize archive");
    }
    bytes.assign((const char*)buf, size);
    mz_free(buf);
    mz_zip_writer_end(&zip);
    PatchTimestamps(bytes);
    return true;
#else
    (void)entries;
    (void)bytes;
    return Fail(err, "miniz is not available in this build; cannot write libraries");
#endif
}

}  // namespace

namespace imblib {

std::string SanitizeLibraryName(const std::string& name) {
    std::string out;
    for (char c : name) {
        bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-' ||
                  c == '.';
        out.push_back(ok ? c : '_');
    }
    while (!out.empty() && out.back() == '.') out.pop_back();
    return out.empty() ? "library" : out;
}

std::string LibraryNameFromPath(const std::string& path) { return SanitizeLibraryName(Stem(path)); }

bool ReadLibraryInfo(const std::string& path, LibraryInfo& out) {
    out = LibraryInfo();
    out.file = path;
    Entries entries;
    Json m;
    if (!ReadZip(path, entries, &out.error) || !ParseManifest(entries, path, m, &out.error)) return false;
    out.name = m["name"].AsString(LibraryNameFromPath(path));
    out.version = m["version"].AsString("0.0.0");
    out.min_imgui_version = m["minImGuiVersion"].AsInt(18500);
    for (const std::string& p : ListedOrFallback(m, "components", entries, {".imc.json"})) out.components.push_back(Stem(p));
    for (const std::string& p : ListedOrFallback(m, "widgets", entries, {".imw.json"})) out.widgets.push_back(Stem(p));
    for (const std::string& p : ListedOrFallback(m, "headers", entries, {".h", ".hpp", ".inl"})) out.headers.push_back(BaseName(p));
    for (const Json& d : m["dependencies"].arr) {
        if (d.IsString()) out.dependencies.push_back(d.AsString());
        else if (d.IsObject())
            out.dependencies.push_back(d["name"].AsString("?") + (d["version"].IsString() ? " " + d["version"].AsString() : ""));
    }
    out.valid = true;
    return true;
}

bool Import(Project& project, WidgetCatalog* catalog, const std::string& path, std::string* err,
            ImblibImportResult* result) {
    ImblibImportResult local;
    ImblibImportResult& res = result ? *result : local;
    res = ImblibImportResult();
    Entries entries;
    Json m;
    if (!ReadZip(path, entries, err) || !ParseManifest(entries, path, m, err)) return false;
    std::string lib = SanitizeLibraryName(m["name"].AsString(LibraryNameFromPath(path)));
    res.library = lib;
    const bool write = !project.dir.empty();

    std::vector<Component> incoming;
    std::map<std::string, std::string> renamed;
    for (const std::string& p : ListedOrFallback(m, "components", entries, {".imc.json"})) {
        auto it = entries.find(p);
        if (it == entries.end()) {
            res.warnings.push_back(lib + ": " + p + " listed in manifest but missing from archive");
            continue;
        }
        Json cj;
        JsonError je;
        if (!ParseJson(it->second, cj, &je)) return Fail(err, path + ": " + p + ": " + je.message);
        Component c;
        Error ce;
        if (!ComponentFromJson(cj, c, &ce)) return Fail(err, path + ": " + p + ": " + ce.message);
        if (const Component* ex = project.FindComponent(c.name)) {
            if (ComponentToJson(*ex) == ComponentToJson(c)) {
                res.warnings.push_back("component " + c.name + " is already in the project (identical), skipped");
                continue;
            }
            renamed[c.name] = lib + "." + c.name;
        }
        incoming.push_back(std::move(c));
    }
    for (Component& c : incoming) {
        auto rn = renamed.find(c.name);
        if (rn != renamed.end()) {
            res.warnings.push_back("component " + c.name + " clashes with a project component, imported as " + rn->second);
            c.name = rn->second;
        }
        Walk(c.root, [&](Node& n) {
            if (n.type != "Component") return;
            auto r = renamed.find(n.component);
            if (r != renamed.end()) n.component = r->second;
        });
        c.file = "components/" + SafeFileName(c.name) + ".imc.json";
        if (write && !WriteInside(project.dir, c.file, DumpJson(ComponentToJson(c)) + "\n", err)) return false;
        bool replaced = false;
        for (Component& ex : project.components) {
            if (ex.name == c.name) {
                ex = c;
                replaced = true;
            }
        }
        if (!replaced) project.components.push_back(c);
        res.components.push_back(c.name);
        res.files.push_back(c.file);
    }

    for (const std::string& p : ListedOrFallback(m, "widgets", entries, {".imw.json"})) {
        auto it = entries.find(p);
        if (it == entries.end()) {
            res.warnings.push_back(lib + ": " + p + " listed in manifest but missing from archive");
            continue;
        }
        Json wj;
        JsonError je;
        if (!ParseJson(it->second, wj, &je)) return Fail(err, path + ": " + p + ": " + je.message);
        if (catalog) {
            Error we;
            if (!catalog->AddManifest(wj, &we)) return Fail(err, path + ": " + p + ": " + we.message);
        }
        if (!EndsWith(p, ".json")) {
            res.warnings.push_back(lib + ": widget file " + SafeFileName(p) + " is not a .json file, skipped");
            continue;
        }
        std::string rel = "libs/" + lib + "/widgets/" + SafeFileName(BaseName(p));
        if (write && !WriteInside(project.dir, rel, DumpJson(wj) + "\n", err)) return false;
        res.widgets.push_back(wj["name"].AsString(Stem(p)));
        res.files.push_back(rel);
    }

    for (const std::string& p : ListedOrFallback(m, "headers", entries, {".h", ".hpp", ".inl"})) {
        auto it = entries.find(p);
        if (it == entries.end()) {
            res.warnings.push_back(lib + ": " + p + " listed in manifest but missing from archive");
            continue;
        }
        if (!EndsWith(p, ".h") && !EndsWith(p, ".hpp") && !EndsWith(p, ".inl")) {
            res.warnings.push_back(lib + ": header " + SafeFileName(p) + " is not a header file, skipped");
            continue;
        }
        std::string rel = "libs/" + lib + "/" + SafeFileName(BaseName(p));
        if (write && !WriteInside(project.dir, rel, it->second, err)) return false;
        res.files.push_back(rel);
    }

    std::string lib_rel = "libs/" + lib + ".imblib";
    if (write) {
        std::string target = Join(project.dir, lib_rel);
        if (!SamePath(target, path)) {
            std::string raw;
            if (!ReadFile(path, raw)) return Fail(err, "cannot read " + path);
            if (!WriteInside(project.dir, lib_rel, raw, err)) return false;
        }
        res.files.push_back(lib_rel);
    }
    if (std::find(project.libraries.begin(), project.libraries.end(), lib_rel) == project.libraries.end())
        project.libraries.push_back(lib_rel);
    return true;
}

bool BuildArchive(const Project& project, const WidgetCatalog* catalog, const ImblibExportOptions& opts,
                  std::string& bytes, std::string* err, std::vector<std::string>* warnings) {
    Entries entries;
    std::vector<std::string> names = opts.component_names, types = opts.widget_types;
    std::sort(names.begin(), names.end());
    names.erase(std::unique(names.begin(), names.end()), names.end());
    std::sort(types.begin(), types.end());
    types.erase(std::unique(types.begin(), types.end()), types.end());
    Json comps = Json::Array(), widgets = Json::Array(), headers = Json::Array();
    for (const std::string& n : names) {
        const Component* c = project.FindComponent(n);
        if (!c) return Fail(err, "unknown component " + n);
        std::string rel = "components/" + SafeFileName(n) + ".imc.json";
        entries[rel] = DumpJson(ComponentToJson(*c)) + "\n";
        comps.Push(Json(rel));
    }
    std::set<std::string> includes(opts.header_files.begin(), opts.header_files.end());
    for (const std::string& t : types) {
        const CustomWidgetDef* w = catalog ? catalog->FindCustom(t.c_str()) : nullptr;
        if (!w) return Fail(err, "unknown custom widget " + t);
        std::string rel = "widgets/" + SafeFileName(t) + ".imw.json";
        entries[rel] = DumpJson(w->manifest) + "\n";
        widgets.Push(Json(rel));
        if (!w->include.empty()) includes.insert(w->include);
    }
    for (const std::string& inc : includes) {
        std::string content;
        if (FindHeaderFile(project, inc, content).empty()) {
            if (warnings) warnings->push_back("header " + inc + " not found; not bundled");
            continue;
        }
        std::string rel = BaseName(inc);
        entries[rel] = content;
        headers.Push(Json(rel));
    }
    Json manifest = Json::Object();
    manifest.Set("schemaVersion", Json(1));
    manifest.Set("kind", Json("imblib"));
    manifest.Set("name", Json(SanitizeLibraryName(opts.name)));
    manifest.Set("version", Json(opts.version.empty() ? std::string("1.0.0") : opts.version));
    manifest.Set("minImGuiVersion", Json(opts.min_imgui_version));
    manifest.Set("components", std::move(comps));
    manifest.Set("widgets", std::move(widgets));
    manifest.Set("headers", std::move(headers));
    manifest.Set("dependencies", Json::Array());
    entries["manifest.json"] = DumpJson(manifest) + "\n";
    return ZipEntries(entries, bytes, err);
}

bool Export(const Project& project, const WidgetCatalog* catalog, const ImblibExportOptions& opts,
            const std::string& out_path, std::string* err, std::vector<std::string>* warnings) {
    std::string bytes;
    if (!BuildArchive(project, catalog, opts, bytes, err, warnings)) return false;
    return WriteFile(out_path, bytes, err);
}

}  // namespace imblib

std::vector<LibraryInfo> ListLibraries(const Project& project) {
    std::vector<LibraryInfo> out;
    for (const std::string& lib : project.libraries) {
        LibraryInfo info;
        if (!imblib::ReadLibraryInfo(Join(project.dir, lib), info)) info.name = imblib::LibraryNameFromPath(lib);
        info.file = lib;
        out.push_back(std::move(info));
    }
    return out;
}

}  // namespace imforge::builder
