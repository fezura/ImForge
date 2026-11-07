#pragma once

// codegen and round-trip helpers for integration_tests.cpp

#include "integration_support.h"

#include <algorithm>
#include <functional>

namespace imforge {
namespace itest {

// tree walk that also descends into Component slot contents
inline void WalkAll(const Node& n, const std::function<void(const Node&)>& f) {
    f(n);
    for (const Node& c : n.children) WalkAll(c, f);
    for (const auto& s : n.slots)
        for (const Node& c : s.second) WalkAll(c, f);
}

inline void WalkAll(Node& n, const std::function<void(Node&)>& f) {
    f(n);
    for (Node& c : n.children) WalkAll(c, f);
    for (auto& s : n.slots)
        for (Node& c : s.second) WalkAll(c, f);
}

inline Node* FindNodeAll(Node& root, const std::string& id) {
    Node* found = nullptr;
    WalkAll(root, [&](Node& n) {
        if (!found && n.id == id) found = &n;
    });
    return found;
}

// stub headers from types.json
struct StubOptions {
    std::map<std::string, std::string> struct_members;   // struct name -> extra member declarations
    std::map<std::string, std::string> header_head;      // header path -> text inserted after the includes
    std::map<std::string, std::string> header_tail;      // header path -> text appended at the end
};

inline std::string UpperSnake(const std::string& s) {
    std::string r;
    for (char c : s) {
        if ((c >= 'a' && c <= 'z')) r += static_cast<char>(c - 'a' + 'A');
        else if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) r += c;
        else r += '_';
    }
    return r;
}

inline std::string FieldDecl(const std::string& type, const std::string& name) {
    size_t lb = type.find('[');
    if (lb != std::string::npos && type.back() == ']') return type.substr(0, lb) + " " + name + type.substr(lb) + ";";
    return type + " " + name + ";";
}

inline std::vector<std::string> EmitTypeStubs(const Json& types, const fs::path& out_dir, const StubOptions& so) {
    std::vector<std::string> written;
    std::map<std::string, std::string> type_header;
    const Json& structs = types["structs"];
    const Json& enums = types["enums"];
    for (const auto& kv : enums.obj) {
        std::string h = kv.second["header"].AsString();
        type_header[kv.first] = h.empty() ? kv.first + ".h" : h;
    }
    for (const auto& kv : structs.obj) {
        std::string h = kv.second["header"].AsString();
        type_header[kv.first] = h.empty() ? kv.first + ".h" : h;
    }
    std::set<std::string> headers;
    for (const auto& kv : type_header) headers.insert(kv.second);

    auto refs = [&](const Json& st) {
        std::vector<std::string> r;
        const Json& fields = st["fields"];
        for (size_t i = 0; i < fields.Size(); ++i) {
            std::string t = fields[i]["type"].AsString();
            std::string elem = fields[i]["elem"].AsString();
            for (const auto& th : type_header)
                if (t.find(th.first) != std::string::npos || elem == th.first) r.push_back(th.first);
        }
        return r;
    };

    for (const std::string& header : headers) {
        std::string text = "#pragma once\n#include \"imgui.h\"\n#include <string>\n#include <vector>\n";
        std::set<std::string> deps;
        for (const auto& kv : structs.obj)
            if (type_header[kv.first] == header)
                for (const std::string& r : refs(kv.second))
                    if (type_header[r] != header) deps.insert(type_header[r]);
        for (const std::string& d : deps) text += "#include \"" + d + "\"\n";
        text += "\n";
        auto head = so.header_head.find(header);
        if (head != so.header_head.end()) text += head->second;
        for (const auto& kv : enums.obj) {
            if (type_header[kv.first] != header) continue;
            bool scoped = kv.second["scoped"].AsBool(true);
            text += std::string("enum ") + (scoped ? "class " : "") + kv.first + " {";
            const Json& vals = kv.second["values"];
            for (size_t i = 0; i < vals.Size(); ++i) {
                const Json& v = vals[i];
                text += (i ? ", " : " ");
                if (v.IsArray()) text += v[0].AsString() + " = " + std::to_string(v[1].AsInt());
                else text += v.AsString();
            }
            text += " };\n";
        }
        std::vector<std::string> order, pending;
        for (const auto& kv : structs.obj)
            if (type_header[kv.first] == header) pending.push_back(kv.first);
        while (!pending.empty()) {
            bool progressed = false;
            for (size_t i = 0; i < pending.size(); ++i) {
                const std::string& name = pending[i];
                bool ready = true;
                for (const std::string& r : refs(structs[name.c_str()]))
                    if (r != name && type_header[r] == header && std::find(order.begin(), order.end(), r) == order.end() &&
                        !enums.Has(r.c_str()))
                        ready = false;
                if (!ready) continue;
                order.push_back(name);
                pending.erase(pending.begin() + static_cast<std::ptrdiff_t>(i));
                progressed = true;
                break;
            }
            if (!progressed) {
                order.insert(order.end(), pending.begin(), pending.end());
                pending.clear();
            }
        }
        for (const std::string& name : order) {
            const Json& st = structs[name.c_str()];
            text += "struct " + name + " {\n";
            const Json& fields = st["fields"];
            for (size_t i = 0; i < fields.Size(); ++i)
                text += "    " + FieldDecl(fields[i]["type"].AsString(), fields[i]["name"].AsString()) + "\n";
            auto extra = so.struct_members.find(name);
            if (extra != so.struct_members.end()) text += extra->second;
            text += "};\n";
        }
        auto tail = so.header_tail.find(header);
        if (tail != so.header_tail.end()) text += tail->second;
        WriteFile(out_dir / header, text);
        written.push_back(header);
    }
    for (const auto& kv : type_header) {
        std::string wrapper = kv.first + ".h";
        if (kv.second == wrapper) continue;
        WriteFile(out_dir / wrapper, "#pragma once\n#include \"" + kv.second + "\"\n");
        written.push_back(wrapper);
    }
    return written;
}

// `{icon:fa-xxx}` references in the project files -> ICON_FA_XXX macros.
inline std::set<std::string> IconMacros(const LoadedProject& lp) {
    std::set<std::string> out;
    auto scan = [&](const std::string& text) {
        size_t pos = 0;
        while ((pos = text.find("{icon:", pos)) != std::string::npos) {
            size_t end = text.find('}', pos);
            if (end == std::string::npos) break;
            std::string name = text.substr(pos + 6, end - pos - 6);
            size_t dash = name.find('-');
            std::string set = dash == std::string::npos ? "fa" : name.substr(0, dash);
            std::string rest = dash == std::string::npos ? name : name.substr(dash + 1);
            out.insert("ICON_" + UpperSnake(set) + "_" + UpperSnake(rest));
            pos = end;
        }
    };
    for (const LoadedFile& f : lp.panel_files) scan(f.text);
    for (const LoadedFile& f : lp.component_files) scan(f.text);
    return out;
}

inline void EmitIconStub(const LoadedProject& lp, const fs::path& out_dir) {
    std::string text = "#pragma once\n#define FONT_ICON_FILE_NAME_FAS \"fa-solid-900.ttf\"\n#define FONT_ICON_FILE_NAME_FAR \"fa-regular-400.ttf\"\n"
                       "#define ICON_MIN_FA 0xe005\n#define ICON_MAX_16_FA 0xf8ff\n#define ICON_MAX_FA 0xf8ff\n";
    for (const std::string& m : IconMacros(lp)) text += "#define " + m + " \"\\xef\\x80\\x80\"\n";
    WriteFile(out_dir / "IconsFontAwesome6.h", text);
    WriteFile(out_dir / "IconsFontAwesome6Brands.h", "#pragma once\n");
}

inline StubOptions ExampleStubOptions(const std::string& project) {
    StubOptions so;
    if (project == "CheatMenu") {
        so.header_tail["game/player.h"] = "\ninline int GetPing() { return 42; }\ninline const char* GetServerName() { return \"local\"; }\n";
    } else if (project == "ToolDock") {
        so.header_head["engine/log.h"] = "struct LogLine { int level = 0; std::string text; };\n"
                                         "inline ImVec4 LevelColor(int) { return ImVec4(1, 1, 1, 1); }\n\n";
        so.struct_members["LogBuffer"] = "    std::vector<LogLine> Filtered(const char*, int) const { return {}; }\n";
    }
    return so;
}

// compiler invocations
struct CompileResult {
    int rc = -1;
    int warnings = 0;
    int errors = 0;
    std::string command, output;
};

inline CompileResult SyntaxCheck(const fs::path& file, const std::vector<fs::path>& include_dirs,
                                 const std::vector<std::string>& defines, bool as_header) {
    CompileResult r;
    std::string cmd = Compiler() + " -std=c++17 -fsyntax-only -fno-exceptions -fno-rtti -Wall -Wextra";
    for (const fs::path& d : include_dirs) cmd += " -I\"" + d.string() + "\"";
    for (const std::string& d : defines) cmd += " -D" + d;
    fs::path unit = file;
    if (as_header) {
        static int counter = 0;
        unit = ScratchRoot() / "header_units" / (file.stem().string() + "_" + std::to_string(++counter) + ".cpp");
        std::string abs = fs::absolute(file).generic_string();
        WriteFile(unit, "#include \"" + abs + "\"\n");
    }
    cmd += " \"" + unit.string() + "\"";
    r.command = cmd;
    r.rc = Run(cmd, r.output);
    r.warnings = CountOccurrences(r.output, "warning:");
    r.errors = CountOccurrences(r.output, "error:");
    if (r.rc != 0 && r.errors == 0) r.errors = 1;
    return r;
}

inline std::vector<fs::path> GeneratedIncludeDirs(const fs::path& gen_dir, const fs::path& stub_dir, const fs::path& imgui_dir) {
    std::vector<fs::path> dirs = {gen_dir, stub_dir, RepoRoot() / "include", RepoRoot() / "include" / "imforge", imgui_dir,
                                  imgui_dir / "misc" / "cpp"};
    return dirs;
}

// marker-aware text edits for the round-trip test
inline bool FindBlockRange(const std::string& text, const char* kind, const std::string& id, size_t& begin, size_t& end) {
    std::string open = std::string("@imb:") + kind + " id=" + id;
    std::string close = "@imb:end id=" + id;
    size_t b = std::string::npos, search = 0;
    while ((b = text.find(open, search)) != std::string::npos) {
        char next = b + open.size() < text.size() ? text[b + open.size()] : '\n';
        if (next == ' ' || next == '\n' || next == '\r') break;
        search = b + 1;
    }
    if (b == std::string::npos) return false;
    size_t line_end = text.find('\n', b);
    if (line_end == std::string::npos) return false;
    size_t e = text.find(close, line_end);
    if (e == std::string::npos) return false;
    size_t e_line = text.rfind('\n', e);
    begin = line_end + 1;
    end = e_line == std::string::npos ? e : e_line + 1;
    return true;
}

inline std::string BlockBody(const std::string& text, const char* kind, const std::string& id) {
    size_t b, e;
    if (!FindBlockRange(text, kind, id, b, e)) return "";
    return text.substr(b, e - b);
}

inline bool ReplaceInBlock(std::string& text, const char* kind, const std::string& id,
                           const std::vector<std::pair<std::string, std::string>>& candidates) {
    size_t b, e;
    if (!FindBlockRange(text, kind, id, b, e)) return false;
    for (const auto& c : candidates) {
        size_t pos = text.find(c.first, b);
        if (pos == std::string::npos || pos >= e) continue;
        text.replace(pos, c.first.size(), c.second);
        return true;
    }
    return false;
}

inline bool InsertLineAtBlockEnd(std::string& text, const char* kind, const std::string& id, const std::string& line) {
    size_t b, e;
    if (!FindBlockRange(text, kind, id, b, e)) return false;
    text.insert(e, line + "\n");
    return true;
}

inline bool ContainsInBlock(const std::string& text, const char* kind, const std::string& id, const std::string& needle) {
    return BlockBody(text, kind, id).find(needle) != std::string::npos;
}

inline bool HasFlag(const Node& n, const char* prop, const char* flag) {
    const PropValue* p = n.Prop(prop);
    if (!p || !p->raw.IsArray()) return false;
    for (const Json& f : p->raw.arr)
        if (f.IsString() && f.AsString() == flag) return true;
    return false;
}

}  // namespace itest
}  // namespace imforge
