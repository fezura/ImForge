#include <algorithm>

#include "emit_internal.h"

namespace imforge {
namespace codegen {

Result GenerateReflection(const Project& project, const Options& opts) {
    Result r;
    Writer w;
    w.opts = &opts;
    WriteFileHead(w, "reflect schema=1");
    w.Line("#pragma once");
    TypesDb db;
    LoadTypes(project, opts, db, r.diagnostics);
    if (!db.loaded) {
        Diagnostic d;
        d.level = Diagnostic::Level::Warning;
        d.message = "no types.json available (binding.typesJson); imforge_reflect.h is empty. Run imforge-reflect or write IMFORGE_REFLECT macros by hand";
        r.diagnostics.push_back(d);
        FileOut f;
        f.path = "imforge_reflect.h";
        f.content = w.out;
        r.files.push_back(f);
        return r;
    }
    w.Line("#include \"imforge/imforge_value.h\"");
    std::vector<std::string> structs = db.OrderedStructs(project.binding_types);
    for (const std::string& t : project.binding_types) {
        if (!db.FindStruct(t)) {
            Diagnostic d;
            d.level = Diagnostic::Level::Warning;
            d.message = "binding type '" + t + "' is not in types.json";
            r.diagnostics.push_back(d);
        }
    }
    std::vector<std::string> headers;
    for (const std::string& s : structs) {
        std::string h = db.HeaderOf(s);
        if (h.empty()) continue;
        bool have = false;
        for (const std::string& x : headers) have = have || x == h;
        if (!have) headers.push_back(h);
    }
    std::sort(headers.begin(), headers.end());
    for (const std::string& h : headers) w.Line("#include \"" + h + "\"");
    w.UserSlot("includes");
    std::vector<std::string> enums = db.EnumsUsedBy(structs);
    for (const std::string& e : enums) {
        const EnumInfo* ei = db.FindEnum(e);
        if (!ei || ei->values.empty()) continue;
        std::string line = "IMFORGE_REFLECT_ENUM(" + e;
        for (const auto& v : ei->values) line += ", " + v.first;
        w.Line(line + ");");
    }
    for (const std::string& s : structs) {
        const StructInfo* si = db.FindStruct(s);
        if (!si) continue;
        if (si->fields.empty()) {
            Diagnostic d;
            d.level = Diagnostic::Level::Info;
            d.message = "struct '" + s + "' has no fields; not reflected";
            r.diagnostics.push_back(d);
            continue;
        }
        if (si->fields.size() > 32) {
            Diagnostic d;
            d.level = Diagnostic::Level::Warning;
            d.message = "struct '" + s + "' has more than 32 fields; only the first 32 are reflected";
            r.diagnostics.push_back(d);
        }
        std::string line = "IMFORGE_REFLECT(" + s;
        size_t count = 0;
        for (const FieldInfo& f : si->fields) {
            if (count++ >= 32) break;
            line += ", " + f.name;
        }
        w.Line(line + ");");
    }
    w.UserSlot("reflect_extra");
    FileOut f;
    f.path = "imforge_reflect.h";
    f.content = w.out;
    r.files.push_back(f);
    return r;
}

}  // namespace codegen
}  // namespace imforge
