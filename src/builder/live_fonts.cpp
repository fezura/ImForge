#include "live_fonts.h"

#include <cctype>
#include <cstdio>
#include <filesystem>
#include <iterator>
#include <system_error>

#include "imforge/imforge_font_check.h"
#include "imforge/imforge_pairing.h"

namespace imforge::builder {

namespace fs = std::filesystem;

namespace {

std::string Fold(std::string s) {
#ifdef _WIN32
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
#endif
    return s;
}

uint64_t Stamp(const fs::path& p) {
    std::error_code ec;
    uint64_t size = (uint64_t)fs::file_size(p, ec);
    if (ec) return 0;
    uint64_t t = (uint64_t)fs::last_write_time(p, ec).time_since_epoch().count();
    return (t * 1099511628211ull) ^ size;
}

bool ReadAll(const fs::path& p, std::vector<unsigned char>& out) {
#ifdef _WIN32
    std::FILE* f = _wfopen(p.c_str(), L"rb");
#else
    std::FILE* f = std::fopen(p.c_str(), "rb");
#endif
    if (!f) return false;
    out.clear();
    unsigned char buf[65536];
    size_t n = 0;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) {
        if (out.size() + n > kProtocolMaxFontBytes) {
            std::fclose(f);
            return false;
        }
        out.insert(out.end(), buf, buf + n);
    }
    std::fclose(f);
    return !out.empty();
}

}  // namespace

// canonical() resolves links too, so a symlink in the project that points elsewhere is refused
bool FontPathAllowed(const std::string& path, const std::vector<std::string>& roots) {
    if (path.empty() || path.compare(0, 2, "\\\\") == 0 || path.compare(0, 2, "//") == 0) return false;
    std::error_code ec;
    fs::path p = fs::canonical(fs::u8path(path), ec);
    if (ec) return false;
    for (const std::string& r : roots) {
        if (r.empty() || r.compare(0, 2, "\\\\") == 0 || r.compare(0, 2, "//") == 0) continue;
        fs::path root = fs::canonical(fs::u8path(r), ec);
        if (ec) continue;
        auto pi = p.begin();
        bool inside = true;
        for (auto ri = root.begin(); ri != root.end(); ++ri, ++pi) {
            if (ri->empty()) continue;
            if (pi == p.end() || Fold(ri->u8string()) != Fold(pi->u8string())) {
                inside = false;
                break;
            }
        }
        if (inside && pi != p.end()) return true;
    }
    return false;
}

bool IconCodepoints(const std::string& merge_icons, unsigned& lo, unsigned& hi) {
    if (merge_icons == "fontawesome6" || merge_icons == "fontawesome") {
        lo = 0xe005;
        hi = 0xf8ff;
        return true;
    }
    if (merge_icons == "material" || merge_icons == "lucide") {
        lo = 0xe000;
        hi = 0xf8ff;
        return true;
    }
    return false;
}

const LiveFontFile* LiveFontCache::Load(const std::string& path, const std::vector<std::string>& roots, std::string& err) {
    if (!FontPathAllowed(path, roots)) {
        err = "outside the project and builder fonts folders";
        return nullptr;
    }
    std::error_code ec;
    fs::path p = fs::canonical(fs::u8path(path), ec);
    std::string key = Fold(p.u8string());
    uint64_t stamp = Stamp(p);
    auto it = files_.find(key);
    if (it != files_.end() && it->second.stamp == stamp && stamp != 0) return &it->second;
    if (fs::file_size(p, ec) > kProtocolMaxFontBytes || ec) {
        err = ec ? "cannot read the file" : "larger than 8 MB";
        return nullptr;
    }
    LiveFontFile f;
    f.path = p.u8string();
    f.stamp = stamp;
    if (!ReadAll(p, f.data)) {
        err = "cannot read the file";
        return nullptr;
    }
    std::string why;
    if (!fontcheck::ValidateFontData(f.data.data(), f.data.size(), &why)) {
        err = why;
        return nullptr;
    }
    unsigned char digest[32];
    pairing::Sha256 h;
    h.Update(f.data.data(), f.data.size());
    h.Final(digest);
    f.hash = pairing::ToHex(digest, sizeof(digest));
    LiveFontFile& slot = files_[key];
    slot = std::move(f);
    return &slot;
}

LiveFontPlan LiveFontCache::Plan(const Project& project, const std::vector<std::string>& roots, const LiveFontResolver& resolve_file,
                                 const LiveFontResolver& resolve_icons) {
    LiveFontPlan plan;
    size_t total = 0;
    auto add_file = [&](const LiveFontFile* f) {
        for (const LiveFontFile* e : plan.files)
            if (e == f) return true;
        if (plan.files.size() >= kProtocolMaxFontFiles || total + f->data.size() > kProtocolMaxFontTotal) return false;
        plan.files.push_back(f);
        total += f->data.size();
        return true;
    };
    // a project can list any number of fonts, each one a file to read: look at a bounded number and bounded bytes
    size_t examined = 0, read_bytes = 0;
    std::vector<const LiveFontFile*> seen;
    auto note_read = [&](const LiveFontFile* f) {
        if (!f) return;
        for (const LiveFontFile* e : seen)
            if (e == f) return;
        seen.push_back(f);
        read_bytes += f->data.size();
    };
    for (const FontSpec& spec : project.fonts) {
        std::string label = "font '" + spec.name + "'";
        if (examined++ >= kProtocolMaxFonts) {
            plan.warnings.push_back("live: only the first " + std::to_string(kProtocolMaxFonts) + " fonts are sent to the app");
            break;
        }
        if (read_bytes > kProtocolMaxFontTotal) {
            plan.warnings.push_back("live: the font files are over 32 MB, the rest are not sent to the app");
            break;
        }
        if (spec.name.empty() || spec.file.empty()) {
            plan.warnings.push_back("live: " + label + " has no name or file, not sent to the app");
            continue;
        }
        std::string err;
        std::string path = resolve_file(spec.file);
        const LiveFontFile* base = path.empty() ? nullptr : Load(path, roots, err);
        note_read(base);
        if (!base) {
            plan.warnings.push_back("live: " + label + " not sent to the app: " + (path.empty() ? "file not found: " + spec.file : err));
            continue;
        }
        FontSpecMsg m;
        m.name = spec.name.substr(0, kProtocolMaxName);
        float size = spec.size > 0 ? spec.size : 16.0f;
        m.size = size < 4.0f ? 4.0f : size > 256.0f ? 256.0f : size;
        m.ranges = spec.ranges.substr(0, 64);
        m.merge_icons = spec.merge_icons.substr(0, 64);
        m.file = base->hash;
        const LiveFontFile* icons = nullptr;
        if (!spec.merge_icons.empty() && spec.merge_icons != "none") {
            unsigned lo = 0, hi = 0;
            std::string icon_path = IconCodepoints(spec.merge_icons, lo, hi) ? resolve_icons(spec.merge_icons) : std::string();
            icons = icon_path.empty() ? nullptr : Load(icon_path, roots, err);
            note_read(icons);
            if (icons) {
                m.icons = icons->hash;
                m.icon_lo = lo;
                m.icon_hi = hi;
            } else {
                plan.warnings.push_back("live: icons for " + label + " not sent to the app: " +
                                        (icon_path.empty() ? "no font for \"" + spec.merge_icons + "\"" : err));
            }
        }
        if (!add_file(base) || (icons && !add_file(icons))) {
            plan.warnings.push_back("live: " + label + " not sent, the fonts go past " +
                                    std::to_string(kProtocolMaxFontFiles) + " files or 32 MB");
            continue;
        }
        plan.specs.push_back(m);
    }
    // forget files this plan doesn't use, the cache would only ever grow otherwise
    for (auto it = files_.begin(); it != files_.end();) {
        bool used = false;
        for (const LiveFontFile* f : plan.files) used = used || f == &it->second;
        it = used ? std::next(it) : files_.erase(it);
    }
    return plan;
}

}  // namespace imforge::builder
