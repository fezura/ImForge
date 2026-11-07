#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "clang_scanner.h"
#include "compile_commands.h"
#include "reflect_header.h"
#include "type_database.h"

using namespace imforge;
using namespace imforge::binding;

namespace {

const char* const kUsage =
    "usage: imforge-reflect [options]\n"
    "  --compile-commands <file>  compile_commands.json to take flags from\n"
    "  --include, -I <dir>        include directory (repeatable)\n"
    "  --define, -D <X[=Y]>       preprocessor define (repeatable)\n"
    "  --header <file>            header to parse (repeatable; default: every TU of compile_commands)\n"
    "  --types A,B,...            struct/class/enum names to reflect (repeatable)\n"
    "  --std <c++17>              language standard when no compile command applies\n"
    "  --from-json <types.json>   load an existing types.json instead of / in addition to scanning\n"
    "  --out-json <file>          write types.json\n"
    "  --out-header <file>        write imforge_reflect.h (IMFORGE_REFLECT macros)\n"
    "  --value-include <path>     include used for imforge_value.h (default \"imforge_value.h\")\n"
    "  --no-type-includes         do not #include each type's header in imforge_reflect.h\n"
    "  --quiet, -q                suppress scanner log\n"
    "  --version                  print libclang availability\n"
    "Without --out-json/--out-header the types.json document is printed to stdout.\n";

void SplitList(const std::string& s, std::vector<std::string>& out) {
    size_t start = 0;
    while (start <= s.size()) {
        size_t comma = s.find(',', start);
        if (comma == std::string::npos) comma = s.size();
        std::string item = s.substr(start, comma - start);
        while (!item.empty() && (item.front() == ' ' || item.front() == '\t')) item.erase(0, 1);
        while (!item.empty() && (item.back() == ' ' || item.back() == '\t')) item.pop_back();
        if (!item.empty()) out.push_back(item);
        start = comma + 1;
    }
}

int Fail(const std::string& msg) {
    std::fprintf(stderr, "imforge-reflect: %s\n", msg.c_str());
    return 1;
}

}  // namespace

int main(int argc, char** argv) {
    ScanOptions scan;
    std::vector<std::string> from_json;
    std::string out_json, out_header;
    ReflectHeaderOptions header_opt;
    bool quiet = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto value = [&](const char* name) -> std::string {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "imforge-reflect: %s needs a value\n", name);
                std::exit(2);
            }
            return argv[++i];
        };
        if (a == "--compile-commands") scan.compile_commands = value("--compile-commands");
        else if (a == "--include" || a == "-I") scan.include_dirs.push_back(value("--include"));
        else if (a.compare(0, 2, "-I") == 0) scan.include_dirs.push_back(a.substr(2));
        else if (a == "--define" || a == "-D") scan.defines.push_back(value("--define"));
        else if (a.compare(0, 2, "-D") == 0) scan.defines.push_back(a.substr(2));
        else if (a == "--header") scan.headers.push_back(value("--header"));
        else if (a == "--types") SplitList(value("--types"), scan.types);
        else if (a == "--std") scan.std = value("--std");
        else if (a == "--from-json") from_json.push_back(value("--from-json"));
        else if (a == "--out-json") out_json = value("--out-json");
        else if (a == "--out-header") out_header = value("--out-header");
        else if (a == "--value-include") header_opt.value_include = value("--value-include");
        else if (a == "--no-type-includes") header_opt.include_type_headers = false;
        else if (a == "--quiet" || a == "-q") quiet = true;
        else if (a == "--help" || a == "-h") {
            std::fputs(kUsage, stdout);
            return 0;
        } else if (a == "--version") {
            std::printf("imforge-reflect (libclang: %s)\n",
                        HaveLibClang() ? LibClangVersion() : "not available");
            return 0;
        } else {
            std::fprintf(stderr, "imforge-reflect: unknown argument '%s'\n%s", argv[i], kUsage);
            return 2;
        }
    }

    TypeDatabase db;
    for (const std::string& path : from_json) {
        std::string text, err;
        if (!ReadTextFile(path, text, &err)) return Fail(err);
        Json doc;
        JsonError jerr;
        if (!ParseJson(text, doc, &jerr))
            return Fail(path + ":" + std::to_string(jerr.line) + ":" + std::to_string(jerr.col) +
                        ": " + jerr.message);
        Error e;
        if (!db.Load(doc, &e)) return Fail(path + ": " + e.message);
    }

    bool scanning = !scan.compile_commands.empty() || !scan.headers.empty();
    bool scan_ok = true;
    if (scanning) {
        if (scan.types.empty()) return Fail("--types is required when scanning headers");
        Json types;
        std::vector<std::string> log;
        scan_ok = ScanTypes(scan, types, log);
        if (!quiet)
            for (const std::string& line : log) std::fprintf(stderr, "imforge-reflect: %s\n", line.c_str());
        if (types.IsObject()) {
            Error e;
            if (!db.Load(types, &e)) return Fail(e.message);
        }
    } else if (from_json.empty()) {
        std::fputs(kUsage, stderr);
        return 2;
    }
    if (db.Empty()) return Fail("no types available");

    std::string err;
    if (!out_json.empty()) {
        if (!WriteTextFile(out_json, DumpJson(db.Save()) + "\n", &err)) return Fail(err);
    }
    if (!out_header.empty()) {
        if (!scanning) header_opt.types = scan.types;
        std::string text;
        std::vector<std::string> warnings;
        if (!GenerateReflectHeader(db, header_opt, text, &err, &warnings)) return Fail(err);
        if (!quiet)
            for (const std::string& w : warnings) std::fprintf(stderr, "imforge-reflect: %s\n", w.c_str());
        if (!WriteTextFile(out_header, text, &err)) return Fail(err);
    }
    if (out_json.empty() && out_header.empty()) {
        std::string text = DumpJson(db.Save()) + "\n";
        std::fwrite(text.data(), 1, text.size(), stdout);
    }
    return scan_ok ? 0 : 1;
}
