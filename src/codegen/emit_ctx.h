#pragma once

// internal emitter state, used by the panel/container/component and widget emitters
//
// When writing a widget emitter:
//  * call every helper before w.Line() for the line that uses it. Some (interpolated labels)
//    write a statement via PreLine() as soon as they're called.
//  * literal helpers embed an invisible marker that the Writer strips to record the LiteralRef.
//    Adding text around the result is fine, cutting it with substr/erase is not.
//  * ctx.scale_expr is "imb_s", "imb_scale" or "1.0f", go through Px()/Scaled() instead.
//  * emit the API of ctx.opts.imgui_version. Only add #if IMGUI_VERSION_NUM >= N branches when
//    the target is older than the API and there's a fallback.

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "codegen.h"
#include "imforge/imforge_expr.h"

namespace imforge {
namespace codegen {

struct TypesDb;

struct Writer {
    std::string out;
    int indent = 0;
    const Options* opts = nullptr;

    struct Mark {
        std::string node_id, prop;
        size_t offset = std::string::npos;
        bool next_literal = false;
    };
    std::vector<Mark> marks;

    struct Block {
        std::string id, kind;
        size_t begin_off = 0, body_off = 0, end_off = 0, close_off = 0, hash_off = 0;
        int parent = -1;
        bool closed = false;
        std::vector<int> children;
    };
    std::vector<Block> blocks;
    std::vector<int> open_blocks;
    int suppress_markers = 0;

    struct Scope {
        Writer* w;
        explicit Scope(Writer& wr) : w(&wr) { ++w->indent; }
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;
        ~Scope() { --w->indent; }
    };

    void Line(const std::string& text);
    void Raw(const std::string& text);
    void Blank();
    void Indent() { ++indent; }
    void Unindent() { --indent; }
    Scope Scoped() { return Scope(*this); }
    std::string IndentString() const;
    size_t Offset() const { return out.size(); }
    int TokenIndex() const;

    bool MarkersOn() const;
    void BeginBlock(const std::string& id, const char* kind = "begin");
    void EndBlock();
    void UserSlot(const std::string& id);
    std::string MarkToken(const std::string& node_id, const std::string& prop);
    void MarkNextLiteral(const std::string& node_id, const std::string& prop);
    static std::string StripMarks(const std::string& text);

    void Prepend(const std::string& text);
    void InsertAt(size_t offset, const std::string& text);
    void Finalize(const std::string& file, std::vector<LiteralRef>& out) const;
};

struct TextFmt {
    std::string fmt;                // complete C++ expression of type const char* (string literal,
                                    // possibly concatenated with ICON_* macros); '%' already doubled
    std::vector<std::string> args;  // printf arguments, already cast
    bool plain = true;              // no arguments
    bool IsLiteral() const { return args.empty(); }
    std::string CallArgs() const;   // `fmt, a, b` for ImGui::Text(...)
};
using FmtArgs = TextFmt;

struct BindInfo {
    std::string cpp;       // lvalue: "player.health", "(*value)" inside a component
    std::string pointer;   // "&player.health"; arrays "player.pos"; vectors "player.v.data()";
                           // enums "(int*)&cfg.mode"
    std::string count;     // element count for arrays/vectors ("IM_ARRAYSIZE(x)", "(int)x.size()")
    CType type;            // kind Unknown when the path could not be typed
    bool ok = false;       // slot bound and mapped to C++
    bool inferred = false; // type guessed from the slot's first accepted bind type
};
using BindExpr = BindInfo;

struct EmitCtx {
    const Project& project;
    const Panel* panel;
    const Component* component;
    const Options& opts;
    Writer& w;
    std::vector<LiteralRef>& literals;
    std::vector<Diagnostic>& diags;
    std::string state_var;
    std::string scale_expr;

    const TypesDb* types = nullptr;
    const Node* parent = nullptr;
    std::string fill_w;
    std::string pending_fill;
    std::vector<std::string> includes;
    std::vector<std::string> warned_paths;
    bool needs_stdlib = false;
    bool uses_runtime = false;
    bool uses_fx = false;
    std::vector<const Node*> split_chain;
    bool tab_targets_ready = false;
    std::vector<std::string> tab_targets;
    std::map<const void*, bool> hstack_has_fill;
    std::map<std::pair<const void*, std::string>, std::map<std::string, std::vector<std::pair<std::string, bool>>>> label_cache;

    EmitCtx(const Project& project, const Panel* panel, const Component* component,
            const Options& opts, Writer& w, std::vector<LiteralRef>& literals,
            std::vector<Diagnostic>& diags);

    // oldest ImGui the output has to build against; anything newer goes behind
    // #if IMGUI_VERSION_NUM >= N so generated code still compiles on 1.85+
    int Version() const { return opts.imgui_version < kMinImGuiVersion ? opts.imgui_version : kMinImGuiVersion; }
    // project's ImGui version, newer features get dropped with a warning
    int Target() const { return opts.imgui_version; }
    static constexpr int kMinImGuiVersion = 18500;
    std::string OwnerName() const;
    const WidgetDef* Def(const Node&) const;

    bool Has(const Node&, const char* prop) const;
    bool IsExpr(const Node&, const char* prop) const;
    const Json& PropJson(const Node&, const char* prop) const;

    TextFmt Text(const Node&, const char* prop, const char* def);
    TextFmt TextOf(const Node&, const char* prop, const std::string& raw);
    // non-printf C string: a literal or ICON_* concatenation, or with {expr} segments the name
    // of a char buffer that a snprintf PreLine fills
    std::string CStr(const Node&, const char* prop, const std::string& raw, const char* buf_suffix);
    bool CStrIsLiteral(const std::string& raw) const;
    std::string Str(const Node&, const char* prop, const char* def);
    std::string Label(const Node&, const char* prop = "label");
    std::string Num(const Node&, const char* prop, double def, bool is_float);
    bool Bool(const Node&, const char* prop, bool def);
    std::string BoolLit(const Node&, const char* prop, bool def);
    std::string Color(const Node&, const char* prop, const char* def);
    std::string ColorU32(const Node&, const char* prop, const char* def);
    std::string Vec2(const Node&, const char* prop, const char* def_json, bool scaled = false);
    std::string Vec2(const Node&, const char* prop, double dx, double dy, bool scaled = false);
    std::string Size(const Node&, const char* prop = "size");
    // "A | B" or "0". Flags newer than Version() go into an int <id>_<prop> built by guarded
    // PreLines (#if ... x |= ...; #endif) and we return that variable's name instead.
    std::string Flags(const Node&, const char* prop, const char* enum_name);
    std::vector<std::pair<std::string, int>> FlagList(const Node&, const char* prop, const char* enum_name);
    std::string FlagsExpr(const Node&, const char* prop, const std::vector<std::pair<std::string, int>>& flags, int floor,
                          std::vector<std::string>& pre_lines, const std::string& var);
    std::string EnumValue(const Node&, const char* prop, const char* def);
    std::string EnumValue(const Node&, const char* prop, const char* prefix, const char* def);
    std::vector<std::string> StringList(const Node&, const char* prop);

    std::string Expr(const std::string& expr);
    std::string ExprCStr(const std::string& expr);
    expr::Type ExprType(const std::string& expr);
    std::string Path(const std::string& path, CType* type_out = nullptr);
    BindInfo Bind(const Node&, const char* slot);
    BindInfo BindPath(const std::string& path, const char* accepted_types);

    std::string Px(double px) const;
    std::string Scaled(const std::string& literal) const;
    std::string Id(const Node&) const;
    std::string Local(const Node&, const char* suffix) const;
    std::string FloatLit(double v) const;
    std::string IntLit(long long v) const;
    std::string StrLit(const std::string& s) const;
    std::string ColorLit(const Vec4& c) const;
    std::string ColorU32Lit(const Vec4& c) const;
    std::string IconMacro(const std::string& icon_name);

    void PreLine(const std::string& line);
    void MarkLiteral(const Node&, const char* prop);
    void Include(const std::string& header);
    void EmitChildren(const Node& n);

    bool HasEvent(const Node&, const char* event) const;
    void FireEvent(const Node&, const char* event);
    std::string HandlerName(const Node&, const char* event) const;
    std::string HandlerArgs() const;

    void Diag(Diagnostic::Level, const Node*, const char* prop, const std::string& msg);
    void Diag(Diagnostic::Level level, const Node& n, const std::string& msg, const char* prop = nullptr) {
        Diag(level, &n, prop, msg);
    }
    void Warn(const Node& n, const std::string& msg) { Diag(Diagnostic::Level::Warning, &n, nullptr, msg); }
    void Info(const Node& n, const std::string& msg) { Diag(Diagnostic::Level::Info, &n, nullptr, msg); }
    void Fail(const Node& n, const std::string& msg) { Diag(Diagnostic::Level::Error, &n, nullptr, msg); }
};

bool EmitStockWidget(EmitCtx&, const Node&);
bool EmitCustomWidget(EmitCtx&, const Node&, const CustomWidgetDef&);
void EmitNode(EmitCtx&, const Node&);
void EmitChildren(EmitCtx&, const Node&);

}  // namespace codegen
}  // namespace imforge
