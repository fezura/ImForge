#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "imforge/imforge_core.h"

namespace fs = std::filesystem;
using namespace imforge;

namespace {

const char* const kUsage =
    "usage: imforge-fmt [--check] [--quiet] <file-or-dir>...\n"
    "Rewrites *.imb.json, *.imc.json, *.theme.json and imforge.json in canonical form\n"
    "(FromJson -> ToJson, sorted keys, 2-space indent, trailing newline).\n"
    "  --check   do not write; list files that would change and exit 1 if any\n"
    "  --quiet   only report errors (and, with --check, files that would change)\n";

enum class FileKind { None, Panel, Component, Theme, Project };

bool EndsWith(const std::string& s, const char* suffix) {
    size_t n = std::strlen(suffix);
    return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

FileKind KindOf(const fs::path& p) {
    std::string name = p.filename().string();
    if (name == "imforge.json") return FileKind::Project;
    if (EndsWith(name, ".imb.json")) return FileKind::Panel;
    if (EndsWith(name, ".imc.json")) return FileKind::Component;
    if (EndsWith(name, ".theme.json")) return FileKind::Theme;
    return FileKind::None;
}

bool ReadText(const fs::path& p, std::string& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

bool Canonical(FileKind kind, const Json& j, std::string& out, Error* err) {
    Json back;
    switch (kind) {
    case FileKind::Panel: {
        Panel p;
        if (!PanelFromJson(j, p, err)) return false;
        back = PanelToJson(p);
        break;
    }
    case FileKind::Component: {
        Component c;
        if (!ComponentFromJson(j, c, err)) return false;
        back = ComponentToJson(c);
        break;
    }
    case FileKind::Theme: {
        Theme t;
        if (!ThemeFromJson(j, t, err)) return false;
        back = ThemeToJson(t);
        break;
    }
    case FileKind::Project: {
        Project p;
        if (!ProjectFromJson(j, p, err)) return false;
        back = ProjectToJson(p);
        break;
    }
    case FileKind::None: return false;
    }
    out = DumpJson(back, 2, true) + "\n";
    return true;
}

void Collect(const fs::path& arg, std::vector<fs::path>& files, int& errors) {
    std::error_code ec;
    if (fs::is_regular_file(arg, ec)) {
        if (KindOf(arg) == FileKind::None) {
            std::fprintf(stderr, "imforge-fmt: %s: not a panel/component/theme/project file\n", arg.string().c_str());
            ++errors;
            return;
        }
        files.push_back(arg);
        return;
    }
    if (!fs::is_directory(arg, ec)) {
        std::fprintf(stderr, "imforge-fmt: %s: no such file or directory\n", arg.string().c_str());
        ++errors;
        return;
    }
    for (fs::recursive_directory_iterator it(arg, ec), end; !ec && it != end; it.increment(ec))
        if (it->is_regular_file(ec) && KindOf(it->path()) != FileKind::None) files.push_back(it->path());
    if (ec) {
        std::fprintf(stderr, "imforge-fmt: %s: %s\n", arg.string().c_str(), ec.message().c_str());
        ++errors;
    }
}

}  // namespace

int main(int argc, char** argv) {
    bool check = false, quiet = false;
    std::vector<fs::path> args;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--check") check = true;
        else if (a == "--quiet" || a == "-q") quiet = true;
        else if (a == "--help" || a == "-h") {
            std::fputs(kUsage, stdout);
            return 0;
        } else if (!a.empty() && a[0] == '-') {
            std::fprintf(stderr, "imforge-fmt: unknown option '%s'\n%s", a.c_str(), kUsage);
            return 2;
        } else {
            args.emplace_back(a);
        }
    }
    if (args.empty()) {
        std::fputs(kUsage, stderr);
        return 2;
    }
    std::vector<fs::path> files;
    int errors = 0;
    for (const fs::path& a : args) Collect(a, files, errors);
    std::sort(files.begin(), files.end());
    files.erase(std::unique(files.begin(), files.end()), files.end());
    int changed = 0;
    for (const fs::path& f : files) {
        std::string text;
        if (!ReadText(f, text)) {
            std::fprintf(stderr, "imforge-fmt: %s: cannot read\n", f.string().c_str());
            ++errors;
            continue;
        }
        Json j;
        JsonError je;
        if (!ParseJson(text, j, &je)) {
            std::fprintf(stderr, "imforge-fmt: %s:%d:%d: %s\n", f.string().c_str(), je.line, je.col, je.message.c_str());
            ++errors;
            continue;
        }
        std::string out;
        Error err;
        if (!Canonical(KindOf(f), j, out, &err)) {
            std::fprintf(stderr, "imforge-fmt: %s: %s\n", f.string().c_str(), err.message.c_str());
            ++errors;
            continue;
        }
        if (out == text) continue;
        ++changed;
        if (check) {
            std::printf("%s\n", f.string().c_str());
            continue;
        }
        std::ofstream o(f, std::ios::binary | std::ios::trunc);
        o.write(out.data(), static_cast<std::streamsize>(out.size()));
        if (!o) {
            std::fprintf(stderr, "imforge-fmt: %s: cannot write\n", f.string().c_str());
            ++errors;
            continue;
        }
        if (!quiet) std::printf("formatted %s\n", f.string().c_str());
    }
    if (!quiet) std::fprintf(stderr, "imforge-fmt: %zu file(s), %d %s, %d error(s)\n", files.size(), changed,
                             check ? "would change" : "rewritten", errors);
    if (errors) return 2;
    return check && changed ? 1 : 0;
}
