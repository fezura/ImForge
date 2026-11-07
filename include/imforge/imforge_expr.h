#pragma once

// Ast: Path keeps the dotted path in `text` with each `[expr]` as `[#]`, plus one Index kid per `[#]`
// holding the index expr. EnumLit is "Enum::Member". Literal keeps the source spelling in `text`.
// TypeEnv/CollectPaths/EmitCpp see the static path: literal int indices stay, anything else is `[0]`.

#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "imforge_value.h"

namespace imforge {
namespace expr {

enum class Type { Bool, Int, Float, String, Enum, Error };

struct Ast {
    enum class Kind { Literal, Path, Unary, Binary, Index, EnumLit };
    Kind kind = Kind::Literal;
    std::string text;
    Value literal;
    std::vector<Ast> kids;
    int pos = 0;
};

struct TypeEnv {
    virtual ~TypeEnv() {}
    virtual bool TypeOfPath(const std::string& path, CType& out) = 0;
    virtual bool EnumValue(const std::string& enum_name, const std::string& member, long long& out) = 0;
};

struct ValueEnv {
    virtual ~ValueEnv() {}
    virtual bool ReadPath(const std::string& path, Value& out) = 0;
    virtual bool EnumValue(const std::string&, const std::string&, long long&) { return false; }
};

struct EmitOptions {
    std::function<std::string(const std::string& path)> path_to_cpp;
    std::function<bool(const std::string& path)> path_is_char_array;
    std::function<std::string(const std::string& path)> path_to_cstr;
};

inline const char* TypeName(Type t) {
    switch (t) {
    case Type::Bool: return "bool";
    case Type::Int: return "int";
    case Type::Float: return "float";
    case Type::String: return "string";
    case Type::Enum: return "enum";
    case Type::Error: return "error";
    }
    return "error";
}

namespace detail {

inline bool SetError(Error* err, const std::string& msg, int pos) {
    if (err) {
        err->message = msg;
        err->line = 1;
        err->col = pos + 1;
    }
    return false;
}

inline Value MakeBool(bool b) { Value v; v.kind = Value::Kind::Bool; v.b = b; return v; }
inline Value MakeInt(long long i) { Value v; v.kind = Value::Kind::Int; v.i = i; return v; }
inline Value MakeFloat(double f) { Value v; v.kind = Value::Kind::Float; v.f = f; return v; }
inline Value MakeString(std::string s) { Value v; v.kind = Value::Kind::String; v.s = std::move(s); return v; }

inline const char* ValueKindName(Value::Kind k) {
    switch (k) {
    case Value::Kind::Null: return "null";
    case Value::Kind::Bool: return "bool";
    case Value::Kind::Int: return "int";
    case Value::Kind::Float: return "float";
    case Value::Kind::String: return "string";
    case Value::Kind::Vec2: return "vec2";
    case Value::Kind::Vec4: return "vec4";
    case Value::Kind::Color: return "color";
    case Value::Kind::List: return "list";
    }
    return "null";
}

inline bool IsNumeric(const Value& v) { return v.kind == Value::Kind::Int || v.kind == Value::Kind::Float; }
inline long long WrapNeg(long long a) { return (long long)(0ull - (unsigned long long)a); }
inline long long WrapAdd(long long a, long long b) { return (long long)((unsigned long long)a + (unsigned long long)b); }
inline long long WrapSub(long long a, long long b) { return (long long)((unsigned long long)a - (unsigned long long)b); }
inline long long WrapMul(long long a, long long b) { return (long long)((unsigned long long)a * (unsigned long long)b); }
inline double AsDouble(const Value& v) { return v.kind == Value::Kind::Int ? (double)v.i : v.f; }
inline bool AsBool(const Value& v) {
    switch (v.kind) {
    case Value::Kind::Bool: return v.b;
    case Value::Kind::Int: return v.i != 0;
    case Value::Kind::Float: return v.f != 0.0;
    case Value::Kind::String: return !v.s.empty();
    default: return false;
    }
}

inline std::string RenderPath(const Ast& p, const std::vector<std::string>& indices) {
    std::string s;
    size_t k = 0;
    for (size_t i = 0; i < p.text.size(); ++i) {
        if (p.text[i] == '#' && i > 0 && p.text[i - 1] == '[' && k < indices.size()) s += indices[k++];
        else s += p.text[i];
    }
    return s;
}

inline std::string StaticPath(const Ast& p) {
    std::vector<std::string> idx;
    for (const Ast& ix : p.kids) {
        const Ast& e = ix.kids.empty() ? ix : ix.kids[0];
        bool lit = e.kind == Ast::Kind::Literal && e.literal.kind == Value::Kind::Int;
        idx.push_back(lit ? std::to_string(e.literal.i) : std::string("0"));
    }
    return RenderPath(p, idx);
}

inline bool EndsWith(const std::string& s, const char* suf) {
    size_t n = std::strlen(suf);
    return s.size() >= n && s.compare(s.size() - n, n, suf) == 0;
}

inline void SplitEnum(const std::string& text, std::string& enum_name, std::string& member) {
    size_t p = text.find("::");
    enum_name = p == std::string::npos ? text : text.substr(0, p);
    member = p == std::string::npos ? std::string() : text.substr(p + 2);
}

struct TInfo {
    Type t = Type::Error;
    std::string enum_name;
};

inline bool IsNum(Type t) { return t == Type::Int || t == Type::Float; }

inline bool TypeOfCType(const CType& ct, TInfo& out) {
    switch (ct.kind) {
    case CKind::Bool: out.t = Type::Bool; return true;
    case CKind::I8: case CKind::U8: case CKind::I16: case CKind::U16:
    case CKind::I32: case CKind::U32: case CKind::I64: case CKind::U64:
        out.t = Type::Int; return true;
    case CKind::Float: case CKind::Double: out.t = Type::Float; return true;
    case CKind::StdString: case CKind::CharArray: out.t = Type::String; return true;
    case CKind::Enum:
        out.t = Type::Enum;
        out.enum_name = ct.enm && ct.enm->name ? ct.enm->name : ct.name;
        return true;
    default: return false;
    }
}

inline bool IsSizeable(CKind k) {
    return k == CKind::Vector || k == CKind::Array || k == CKind::FloatArray || k == CKind::IntArray;
}

inline bool CheckBinary(const std::string& op, const TInfo& l, const TInfo& r, TInfo& out, int pos, Error* err) {
    std::string fail = "operator '" + op + "' cannot be applied to " + TypeName(l.t) + " and " + TypeName(r.t);
    if (op == "&&" || op == "||") {
        if (l.t != Type::Bool || r.t != Type::Bool) return SetError(err, fail, pos);
        out.t = Type::Bool;
        return true;
    }
    bool is_eq = op == "==" || op == "!=";
    if (is_eq || op == "<" || op == "<=" || op == ">" || op == ">=") {
        bool ok = false;
        if (IsNum(l.t) && IsNum(r.t)) ok = true;
        else if (l.t == Type::String && r.t == Type::String) ok = true;
        else if (l.t == Type::Enum && r.t == Type::Enum) {
            if (!l.enum_name.empty() && !r.enum_name.empty() && l.enum_name != r.enum_name)
                return SetError(err, "cannot compare enum '" + l.enum_name + "' with enum '" + r.enum_name + "'", pos);
            ok = true;
        } else if ((l.t == Type::Enum && r.t == Type::Int) || (l.t == Type::Int && r.t == Type::Enum)) ok = true;
        else if (is_eq && l.t == Type::Bool && r.t == Type::Bool) ok = true;
        if (!ok) return SetError(err, fail, pos);
        out.t = Type::Bool;
        return true;
    }
    if (op == "%") {
        if (l.t != Type::Int || r.t != Type::Int) return SetError(err, fail, pos);
        out.t = Type::Int;
        return true;
    }
    if (IsNum(l.t) && IsNum(r.t)) {
        out.t = (l.t == Type::Float || r.t == Type::Float) ? Type::Float : Type::Int;
        return true;
    }
    if (op == "+" && l.t == Type::String && r.t == Type::String) {
        out.t = Type::String;
        return true;
    }
    return SetError(err, fail, pos);
}

inline bool CheckNode(const Ast& a, TypeEnv& env, TInfo& out, Error* err) {
    out = TInfo();
    switch (a.kind) {
    case Ast::Kind::Literal:
        switch (a.literal.kind) {
        case Value::Kind::Bool: out.t = Type::Bool; return true;
        case Value::Kind::Int: out.t = Type::Int; return true;
        case Value::Kind::Float: out.t = Type::Float; return true;
        case Value::Kind::String: out.t = Type::String; return true;
        default: return SetError(err, "unsupported literal", a.pos);
        }
    case Ast::Kind::EnumLit: {
        std::string e, m;
        SplitEnum(a.text, e, m);
        long long v = 0;
        if (!env.EnumValue(e, m, v)) return SetError(err, "unknown enum value '" + a.text + "'", a.pos);
        out.t = Type::Enum;
        out.enum_name = e;
        return true;
    }
    case Ast::Kind::Index:
        return SetError(err, "unexpected index", a.pos);
    case Ast::Kind::Path: {
        for (const Ast& ix : a.kids) {
            TInfo ti;
            if (ix.kids.empty()) return SetError(err, "missing index expression", ix.pos);
            if (!CheckNode(ix.kids[0], env, ti, err)) return false;
            if (ti.t != Type::Int) return SetError(err, std::string("index must be int, got ") + TypeName(ti.t), ix.pos);
        }
        std::string sp = StaticPath(a);
        CType ct;
        if (env.TypeOfPath(sp, ct)) {
            if (TypeOfCType(ct, out)) return true;
            if (!EndsWith(sp, ".size")) return SetError(err, "path '" + sp + "' is not a scalar", a.pos);
        }
        if (EndsWith(sp, ".size")) {
            CType parent;
            if (env.TypeOfPath(sp.substr(0, sp.size() - 5), parent) && IsSizeable(parent.kind)) {
                out.t = Type::Int;
                return true;
            }
        }
        return SetError(err, "unknown path '" + sp + "'", a.pos);
    }
    case Ast::Kind::Unary: {
        TInfo k;
        if (!CheckNode(a.kids[0], env, k, err)) return false;
        if (a.text == "!") {
            if (k.t != Type::Bool) return SetError(err, std::string("operator '!' requires bool, got ") + TypeName(k.t), a.pos);
            out.t = Type::Bool;
            return true;
        }
        if (!IsNum(k.t)) return SetError(err, std::string("operator '-' requires a number, got ") + TypeName(k.t), a.pos);
        out.t = k.t;
        return true;
    }
    case Ast::Kind::Binary: {
        TInfo l, r;
        if (!CheckNode(a.kids[0], env, l, err)) return false;
        if (!CheckNode(a.kids[1], env, r, err)) return false;
        return CheckBinary(a.text, l, r, out, a.pos, err);
    }
    }
    return SetError(err, "bad node", a.pos);
}

inline bool EvalBinary(const std::string& op, const Value& l, const Value& r, Value& out, int pos, Error* err) {
    std::string fail = std::string("operator '") + op + "' cannot be applied to " + ValueKindName(l.kind) + " and " + ValueKindName(r.kind);
    if (l.kind == Value::Kind::String && r.kind == Value::Kind::String) {
        int c = l.s.compare(r.s);
        if (op == "+") out = MakeString(l.s + r.s);
        else if (op == "==") out = MakeBool(c == 0);
        else if (op == "!=") out = MakeBool(c != 0);
        else if (op == "<") out = MakeBool(c < 0);
        else if (op == "<=") out = MakeBool(c <= 0);
        else if (op == ">") out = MakeBool(c > 0);
        else if (op == ">=") out = MakeBool(c >= 0);
        else return SetError(err, fail, pos);
        return true;
    }
    if (l.kind == Value::Kind::Bool && r.kind == Value::Kind::Bool) {
        if (op == "==") out = MakeBool(l.b == r.b);
        else if (op == "!=") out = MakeBool(l.b != r.b);
        else return SetError(err, fail, pos);
        return true;
    }
    if (!IsNumeric(l) || !IsNumeric(r)) return SetError(err, fail, pos);
    if (op == "%") {
        if (l.kind != Value::Kind::Int || r.kind != Value::Kind::Int) return SetError(err, fail, pos);
        if (r.i == 0) return SetError(err, "division by zero", pos);
        out = MakeInt(r.i == -1 ? 0 : l.i % r.i);
        return true;
    }
    if (l.kind == Value::Kind::Float || r.kind == Value::Kind::Float) {
        double x = AsDouble(l), y = AsDouble(r);
        if (op == "+") out = MakeFloat(x + y);
        else if (op == "-") out = MakeFloat(x - y);
        else if (op == "*") out = MakeFloat(x * y);
        else if (op == "/") {
            if (y == 0.0) return SetError(err, "division by zero", pos);
            out = MakeFloat(x / y);
        }
        else if (op == "==") out = MakeBool(x == y);
        else if (op == "!=") out = MakeBool(x != y);
        else if (op == "<") out = MakeBool(x < y);
        else if (op == "<=") out = MakeBool(x <= y);
        else if (op == ">") out = MakeBool(x > y);
        else if (op == ">=") out = MakeBool(x >= y);
        else return SetError(err, fail, pos);
        return true;
    }
    long long x = l.i, y = r.i;
    if (op == "+") out = MakeInt(WrapAdd(x, y));
    else if (op == "-") out = MakeInt(WrapSub(x, y));
    else if (op == "*") out = MakeInt(WrapMul(x, y));
    else if (op == "/") {
        if (y == 0) return SetError(err, "division by zero", pos);
        out = MakeInt(y == -1 ? WrapNeg(x) : x / y);
    }
    else if (op == "==") out = MakeBool(x == y);
    else if (op == "!=") out = MakeBool(x != y);
    else if (op == "<") out = MakeBool(x < y);
    else if (op == "<=") out = MakeBool(x <= y);
    else if (op == ">") out = MakeBool(x > y);
    else if (op == ">=") out = MakeBool(x >= y);
    else return SetError(err, fail, pos);
    return true;
}

inline bool EvalNode(const Ast& a, ValueEnv& env, Value& out, Error* err) {
    switch (a.kind) {
    case Ast::Kind::Literal:
        out = a.literal;
        return true;
    case Ast::Kind::EnumLit: {
        std::string e, m;
        SplitEnum(a.text, e, m);
        long long v = 0;
        if (env.EnumValue(e, m, v)) {
            out = MakeInt(v);
            return true;
        }
        if (env.ReadPath(a.text, out)) return true;
        return SetError(err, "unknown enum value '" + a.text + "'", a.pos);
    }
    case Ast::Kind::Index:
        return SetError(err, "unexpected index", a.pos);
    case Ast::Kind::Path: {
        std::vector<std::string> idx;
        for (const Ast& ix : a.kids) {
            Value iv;
            if (ix.kids.empty()) return SetError(err, "missing index expression", ix.pos);
            if (!EvalNode(ix.kids[0], env, iv, err)) return false;
            if (iv.kind != Value::Kind::Int) return SetError(err, std::string("index must be int, got ") + ValueKindName(iv.kind), ix.pos);
            idx.push_back(std::to_string(iv.i));
        }
        std::string path = RenderPath(a, idx);
        if (!env.ReadPath(path, out)) return SetError(err, "cannot read '" + path + "'", a.pos);
        return true;
    }
    case Ast::Kind::Unary: {
        Value v;
        if (!EvalNode(a.kids[0], env, v, err)) return false;
        if (a.text == "!") {
            if (v.kind != Value::Kind::Bool && v.kind != Value::Kind::Int)
                return SetError(err, std::string("operator '!' requires bool, got ") + ValueKindName(v.kind), a.pos);
            out = MakeBool(!AsBool(v));
            return true;
        }
        if (v.kind == Value::Kind::Int) out = MakeInt(WrapNeg(v.i));
        else if (v.kind == Value::Kind::Float) out = MakeFloat(-v.f);
        else return SetError(err, std::string("operator '-' requires a number, got ") + ValueKindName(v.kind), a.pos);
        return true;
    }
    case Ast::Kind::Binary: {
        Value l;
        if (!EvalNode(a.kids[0], env, l, err)) return false;
        if (a.text == "&&" || a.text == "||") {
            if (l.kind != Value::Kind::Bool && l.kind != Value::Kind::Int)
                return SetError(err, "operator '" + a.text + "' requires bool, got " + ValueKindName(l.kind), a.pos);
            bool lb = AsBool(l);
            if (a.text == "&&" ? !lb : lb) {
                out = MakeBool(lb);
                return true;
            }
            Value r;
            if (!EvalNode(a.kids[1], env, r, err)) return false;
            if (r.kind != Value::Kind::Bool && r.kind != Value::Kind::Int)
                return SetError(err, "operator '" + a.text + "' requires bool, got " + ValueKindName(r.kind), a.pos);
            out = MakeBool(AsBool(r));
            return true;
        }
        Value r;
        if (!EvalNode(a.kids[1], env, r, err)) return false;
        return EvalBinary(a.text, l, r, out, a.pos, err);
    }
    }
    return SetError(err, "bad node", a.pos);
}

inline int Prec(const std::string& op) {
    if (op == "||") return 1;
    if (op == "&&") return 2;
    if (op == "==" || op == "!=") return 3;
    if (op == "<" || op == "<=" || op == ">" || op == ">=") return 4;
    if (op == "+" || op == "-") return 5;
    return 6;
}

inline bool IsCmpPrec(int p) { return p == 3 || p == 4; }
inline bool IsLogicalPrec(int p) { return p <= 2; }

inline std::string CStringLiteral(const std::string& s) {
    std::string out = "\"";
    for (size_t i = 0; i < s.size(); ++i) {
        unsigned char c = (unsigned char)s[i];
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\t': out += "\\t"; break;
        case '\r': out += "\\r"; break;
        default:
            if (c < 0x20 || c == 0x7f) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\%03o", (unsigned)c);
                out += buf;
            } else {
                out += (char)c;
            }
        }
    }
    out += "\"";
    return out;
}

inline std::string FloatLiteralCpp(const Ast& a) {
    std::string t = a.text;
    if (t.empty()) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.9g", a.literal.f);
        t = buf;
    }
    if (!t.empty() && (t.back() == 'f' || t.back() == 'F')) t.pop_back();
    if (t.find_first_of(".eE") == std::string::npos) t += ".0";
    return t + "f";
}

inline std::string EmitNode(const Ast& a, const EmitOptions& o);

inline std::string EmitChild(const Ast& c, int parent_prec, bool right, const EmitOptions& o) {
    std::string s = EmitNode(c, o);
    bool wrap = false;
    if (c.kind == Ast::Kind::Unary) {
        wrap = parent_prec == 7 || (right && parent_prec == 5);
    } else if (c.kind == Ast::Kind::Binary) {
        int cp = Prec(c.text);
        if (cp < parent_prec) wrap = true;
        else if (cp == parent_prec) wrap = right || IsCmpPrec(cp);
        else wrap = (IsLogicalPrec(parent_prec) && IsLogicalPrec(cp)) || (IsCmpPrec(parent_prec) && IsCmpPrec(cp));
    }
    return wrap ? "(" + s + ")" : s;
}

inline std::string EmitPath(const Ast& a, const EmitOptions& o, bool cstr) {
    std::string sp = StaticPath(a);
    const std::function<std::string(const std::string&)>& map = cstr && o.path_to_cstr ? o.path_to_cstr : o.path_to_cpp;
    std::string cpp = map ? map(sp) : sp;
    size_t from = 0;
    for (const Ast& ix : a.kids) {
        size_t open = cpp.find('[', from);
        if (open == std::string::npos) break;
        size_t depth = 0, close = open;
        for (; close < cpp.size(); ++close) {
            if (cpp[close] == '[') ++depth;
            else if (cpp[close] == ']' && --depth == 0) break;
        }
        if (close >= cpp.size()) break;
        std::string inner = EmitNode(ix.kids[0], o);
        cpp = cpp.substr(0, open + 1) + inner + cpp.substr(close);
        from = open + 1 + inner.size() + 1;
    }
    return cpp;
}

inline bool IsCharArrayPath(const Ast& a, const EmitOptions& o) {
    return a.kind == Ast::Kind::Path && o.path_is_char_array && o.path_is_char_array(StaticPath(a));
}

inline std::string EmitCStr(const Ast& a, const EmitOptions& o) {
    if (a.kind == Ast::Kind::Path) return EmitPath(a, o, true);
    return EmitChild(a, 7, false, o);
}

inline std::string EmitNode(const Ast& a, const EmitOptions& o) {
    switch (a.kind) {
    case Ast::Kind::Literal:
        switch (a.literal.kind) {
        case Value::Kind::Bool: return a.literal.b ? "true" : "false";
        case Value::Kind::Int: return a.text.empty() ? std::to_string(a.literal.i) : a.text;
        case Value::Kind::Float: return FloatLiteralCpp(a);
        case Value::Kind::String: return CStringLiteral(a.literal.s);
        default: return "0";
        }
    case Ast::Kind::EnumLit: return a.text;
    case Ast::Kind::Index: return a.kids.empty() ? std::string() : EmitNode(a.kids[0], o);
    case Ast::Kind::Path: return EmitPath(a, o, false);
    case Ast::Kind::Unary: return a.text + EmitChild(a.kids[0], 7, false, o);
    case Ast::Kind::Binary: {
        const Ast& l = a.kids[0];
        const Ast& r = a.kids[1];
        bool is_eq = a.text == "==" || a.text == "!=";
        if (is_eq && (IsCharArrayPath(l, o) || IsCharArrayPath(r, o))) {
            std::string ls = EmitCStr(l, o);
            std::string rs = EmitCStr(r, o);
            return "strcmp(" + ls + ", " + rs + ") " + a.text + " 0";
        }
        int p = Prec(a.text);
        std::string ls = EmitChild(l, p, false, o);
        std::string rs = EmitChild(r, p, true, o);
        return ls + " " + a.text + " " + rs;
    }
    }
    return std::string();
}

inline void Collect(const Ast& a, std::vector<std::string>& out) {
    if (a.kind == Ast::Kind::Path) {
        std::string sp = StaticPath(a);
        bool seen = false;
        for (const std::string& s : out) seen = seen || s == sp;
        if (!seen) out.push_back(sp);
    }
    for (const Ast& k : a.kids) Collect(k, out);
}

}  // namespace detail

inline bool TypeCheck(const Ast& ast, TypeEnv& env, Type& result, Error* err) {
    detail::TInfo info;
    if (!detail::CheckNode(ast, env, info, err)) {
        result = Type::Error;
        return false;
    }
    result = info.t;
    return true;
}

inline bool Eval(const Ast& ast, ValueEnv& env, Value& out, Error* err) {
    out = Value();
    return detail::EvalNode(ast, env, out, err);
}

inline std::string EmitCpp(const Ast& ast, const EmitOptions& options) {
    std::string s = detail::EmitNode(ast, options);
    if (ast.kind == Ast::Kind::Binary || ast.kind == Ast::Kind::Unary) return "(" + s + ")";
    return s;
}

inline std::string EmitCpp(const Ast& ast, const std::function<std::string(const std::string& path)>& path_to_cpp) {
    EmitOptions o;
    o.path_to_cpp = path_to_cpp;
    return EmitCpp(ast, o);
}

inline std::vector<std::string> CollectPaths(const Ast& ast) {
    std::vector<std::string> out;
    detail::Collect(ast, out);
    return out;
}

inline std::string StaticPath(const Ast& path_node) { return detail::StaticPath(path_node); }

}  // namespace expr
}  // namespace imforge

#include "imforge_expr_parse.h"
#include "imforge_expr_interp.h"
