#include "clang_scanner.h"

#include <cstdlib>
#include <set>

#include "compile_commands.h"
#include "type_database.h"

#ifdef IMFORGE_HAVE_LIBCLANG
#include <clang-c/Index.h>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <direct.h>
#include <stdlib.h>
#else
#include <dlfcn.h>
#endif
#endif

namespace imforge {
namespace binding {

bool HaveLibClang() {
#ifdef IMFORGE_HAVE_LIBCLANG
    return true;
#else
    return false;
#endif
}

#ifndef IMFORGE_HAVE_LIBCLANG

const char* LibClangVersion() { return "unavailable"; }

bool ScanTypes(const ScanOptions&, Json&, std::vector<std::string>& log) {
    log.push_back("imforge-reflect was built without libclang: install LLVM (libclang) and rebuild "
                  "with IMFORGE_WITH_LIBCLANG=ON, or use --from-json <types.json> / hand-written "
                  "IMFORGE_REFLECT macros");
    return false;
}

#else

namespace {

std::string Str(CXString s) {
    const char* c = clang_getCString(s);
    std::string r = c ? c : "";
    clang_disposeString(s);
    return r;
}

std::string AbsPath(const std::string& p) {
    if (p.empty()) return p;
#ifdef _WIN32
    char buf[_MAX_PATH];
    if (_fullpath(buf, p.c_str(), sizeof(buf))) return NormalizePath(buf);
#else
    char buf[4096];
    if (realpath(p.c_str(), buf)) return NormalizePath(buf);
#endif
    return NormalizePath(p);
}

bool PathPrefix(const std::string& path, const std::string& dir) {
    if (dir.empty() || path.size() <= dir.size()) return false;
    for (size_t i = 0; i < dir.size(); ++i) {
        char a = path[i], b = dir[i];
#ifdef _WIN32
        if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
#endif
        if (a != b) return false;
    }
    return path[dir.size()] == '/';
}

bool HasArg(const std::vector<std::string>& args, const char* prefix) {
    size_t n = std::strlen(prefix);
    for (const std::string& a : args)
        if (a.compare(0, n, prefix) == 0) return true;
    return false;
}

// clang_getOffsetOfBase showed up in LLVM 20 without a CINDEX_VERSION_MINOR bump, so look it up
// at runtime unless IMFORGE_LIBCLANG_HAS_OFFSETOFBASE is set
typedef long long (*OffsetOfBaseFn)(CXCursor, CXCursor);

OffsetOfBaseFn FindOffsetOfBase() {
#ifdef IMFORGE_LIBCLANG_HAS_OFFSETOFBASE
    return &clang_getOffsetOfBase;
#elif defined(_WIN32)
    HMODULE m = GetModuleHandleA("libclang.dll");
    if (!m) return nullptr;
    return reinterpret_cast<OffsetOfBaseFn>(reinterpret_cast<void*>(GetProcAddress(m, "clang_getOffsetOfBase")));
#else
    return reinterpret_cast<OffsetOfBaseFn>(dlsym(RTLD_DEFAULT, "clang_getOffsetOfBase"));
#endif
}

struct Scanner {
    const ScanOptions& opt;
    std::vector<std::string>& log;
    TypeDatabase db;
    std::set<std::string> wanted;
    std::set<std::string> done;
    std::vector<std::string> include_dirs;
    std::vector<std::string> given_headers;
    std::vector<CXCursor> work;
    std::string cur_dir;
    OffsetOfBaseFn offset_of_base;

    Scanner(const ScanOptions& o, std::vector<std::string>& l)
        : opt(o), log(l), offset_of_base(FindOffsetOfBase()) {}

    void Log(const std::string& s) { log.push_back(s); }

    void AddIncludeDir(const std::string& d) {
        std::string a = AbsPath(d);
        while (!a.empty() && a.back() == '/') a.pop_back();
        for (const std::string& e : include_dirs)
            if (e == a) return;
        include_dirs.push_back(a);
    }

    static bool IsInline(CXCursor ns) {
#if CINDEX_VERSION_MINOR >= 59
        return clang_Cursor_isInlineNamespace(ns) != 0;
#else
        std::string s = Str(clang_getCursorSpelling(ns));
        return s == "__1" || s == "__cxx11" || s == "__ndk1";
#endif
    }

    std::string QualifiedName(CXCursor c) {
        std::string name = Str(clang_getCursorSpelling(c));
        if (name.empty()) return name;
        for (CXCursor p = clang_getCursorSemanticParent(c); !clang_Cursor_isNull(p);
             p = clang_getCursorSemanticParent(p)) {
            CXCursorKind k = clang_getCursorKind(p);
            if (k == CXCursor_Namespace) {
                if (IsInline(p)) continue;
                std::string s = Str(clang_getCursorSpelling(p));
                if (s.empty()) continue;
                name = s + "::" + name;
            } else if (k == CXCursor_StructDecl || k == CXCursor_ClassDecl ||
                       k == CXCursor_UnionDecl || k == CXCursor_ClassTemplate) {
                name = Str(clang_getCursorSpelling(p)) + "::" + name;
            } else {
                break;
            }
        }
        return name;
    }

    bool InStd(CXCursor c) {
        for (CXCursor p = clang_getCursorSemanticParent(c); !clang_Cursor_isNull(p);
             p = clang_getCursorSemanticParent(p)) {
            CXCursorKind k = clang_getCursorKind(p);
            if (k == CXCursor_Namespace) {
                if (Str(clang_getCursorSpelling(p)) == "std") return true;
            } else if (k != CXCursor_StructDecl && k != CXCursor_ClassDecl &&
                       k != CXCursor_ClassTemplate) {
                return false;
            }
        }
        return false;
    }

    std::string HeaderOf(CXCursor c) {
        CXFile file = nullptr;
        clang_getFileLocation(clang_getCursorLocation(c), &file, nullptr, nullptr, nullptr);
        if (!file) return "";
        std::string given = NormalizePath(Str(clang_getFileName(file)));
        std::string real = NormalizePath(Str(clang_File_tryGetRealPathName(file)));
        std::string candidates[4] = {real, AbsPath(JoinPath(cur_dir, given)), AbsPath(given), given};
        for (const std::string& cand : candidates) {
            if (cand.empty()) continue;
            for (const std::string& dir : include_dirs)
                if (PathPrefix(cand, dir)) return cand.substr(dir.size() + 1);
        }
        for (const std::string& h : given_headers) {
            std::string ah = AbsPath(h);
            if (h == given || ah == candidates[0] || ah == candidates[1] || ah == candidates[2]) return h;
        }
        return real.empty() ? given : real;
    }

    void Enqueue(CXCursor decl) {
        CXCursor def = clang_getCursorDefinition(decl);
        if (clang_Cursor_isNull(def)) def = decl;
        std::string q = QualifiedName(def);
        if (q.empty() || done.count(q)) return;
        done.insert(q);
        work.push_back(def);
    }

    void Set(FieldDesc& fd, const std::string& type, CKind kind) {
        fd.type = type;
        fd.ctype = CKindName(kind);
    }

    bool MapType(CXType t, FieldDesc& fd, std::string& why) {
        for (CXType cur = t;;) {
            if (cur.kind == CXType_Elaborated) {
                cur = clang_Type_getNamedType(cur);
                continue;
            }
            if (cur.kind == CXType_Typedef) {
                std::string tn = Str(clang_getTypedefName(cur));
                if (tn == "ImTextureID") {
                    Set(fd, "ImTextureID", CKind::TexID);
                    return true;
                }
                cur = clang_getTypedefDeclUnderlyingType(clang_getTypeDeclaration(cur));
                continue;
            }
            break;
        }
        CXType canon = clang_getCanonicalType(t);
        std::string cspell = NormalizeTypeName(Str(clang_getTypeSpelling(canon)));
        switch (canon.kind) {
        case CXType_Bool: Set(fd, "bool", CKind::Bool); return true;
        case CXType_Char_U:
        case CXType_UChar: Set(fd, cspell, CKind::U8); return true;
        case CXType_Char_S:
        case CXType_SChar: Set(fd, cspell, CKind::I8); return true;
        case CXType_Short: Set(fd, cspell, CKind::I16); return true;
        case CXType_UShort: Set(fd, cspell, CKind::U16); return true;
        case CXType_Int: Set(fd, cspell, CKind::I32); return true;
        case CXType_UInt: Set(fd, cspell, CKind::U32); return true;
        case CXType_Long:
            Set(fd, cspell, clang_Type_getSizeOf(canon) == 4 ? CKind::I32 : CKind::I64);
            return true;
        case CXType_ULong:
            Set(fd, cspell, clang_Type_getSizeOf(canon) == 4 ? CKind::U32 : CKind::U64);
            return true;
        case CXType_LongLong: Set(fd, cspell, CKind::I64); return true;
        case CXType_ULongLong: Set(fd, cspell, CKind::U64); return true;
        case CXType_Float: Set(fd, "float", CKind::Float); return true;
        case CXType_Double: Set(fd, "double", CKind::Double); return true;
        case CXType_Enum: {
            CXCursor decl = clang_getTypeDeclaration(canon);
            std::string q = QualifiedName(decl);
            if (q.empty()) {
                why = "anonymous enum";
                return false;
            }
            Set(fd, q, CKind::Enum);
            Enqueue(decl);
            return true;
        }
        case CXType_Record: {
            CXCursor decl = clang_getTypeDeclaration(canon);
            std::string q = QualifiedName(decl);
            std::string bare = Str(clang_getCursorSpelling(decl));
            if (q == "ImVec2") {
                Set(fd, q, CKind::Vec2);
                return true;
            }
            if (q == "ImVec4") {
                Set(fd, q, CKind::Vec4);
                return true;
            }
            if (q == "ImTextureRef") {
                Set(fd, q, CKind::TexID);
                return true;
            }
            if (InStd(decl)) {
                if (bare == "basic_string") {
                    CXType a0 = clang_Type_getTemplateArgumentAsType(canon, 0);
                    if (a0.kind == CXType_Char_S || a0.kind == CXType_Char_U) {
                        Set(fd, "std::string", CKind::StdString);
                        return true;
                    }
                    why = "std::basic_string of non-char is not supported";
                    return false;
                }
                if (bare == "vector") {
                    CXType a0 = clang_Type_getTemplateArgumentAsType(canon, 0);
                    FieldDesc e;
                    if (!MapType(a0, e, why)) {
                        why = "std::vector element: " + why;
                        return false;
                    }
                    fd.ctype = "Vector";
                    fd.elem = e.type;
                    fd.type = "std::vector<" + e.type + ">";
                    return true;
                }
                why = "std::" + bare + " is not supported";
                return false;
            }
            if (clang_getCursorKind(decl) == CXCursor_UnionDecl) {
                why = "unions are not supported";
                return false;
            }
            if (q.empty()) {
                why = "anonymous struct";
                return false;
            }
            Set(fd, q, CKind::Struct);
            Enqueue(decl);
            return true;
        }
        case CXType_ConstantArray: {
            CXType et = clang_getArrayElementType(canon);
            long long n = clang_getArraySize(canon);
            CXType ec = clang_getCanonicalType(et);
            std::string dim = "[" + std::to_string(n) + "]";
            fd.count = static_cast<int>(n);
            if (ec.kind == CXType_Char_S || ec.kind == CXType_Char_U) {
                Set(fd, "char" + dim, CKind::CharArray);
                return true;
            }
            if (ec.kind == CXType_Float) {
                Set(fd, "float" + dim, CKind::FloatArray);
                return true;
            }
            if (ec.kind == CXType_Int) {
                Set(fd, "int" + dim, CKind::IntArray);
                return true;
            }
            FieldDesc e;
            if (!MapType(et, e, why)) {
                why = "array element: " + why;
                return false;
            }
            fd.ctype = "Array";
            fd.elem = e.type;
            fd.type = e.type + dim;
            return true;
        }
        case CXType_Pointer:
        case CXType_LValueReference:
        case CXType_RValueReference:
        case CXType_MemberPointer:
        case CXType_BlockPointer:
            why = "pointer/reference members are not supported (" + cspell + ")";
            return false;
        case CXType_FunctionProto:
        case CXType_FunctionNoProto: why = "function type"; return false;
        case CXType_IncompleteArray:
        case CXType_VariableArray:
        case CXType_DependentSizedArray: why = "array of unknown size"; return false;
        default: Set(fd, cspell.empty() ? "unknown" : cspell, CKind::Unknown); return true;
        }
    }

    struct FieldCtx {
        Scanner* self;
        CXCursor record;
        long long base_offset;
        std::string owner;
        StructDesc* out;
    };

    static CXChildVisitResult FieldVisitor(CXCursor c, CXCursor, CXClientData data) {
        FieldCtx* ctx = static_cast<FieldCtx*>(data);
        ctx->self->VisitMember(c, *ctx);
        return CXChildVisit_Continue;
    }

    void CollectFields(CXCursor record, long long base_offset, const std::string& owner,
                       StructDesc& out) {
        FieldCtx ctx{this, record, base_offset, owner, &out};
        clang_visitChildren(record, &FieldVisitor, &ctx);
    }

    void VisitMember(CXCursor c, FieldCtx& ctx) {
        CXCursorKind k = clang_getCursorKind(c);
        if (k == CXCursor_CXXBaseSpecifier) {
            std::string bname = NormalizeTypeName(Str(clang_getTypeSpelling(clang_getCursorType(c))));
            if (clang_getCXXAccessSpecifier(c) != CX_CXXPublic) {
                Log("skipping non-public base '" + bname + "' of '" + ctx.owner + "'");
                return;
            }
            if (clang_isVirtualBase(c)) {
                Log("skipping virtual base '" + bname + "' of '" + ctx.owner + "'");
                return;
            }
            CXCursor bdef = clang_getCursorDefinition(clang_getTypeDeclaration(clang_getCursorType(c)));
            if (clang_Cursor_isNull(bdef)) {
                Log("skipping base '" + bname + "' of '" + ctx.owner + "': no definition");
                return;
            }
            if (!offset_of_base) {
                Log("skipping fields of base '" + bname + "' of '" + ctx.owner +
                    "': this libclang (< 20) cannot compute base offsets; reflect the base type "
                    "and bind to it directly");
                return;
            }
            long long off = offset_of_base(ctx.record, c);
            if (off < 0) {
                Log("skipping fields of base '" + bname + "' of '" + ctx.owner + "': unknown layout");
                return;
            }
            CollectFields(bdef, ctx.base_offset + off / 8, ctx.owner, *ctx.out);
            return;
        }
        if (k != CXCursor_FieldDecl) return;
        std::string name = Str(clang_getCursorSpelling(c));
        std::string full = ctx.owner + "::" + name;
        CX_CXXAccessSpecifier access = clang_getCXXAccessSpecifier(c);
        if (access == CX_CXXPrivate || access == CX_CXXProtected) {
            Log("skipping " + full + ": " + (access == CX_CXXPrivate ? "private" : "protected") +
                " member");
            return;
        }
        if (clang_Cursor_isBitField(c)) {
            Log("skipping " + full + ": bit-field");
            return;
        }
        FieldDesc fd;
        std::string why;
        if (!MapType(clang_getCursorType(c), fd, why)) {
            Log("skipping " + full + ": " + why);
            return;
        }
        if (fd.ctype == "Unknown")
            Log("field " + full + " has unsupported type '" + fd.type + "' (kept as Unknown)");
        fd.name = name;
        long long off = clang_Cursor_getOffsetOfField(c);
        fd.offset = off >= 0 ? ctx.base_offset + off / 8 : -1;
        ctx.out->fields.push_back(std::move(fd));
    }

    void ProcessRecord(CXCursor def) {
        std::string q = QualifiedName(def);
        if (!clang_isCursorDefinition(def)) {
            Log("type '" + q + "' is only forward-declared here");
            done.erase(q);
            return;
        }
        StructDesc d;
        d.name = q;
        d.header = HeaderOf(def);
        long long sz = clang_Type_getSizeOf(clang_getCursorType(def));
        d.size = sz > 0 ? sz : 0;
        CollectFields(def, 0, q, d);
        if (d.fields.empty()) Log("struct '" + q + "' has no reflectable fields");
        db.AddStruct(d);
        MarkFound(def, q);
    }

    static CXChildVisitResult EnumVisitor(CXCursor c, CXCursor, CXClientData data) {
        if (clang_getCursorKind(c) == CXCursor_EnumConstantDecl) {
            EnumDesc* d = static_cast<EnumDesc*>(data);
            d->values.emplace_back(Str(clang_getCursorSpelling(c)), clang_getEnumConstantDeclValue(c));
        }
        return CXChildVisit_Continue;
    }

    void ProcessEnum(CXCursor def) {
        std::string q = QualifiedName(def);
        if (!clang_isCursorDefinition(def)) {
            Log("enum '" + q + "' is only forward-declared here");
            done.erase(q);
            return;
        }
        EnumDesc d;
        d.name = q;
        d.header = HeaderOf(def);
        d.scoped = clang_EnumDecl_isScoped(def) != 0;
        d.underlying = NormalizeTypeName(Str(clang_getTypeSpelling(clang_getEnumDeclIntegerType(def))));
        clang_visitChildren(def, &EnumVisitor, &d);
        db.AddEnum(d);
        MarkFound(def, q);
    }

    void MarkFound(CXCursor def, const std::string& q) {
        wanted.erase(q);
        wanted.erase(Str(clang_getCursorSpelling(def)));
    }

    bool Wanted(CXCursor c) {
        if (wanted.empty()) return false;
        if (wanted.count(Str(clang_getCursorSpelling(c)))) return true;
        return wanted.count(QualifiedName(c)) != 0;
    }

    static CXChildVisitResult FindVisitor(CXCursor c, CXCursor, CXClientData data) {
        Scanner* self = static_cast<Scanner*>(data);
        CXCursorKind k = clang_getCursorKind(c);
        switch (k) {
        case CXCursor_Namespace:
        case CXCursor_LinkageSpec:
        case CXCursor_UnexposedDecl: clang_visitChildren(c, &FindVisitor, data); break;
        case CXCursor_StructDecl:
        case CXCursor_ClassDecl:
        case CXCursor_EnumDecl:
            if (clang_isCursorDefinition(c)) {
                if (self->Wanted(c)) self->Enqueue(c);
                if (k != CXCursor_EnumDecl) clang_visitChildren(c, &FindVisitor, data);
            }
            break;
        case CXCursor_ClassTemplate:
            if (self->Wanted(c))
                self->Log("type '" + self->QualifiedName(c) + "' is a class template; not supported");
            break;
        default: break;
        }
        return CXChildVisit_Continue;
    }

    bool ParseUnit(CXIndex idx, const std::string& file, const std::vector<std::string>& args) {
        std::vector<const char*> argv;
        for (const std::string& a : args) argv.push_back(a.c_str());
        CXTranslationUnit tu = nullptr;
        unsigned flags = CXTranslationUnit_SkipFunctionBodies | CXTranslationUnit_KeepGoing |
                         CXTranslationUnit_Incomplete;
        CXErrorCode ec = clang_parseTranslationUnit2(idx, file.c_str(), argv.data(),
                                                     static_cast<int>(argv.size()), nullptr, 0,
                                                     flags, &tu);
        if (ec != CXError_Success || !tu) {
            Log("failed to parse '" + file + "' (libclang error " + std::to_string(ec) + ")");
            return false;
        }
        unsigned n = clang_getNumDiagnostics(tu);
        int errors = 0;
        for (unsigned i = 0; i < n; ++i) {
            CXDiagnostic d = clang_getDiagnostic(tu, i);
            if (clang_getDiagnosticSeverity(d) >= CXDiagnostic_Error) {
                if (errors < 20)
                    Log(Str(clang_formatDiagnostic(d, CXDiagnostic_DisplaySourceLocation |
                                                          CXDiagnostic_DisplayColumn)));
                ++errors;
            }
            clang_disposeDiagnostic(d);
        }
        if (errors > 20) Log("... " + std::to_string(errors - 20) + " more errors in '" + file + "'");
        clang_visitChildren(clang_getTranslationUnitCursor(tu), &FindVisitor, this);
        while (!work.empty()) {
            CXCursor c = work.back();
            work.pop_back();
            if (clang_getCursorKind(c) == CXCursor_EnumDecl) ProcessEnum(c);
            else ProcessRecord(c);
        }
        clang_disposeTranslationUnit(tu);
        return true;
    }

    bool Run(Json& out) {
        if (opt.types.empty()) {
            Log("no types requested (--types)");
            return false;
        }
        for (const std::string& t : opt.types) {
            std::string n = NormalizeTypeName(t);
            if (!n.empty()) wanted.insert(n);
        }
        for (const std::string& d : opt.include_dirs) AddIncludeDir(d);
        for (const std::string& h : opt.headers) given_headers.push_back(NormalizePath(h));
        std::vector<CompileCommand> ccs;
        if (!opt.compile_commands.empty()) {
            std::string err;
            if (!LoadCompileCommands(opt.compile_commands, ccs, &err)) {
                Log(err);
                return false;
            }
            if (ccs.empty()) Log("'" + opt.compile_commands + "' has no entries");
            for (const CompileCommand& cc : ccs)
                for (const std::string& d : IncludeDirsOf(cc)) AddIncludeDir(d);
        }
        struct Unit {
            std::string file, dir;
            std::vector<std::string> args;
        };
        std::vector<Unit> units;
        if (!opt.headers.empty()) {
            for (const std::string& h : opt.headers) {
                std::vector<std::string> args;
                std::string dir;
                if (!ccs.empty()) {
                    const CompileCommand* pick = &ccs[0];
                    std::string hdir = NormalizePath(h.substr(0, h.find_last_of("/\\") + 1));
                    for (const CompileCommand& cc : ccs) {
                        std::string f = NormalizePath(cc.file);
                        if (!hdir.empty() && f.compare(0, hdir.size(), hdir) == 0) {
                            pick = &cc;
                            break;
                        }
                    }
                    args = ClangArgsFor(*pick);
                    dir = pick->directory;
                }
                if (!HasArg(args, "-x")) {
                    args.push_back("-x");
                    args.push_back("c++");
                }
                if (!HasArg(args, "-std=") && !HasArg(args, "/std:")) args.push_back("-std=" + opt.std);
                for (const std::string& d : opt.include_dirs) args.push_back("-I" + d);
                for (const std::string& d : opt.defines) args.push_back("-D" + d);
                units.push_back(Unit{h, dir, args});
            }
        } else if (!ccs.empty()) {
            std::set<std::string> seen;
            for (const CompileCommand& cc : ccs) {
                std::string f = JoinPath(cc.directory, cc.file);
                if (!seen.insert(f).second) continue;
                std::vector<std::string> args = ClangArgsFor(cc);
                for (const std::string& d : opt.include_dirs) args.push_back("-I" + d);
                for (const std::string& d : opt.defines) args.push_back("-D" + d);
                units.push_back(Unit{f, cc.directory, args});
            }
        } else {
            Log("nothing to scan: pass --compile-commands <file> and/or --header <file>");
            return false;
        }
        CXIndex idx = clang_createIndex(0, 0);
        int parsed = 0;
        for (const Unit& u : units) {
            cur_dir = u.dir;
            if (ParseUnit(idx, u.file, u.args)) ++parsed;
            if (wanted.empty() && !opt.headers.empty()) break;
        }
        clang_disposeIndex(idx);
        if (parsed == 0) return false;
        out = db.Save();
        bool ok = true;
        for (const std::string& w : wanted) {
            Log("requested type '" + w + "' was not found");
            ok = false;
        }
        return ok && !db.Empty();
    }
};

}  // namespace

const char* LibClangVersion() {
    static std::string v = Str(clang_getClangVersion());
    return v.c_str();
}

bool ScanTypes(const ScanOptions& options, Json& types_out, std::vector<std::string>& log) {
    Scanner s(options, log);
    return s.Run(types_out);
}

#endif

}  // namespace binding
}  // namespace imforge
