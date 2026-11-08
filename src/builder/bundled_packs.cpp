// imgui.h first: Value only gets its ImVec2/ImVec4 constructors when IMGUI_VERSION is already defined
#include "imgui.h"

#include "bundled_packs.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>

#include "builder_fonts.h"
#include "editor_context.h"
#include "project_io.h"

// rpcndr.h (via windows.h) does #define small char, and the pack headers use `small` as a name
#ifdef small
#undef small
#endif

#if IMFORGE_HAS_INTERP
#if __has_include("imforge_adapters_essentials.h")
#include "imforge_adapters_essentials.h"
#define IMFORGE_PACK_ESSENTIALS 1
#endif
#if __has_include("imforge_adapters_controls.h")
#include "imforge_adapters_controls.h"
#define IMFORGE_PACK_CONTROLS 1
#endif
#if __has_include("imforge_adapters_display.h")
#include "imforge_adapters_display.h"
#define IMFORGE_PACK_DISPLAY 1
#endif
#if __has_include("imforge_adapters_gaming.h")
#include "imforge_adapters_gaming.h"
#define IMFORGE_PACK_GAMING 1
#endif
#endif

namespace imforge::builder {

namespace fs = std::filesystem;

namespace {

std::vector<BundledPack>& Registry() {
    static std::vector<BundledPack> packs;
    return packs;
}

bool& Scanned() {
    static bool scanned = false;
    return scanned;
}

std::string TitleCase(const std::string& s) {
    std::string out = s;
    if (!out.empty()) out[0] = (char)std::toupper((unsigned char)out[0]);
    return out;
}

void Strings(const Json& arr, std::vector<std::string>& out) {
    for (size_t i = 0; arr.IsArray() && i < arr.Size(); ++i)
        if (arr[i].IsString() && !arr[i].AsString().empty()) out.push_back(arr[i].AsString());
}

bool Inside(const fs::path& base, const fs::path& p) {
    std::string b = base.lexically_normal().generic_string(), s = p.lexically_normal().generic_string();
    return s.size() > b.size() && s.compare(0, b.size(), b) == 0 && s[b.size()] == '/';
}

}  // namespace

std::string FindBundledPacksDir() {
    std::error_code ec;
    auto ok = [&](const fs::path& p) {
        return fs::is_directory(p, ec) && fs::is_regular_file(p / "essentials" / "manifest.json", ec);
    };
    if (const char* env = std::getenv("IMFORGE_PACKS_DIR"))
        if (*env && ok(fs::path(env))) return fs::path(env).lexically_normal().generic_string();
    fs::path exe = ExeDir();
    for (const char* rel : {"packs", "../share/imforge/packs", "../packs"}) {
        fs::path p = exe / rel;
        if (ok(p)) return p.lexically_normal().generic_string();
    }
#ifdef IMFORGE_SOURCE_DIR
    fs::path src = fs::path(IMFORGE_SOURCE_DIR) / "packs";
    if (ok(src)) return src.lexically_normal().generic_string();
#endif
    return std::string();
}

std::vector<BundledPack> ScanBundledPacks(const std::string& packs_dir, WidgetCatalog* catalog,
                                          std::vector<std::string>* warnings) {
    std::vector<BundledPack> out;
    std::error_code ec;
    if (packs_dir.empty() || !fs::is_directory(fs::path(packs_dir), ec)) return out;
    std::vector<fs::path> dirs;
    for (fs::directory_iterator it(fs::path(packs_dir), ec), end; !ec && it != end; it.increment(ec))
        if (it->is_directory(ec) && fs::is_regular_file(it->path() / "manifest.json", ec)) dirs.push_back(it->path());
    std::sort(dirs.begin(), dirs.end());
    for (const fs::path& dir : dirs) {
        Json m;
        Error err;
        std::string mpath = (dir / "manifest.json").generic_string();
        if (!ReadJsonFile(mpath, m, &err) || !m.IsObject()) {
            if (warnings) warnings->push_back(mpath + ": " + err.message);
            continue;
        }
        BundledPack pack;
        pack.name = m["name"].AsString(dir.filename().generic_string());
        if (pack.name.empty()) pack.name = dir.filename().generic_string();
        if (std::any_of(out.begin(), out.end(), [&](const BundledPack& o) { return o.name == pack.name; })) {
            if (warnings) warnings->push_back(mpath + ": a pack named " + pack.name + " is already loaded, skipped");
            continue;
        }
        pack.title = m["title"].AsString(TitleCase(pack.name));
        pack.dir = dir.lexically_normal().generic_string();
        pack.version = m["version"].AsString("");
        pack.description = m["description"].AsString("");
        Strings(m["headers"], pack.headers);
        pack.has_adapters = HasBundledAdapters(pack.name);
        std::vector<std::string> files;
        Strings(m["widgets"], files);
        for (const std::string& rel : files) {
            fs::path wp = dir / fs::path(rel);
            if (!Inside(dir, wp)) {
                if (warnings) warnings->push_back(pack.name + ": widget path " + rel + " leaves the pack folder");
                continue;
            }
            Json wj;
            Error we;
            if (!ReadJsonFile(wp.generic_string(), wj, &we)) {
                if (warnings) warnings->push_back(pack.name + ": " + rel + ": " + we.message);
                continue;
            }
            if (catalog && !catalog->AddManifest(wj, &we)) {
                if (warnings) warnings->push_back(pack.name + ": " + rel + ": " + we.message);
                continue;
            }
            pack.widgets.push_back(wj["name"].AsString());
            std::string inc = wj["include"].AsString("");
            if (!inc.empty() && std::find(pack.includes.begin(), pack.includes.end(), inc) == pack.includes.end())
                pack.includes.push_back(inc);
        }
        out.push_back(std::move(pack));
    }
    return out;
}

const std::vector<BundledPack>& BundledPacks() {
    if (!Scanned()) {
        Scanned() = true;
        Registry() = ScanBundledPacks(FindBundledPacksDir(), nullptr, nullptr);
    }
    return Registry();
}

const BundledPack* BundledPackForWidget(const std::string& type) {
    for (const BundledPack& p : BundledPacks())
        if (std::find(p.widgets.begin(), p.widgets.end(), type) != p.widgets.end()) return &p;
    return nullptr;
}

const BundledPack* BundledPackByName(const std::string& name) {
    for (const BundledPack& p : BundledPacks())
        if (p.name == name) return &p;
    return nullptr;
}

bool HasBundledAdapters(const std::string& pack) {
    (void)pack;
#ifdef IMFORGE_PACK_ESSENTIALS
    if (pack == "essentials") return true;
#endif
#ifdef IMFORGE_PACK_CONTROLS
    if (pack == "controls") return true;
#endif
#ifdef IMFORGE_PACK_DISPLAY
    if (pack == "display") return true;
#endif
#ifdef IMFORGE_PACK_GAMING
    if (pack == "gaming") return true;
#endif
    return false;
}

int RegisterBundledAdapters(Interpreter& interp) {
    int n = 0;
    (void)interp;
#ifdef IMFORGE_PACK_ESSENTIALS
    RegisterPackWidgets_essentials(interp);
    ++n;
#endif
#ifdef IMFORGE_PACK_CONTROLS
    RegisterPackWidgets_controls(interp);
    ++n;
#endif
#ifdef IMFORGE_PACK_DISPLAY
    RegisterPackWidgets_display(interp);
    ++n;
#endif
#ifdef IMFORGE_PACK_GAMING
    RegisterPackWidgets_gaming(interp);
    ++n;
#endif
    return n;
}

void InitBundledPacks(EditorContext& ctx) {
    std::string dir = FindBundledPacksDir();
    if (dir.empty()) {
        ctx.Log(LogLevel::Warning, "bundled widget packs not found (looked next to the exe and in the source tree)");
        return;
    }
    std::vector<std::string> warnings;
    Registry() = ScanBundledPacks(dir, &ctx.catalog, &warnings);
    Scanned() = true;
    for (const std::string& w : warnings) ctx.Log(LogLevel::Warning, "packs: %s", w.c_str());
#if IMFORGE_HAS_INTERP
    RegisterBundledAdapters(ctx.interp);
#endif
    int widgets = 0;
    for (const BundledPack& p : Registry()) {
        widgets += (int)p.widgets.size();
        if (!p.has_adapters)
            ctx.Log(LogLevel::Info, "pack %s has no adapters in this build, its widgets draw as placeholders", p.name.c_str());
    }
    ctx.Log(LogLevel::Info, "loaded %d bundled packs (%d widgets) from %s", (int)Registry().size(), widgets, dir.c_str());
}

std::set<std::string> UsedWidgetTypes(const Project& project) {
    std::set<std::string> out;
    auto add = [&](const Node& n) { out.insert(n.type); };
    for (const Panel& p : project.panels) Walk(p.root, add);
    for (const Component& c : project.components) Walk(c.root, add);
    return out;
}

}  // namespace imforge::builder
