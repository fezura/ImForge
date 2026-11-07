#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "compile_commands.h"

#include <cstdio>
#include <cstring>

namespace imforge {
namespace binding {

namespace {

bool StartsWith(const std::string& s, const char* p) {
    size_t n = std::strlen(p);
    return s.size() >= n && s.compare(0, n, p) == 0;
}

bool EndsWithNoCase(const std::string& s, const char* suffix) {
    size_t n = std::strlen(suffix);
    if (s.size() < n) return false;
    for (size_t i = 0; i < n; ++i) {
        char a = s[s.size() - n + i], b = suffix[i];
        if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
        if (a != b) return false;
    }
    return true;
}

bool IsAbsolute(const std::string& p) {
    if (p.empty()) return false;
    if (p[0] == '/' || p[0] == '\\') return true;
    return p.size() >= 2 && p[1] == ':';
}

bool PluginFlagTakesValue(const std::string& s) { return s == "-load" || s == "-plugin" || s == "-add-plugin"; }

// compile_commands.json is untrusted, don't let it make clang load plugins or shared libs
bool IsPluginFlag(const std::string& s) {
    return PluginFlagTakesValue(s) || StartsWith(s, "-plugin-arg-") || StartsWith(s, "-fplugin") ||
           StartsWith(s, "-fpass-plugin");
}

bool IsSourceFile(const std::string& a) {
    static const char* const kExt[] = {".cpp", ".cc", ".cxx", ".c++", ".c", ".m", ".mm", ".h", ".hpp", ".hh"};
    for (const char* e : kExt)
        if (EndsWithNoCase(a, e)) return true;
    return false;
}

}  // namespace

bool ReadTextFile(const std::string& path, std::string& out, std::string* err) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        if (err) *err = "cannot open '" + path + "'";
        return false;
    }
    out.clear();
    char buf[65536];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    std::fclose(f);
    return true;
}

bool WriteTextFile(const std::string& path, const std::string& data, std::string* err) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) {
        if (err) *err = "cannot write '" + path + "'";
        return false;
    }
    bool ok = std::fwrite(data.data(), 1, data.size(), f) == data.size();
    std::fclose(f);
    if (!ok && err) *err = "short write to '" + path + "'";
    return ok;
}

std::vector<std::string> SplitCommandLine(const std::string& command) {
    std::vector<std::string> out;
    std::string cur;
    bool have = false, in_single = false, in_double = false;
    for (size_t i = 0; i < command.size(); ++i) {
        char c = command[i];
        if (in_single) {
            if (c == '\'') in_single = false;
            else cur.push_back(c);
            continue;
        }
        if (in_double) {
            if (c == '"') in_double = false;
            else if (c == '\\' && i + 1 < command.size() &&
                     (command[i + 1] == '"' || command[i + 1] == '\\'))
                cur.push_back(command[++i]);
            else cur.push_back(c);
            continue;
        }
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            if (have) out.push_back(cur);
            cur.clear();
            have = false;
            continue;
        }
        have = true;
        if (c == '\'') in_single = true;
        else if (c == '"') in_double = true;
        else if (c == '\\' && i + 1 < command.size() &&
                 (command[i + 1] == ' ' || command[i + 1] == '"' || command[i + 1] == '\'' ||
                  command[i + 1] == '\\'))
            cur.push_back(command[++i]);
        else cur.push_back(c);
    }
    if (have) out.push_back(cur);
    return out;
}

bool ParseCompileCommands(const Json& doc, std::vector<CompileCommand>& out, std::string* err) {
    if (!doc.IsArray()) {
        if (err) *err = "compile_commands.json: expected an array";
        return false;
    }
    for (const Json& e : doc.arr) {
        if (!e.IsObject()) continue;
        CompileCommand cc;
        cc.directory = e["directory"].AsString();
        cc.file = e["file"].AsString();
        if (e["arguments"].IsArray()) {
            for (const Json& a : e["arguments"].arr) cc.args.push_back(a.AsString());
        } else if (e["command"].IsString()) {
            cc.args = SplitCommandLine(e["command"].AsString());
        } else {
            continue;
        }
        if (cc.args.empty()) continue;
        out.push_back(std::move(cc));
    }
    return true;
}

bool LoadCompileCommands(const std::string& path, std::vector<CompileCommand>& out,
                         std::string* err) {
    std::string text;
    if (!ReadTextFile(path, text, err)) return false;
    Json doc;
    JsonError jerr;
    if (!ParseJson(text, doc, &jerr)) {
        if (err)
            *err = path + ":" + std::to_string(jerr.line) + ":" + std::to_string(jerr.col) + ": " +
                   jerr.message;
        return false;
    }
    return ParseCompileCommands(doc, out, err);
}

std::string NormalizePath(const std::string& path) {
    std::string p = path;
    for (char& c : p)
        if (c == '\\') c = '/';
    while (StartsWith(p, "./")) p.erase(0, 2);
    size_t pos;
    while ((pos = p.find("/./")) != std::string::npos) p.erase(pos, 2);
    while ((pos = p.find("//")) != std::string::npos && pos > 0) p.erase(pos, 1);
    return p;
}

std::string JoinPath(const std::string& dir, const std::string& rel) {
    if (rel.empty()) return NormalizePath(dir);
    if (dir.empty() || IsAbsolute(rel)) return NormalizePath(rel);
    std::string d = NormalizePath(dir);
    if (!d.empty() && d.back() != '/') d.push_back('/');
    return NormalizePath(d + rel);
}

std::string FileBaseName(const std::string& path) {
    size_t s = path.find_last_of("/\\");
    return s == std::string::npos ? path : path.substr(s + 1);
}

std::vector<std::string> ClangArgsFor(const CompileCommand& cc) {
    std::vector<std::string> out;
    if (cc.args.empty()) return out;
    std::string compiler = FileBaseName(cc.args[0]);
    bool cl_mode = EndsWithNoCase(compiler, "cl.exe") || compiler == "cl" || compiler == "clang-cl";
    if (!cc.directory.empty()) out.push_back("-working-directory=" + cc.directory);
    if (cl_mode) out.push_back("--driver-mode=cl");
    std::string file_base = FileBaseName(cc.file);
    for (size_t i = 1; i < cc.args.size(); ++i) {
        const std::string& a = cc.args[i];
        if (a == "-c" || a == "/c" || a == "-MD" || a == "-MMD" || a == "-MP" || a == "--") continue;
        if (StartsWith(a, "@")) continue;
        if (a == "-Xclang" && i + 1 < cc.args.size() && IsPluginFlag(cc.args[i + 1])) {
            const bool takes_value = PluginFlagTakesValue(cc.args[++i]);
            if (takes_value && i + 1 < cc.args.size() && cc.args[i + 1] == "-Xclang") ++i;
            if (takes_value && i + 1 < cc.args.size()) ++i;
            continue;
        }
        if (IsPluginFlag(a)) {
            if (PluginFlagTakesValue(a) && i + 1 < cc.args.size()) ++i;
            continue;
        }
        if (a == "-o" || a == "-MF" || a == "-MT" || a == "-MQ" || a == "/Fo") {
            ++i;
            continue;
        }
        if (StartsWith(a, "-o") && a.size() > 2 && a[2] != 'p') continue;
        if (StartsWith(a, "/Fo") || StartsWith(a, "/Fd") || StartsWith(a, "/Fp") ||
            StartsWith(a, "/Fe"))
            continue;
        if (!a.empty() && a[0] != '-' && a[0] != '/' && IsSourceFile(a) &&
            (a == cc.file || FileBaseName(a) == file_base || cc.file.empty()))
            continue;
        out.push_back(a);
    }
    return out;
}

std::vector<std::string> IncludeDirsOf(const CompileCommand& cc) {
    std::vector<std::string> out;
    for (size_t i = 1; i < cc.args.size(); ++i) {
        const std::string& a = cc.args[i];
        std::string dir;
        if (a == "-I" || a == "-isystem" || a == "-iquote" || a == "/I") {
            if (i + 1 < cc.args.size()) dir = cc.args[++i];
        } else if (StartsWith(a, "-I") || StartsWith(a, "/I")) {
            dir = a.substr(2);
        } else if (StartsWith(a, "-isystem")) {
            dir = a.substr(8);
        } else if (StartsWith(a, "-iquote")) {
            dir = a.substr(7);
        } else if (StartsWith(a, "-external:I")) {
            dir = a.substr(11);
        }
        if (!dir.empty()) out.push_back(JoinPath(cc.directory, dir));
    }
    return out;
}

}  // namespace binding
}  // namespace imforge
