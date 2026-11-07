#pragma once

#include <cctype>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

#include "imforge_expr.h"

namespace imforge {
namespace expr {
namespace detail {

enum class Tok {
    End, Int, Float, Str, Ident, True, False, ColonColon, LParen, RParen, LBracket, RBracket,
    Dot, Or, And, Eq, Ne, Lt, Le, Gt, Ge, Plus, Minus, Star, Slash, Percent, Not
};

struct Token {
    Tok kind = Tok::End;
    std::string text;
    int pos = 0;
};

const size_t kMaxTokens = 2048;
const int kMaxDepth = 64;

inline bool IsDigit(char c) { return c >= '0' && c <= '9'; }
inline bool IsIdentStart(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
inline bool IsIdentChar(char c) { return IsIdentStart(c) || IsDigit(c); }

inline bool LexNumber(const std::string& src, size_t& i, std::vector<Token>& out, Error* err) {
    size_t n = src.size(), j = i;
    int pos = (int)i;
    bool is_float = false;
    if (src[j] == '0' && j + 1 < n && (src[j + 1] == 'x' || src[j + 1] == 'X')) {
        j += 2;
        size_t h = j;
        while (j < n && std::isxdigit((unsigned char)src[j])) ++j;
        if (j == h) return SetError(err, "malformed hex literal", pos);
    } else {
        while (j < n && IsDigit(src[j])) ++j;
        if (j < n && src[j] == '.') {
            is_float = true;
            ++j;
            while (j < n && IsDigit(src[j])) ++j;
        }
        if (j < n && (src[j] == 'e' || src[j] == 'E')) {
            size_t k = j + 1;
            if (k < n && (src[k] == '+' || src[k] == '-')) ++k;
            if (k >= n || !IsDigit(src[k])) return SetError(err, "malformed exponent", (int)j);
            is_float = true;
            j = k;
            while (j < n && IsDigit(src[j])) ++j;
        }
        if (j < n && (src[j] == 'f' || src[j] == 'F')) {
            is_float = true;
            ++j;
        }
    }
    if (j < n && (IsIdentChar(src[j]) || src[j] == '.')) return SetError(err, "malformed number", pos);
    Token t;
    t.kind = is_float ? Tok::Float : Tok::Int;
    t.text = src.substr(i, j - i);
    t.pos = pos;
    out.push_back(t);
    i = j;
    return true;
}

inline bool LexString(const std::string& src, size_t& i, std::vector<Token>& out, Error* err) {
    size_t n = src.size();
    int pos = (int)i;
    char q = src[i];
    size_t j = i + 1;
    std::string val;
    for (;;) {
        if (j >= n) return SetError(err, "unterminated string", pos);
        char d = src[j];
        if (d == q) {
            ++j;
            break;
        }
        if (d == '\\') {
            if (j + 1 >= n) return SetError(err, "unterminated string", pos);
            switch (src[j + 1]) {
            case 'n': val += '\n'; break;
            case 't': val += '\t'; break;
            case 'r': val += '\r'; break;
            case '0': val += '\0'; break;
            case '\\': val += '\\'; break;
            case '"': val += '"'; break;
            case '\'': val += '\''; break;
            default: return SetError(err, "bad escape sequence", (int)j);
            }
            j += 2;
            continue;
        }
        val += d;
        ++j;
    }
    Token t;
    t.kind = Tok::Str;
    t.text = val;
    t.pos = pos;
    out.push_back(t);
    i = j;
    return true;
}

inline bool Lex(const std::string& src, std::vector<Token>& out, Error* err) {
    out.clear();
    size_t i = 0, n = src.size();
    while (i < n) {
        char c = src[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            ++i;
            continue;
        }
        int pos = (int)i;
        if (IsDigit(c) || (c == '.' && i + 1 < n && IsDigit(src[i + 1]))) {
            if (!LexNumber(src, i, out, err)) return false;
            continue;
        }
        if (IsIdentStart(c)) {
            size_t j = i;
            while (j < n && IsIdentChar(src[j])) ++j;
            Token t;
            t.text = src.substr(i, j - i);
            t.kind = t.text == "true" ? Tok::True : t.text == "false" ? Tok::False : Tok::Ident;
            t.pos = pos;
            out.push_back(t);
            i = j;
            continue;
        }
        if (c == '"' || c == '\'') {
            if (!LexString(src, i, out, err)) return false;
            continue;
        }
        char c2 = i + 1 < n ? src[i + 1] : '\0';
        Tok k = Tok::End;
        size_t len = 1;
        if (c == '|' && c2 == '|') { k = Tok::Or; len = 2; }
        else if (c == '&' && c2 == '&') { k = Tok::And; len = 2; }
        else if (c == '=' && c2 == '=') { k = Tok::Eq; len = 2; }
        else if (c == '!' && c2 == '=') { k = Tok::Ne; len = 2; }
        else if (c == '<' && c2 == '=') { k = Tok::Le; len = 2; }
        else if (c == '>' && c2 == '=') { k = Tok::Ge; len = 2; }
        else if (c == ':' && c2 == ':') { k = Tok::ColonColon; len = 2; }
        else if (c == '<') k = Tok::Lt;
        else if (c == '>') k = Tok::Gt;
        else if (c == '+') k = Tok::Plus;
        else if (c == '-') k = Tok::Minus;
        else if (c == '*') k = Tok::Star;
        else if (c == '/') k = Tok::Slash;
        else if (c == '%') k = Tok::Percent;
        else if (c == '!') k = Tok::Not;
        else if (c == '(') k = Tok::LParen;
        else if (c == ')') k = Tok::RParen;
        else if (c == '[') k = Tok::LBracket;
        else if (c == ']') k = Tok::RBracket;
        else if (c == '.') k = Tok::Dot;
        else if (c == '|') return SetError(err, "expected '||'", pos);
        else if (c == '&') return SetError(err, "expected '&&'", pos);
        else if (c == '=') return SetError(err, "expected '=='", pos);
        else if (c == ':') return SetError(err, "expected '::'", pos);
        else return SetError(err, std::string("unexpected character '") + c + "'", pos);
        Token t;
        t.kind = k;
        t.text = src.substr(i, len);
        t.pos = pos;
        out.push_back(t);
        i += len;
    }
    if (out.size() > kMaxTokens) return SetError(err, "expression too long", 0);
    Token end;
    end.kind = Tok::End;
    end.pos = (int)n;
    out.push_back(end);
    return true;
}

inline int LevelOf(Tok k) {
    switch (k) {
    case Tok::Or: return 1;
    case Tok::And: return 2;
    case Tok::Eq: case Tok::Ne: return 3;
    case Tok::Lt: case Tok::Le: case Tok::Gt: case Tok::Ge: return 4;
    case Tok::Plus: case Tok::Minus: return 5;
    case Tok::Star: case Tok::Slash: case Tok::Percent: return 6;
    default: return 0;
    }
}

struct Parser {
    const std::vector<Token>& toks;
    size_t i = 0;
    int depth = 0;
    Error* err = nullptr;

    Parser(const std::vector<Token>& t, Error* e) : toks(t), err(e) {}

    const Token& Peek() const { return toks[i]; }

    bool Enter() {
        if (depth >= kMaxDepth) return SetError(err, "expression nested too deeply", Peek().pos);
        ++depth;
        return true;
    }

    bool ParseExpr(Ast& out) {
        if (!Enter()) return false;
        bool ok = ParseBinary(1, out);
        --depth;
        return ok;
    }

    bool Fail(const std::string& msg) {
        const Token& t = Peek();
        std::string what = t.kind == Tok::End ? "end of expression" : "'" + t.text + "'";
        return SetError(err, msg + " but found " + what, t.pos);
    }

    bool Expect(Tok k, const char* what) {
        if (Peek().kind != k) return Fail(std::string("expected ") + what);
        ++i;
        return true;
    }

    bool ParseBinary(int level, Ast& out) {
        if (level > 6) return ParseUnary(out);
        if (!ParseBinary(level + 1, out)) return false;
        while (LevelOf(Peek().kind) == level) {
            Token op = Peek();
            ++i;
            Ast rhs;
            if (!ParseBinary(level + 1, rhs)) return false;
            Ast node;
            node.kind = Ast::Kind::Binary;
            node.text = op.text;
            node.pos = op.pos;
            node.kids.push_back(std::move(out));
            node.kids.push_back(std::move(rhs));
            out = std::move(node);
        }
        return true;
    }

    bool ParseUnary(Ast& out) {
        const Token& t = Peek();
        if (t.kind == Tok::Not || t.kind == Tok::Minus) {
            Ast node;
            node.kind = Ast::Kind::Unary;
            node.text = t.text;
            node.pos = t.pos;
            ++i;
            Ast kid;
            if (!Enter()) return false;
            bool ok = ParseUnary(kid);
            --depth;
            if (!ok) return false;
            node.kids.push_back(std::move(kid));
            out = std::move(node);
            return true;
        }
        return ParsePrimary(out);
    }

    bool ParsePrimary(Ast& out) {
        const Token& t = Peek();
        out = Ast();
        out.pos = t.pos;
        switch (t.kind) {
        case Tok::Int: {
            out.kind = Ast::Kind::Literal;
            out.text = t.text;
            bool hex = t.text.size() > 1 && (t.text[1] == 'x' || t.text[1] == 'X');
            out.literal = MakeInt(std::strtoll(t.text.c_str() + (hex ? 2 : 0), nullptr, hex ? 16 : 10));
            ++i;
            return true;
        }
        case Tok::Float:
            out.kind = Ast::Kind::Literal;
            out.text = t.text;
            out.literal = MakeFloat(json_detail::StrToDoubleC(t.text.c_str(), t.text.size()));
            ++i;
            return true;
        case Tok::Str:
            out.kind = Ast::Kind::Literal;
            out.text = t.text;
            out.literal = MakeString(t.text);
            ++i;
            return true;
        case Tok::True:
        case Tok::False:
            out.kind = Ast::Kind::Literal;
            out.text = t.text;
            out.literal = MakeBool(t.kind == Tok::True);
            ++i;
            return true;
        case Tok::LParen: {
            ++i;
            if (!ParseExpr(out)) return false;
            return Expect(Tok::RParen, "')'");
        }
        case Tok::Ident:
            return ParsePath(out);
        default:
            return Fail("expected expression");
        }
    }

    bool ParsePath(Ast& out) {
        const Token& first = Peek();
        out.kind = Ast::Kind::Path;
        out.text = first.text;
        out.pos = first.pos;
        ++i;
        if (Peek().kind == Tok::ColonColon) {
            ++i;
            if (Peek().kind != Tok::Ident) return Fail("expected enum member after '::'");
            out.kind = Ast::Kind::EnumLit;
            out.text += "::" + Peek().text;
            ++i;
            return true;
        }
        for (;;) {
            if (Peek().kind == Tok::Dot) {
                ++i;
                if (Peek().kind != Tok::Ident) return Fail("expected identifier after '.'");
                out.text += "." + Peek().text;
                ++i;
            } else if (Peek().kind == Tok::LBracket) {
                Ast ix;
                ix.kind = Ast::Kind::Index;
                ix.text = "[]";
                ix.pos = Peek().pos;
                ++i;
                Ast e;
                if (!ParseExpr(e)) return false;
                if (!Expect(Tok::RBracket, "']'")) return false;
                ix.kids.push_back(std::move(e));
                out.kids.push_back(std::move(ix));
                out.text += "[#]";
            } else {
                return true;
            }
        }
    }
};

}  // namespace detail

inline bool Parse(const std::string& src, Ast& out, Error* err) {
    out = Ast();
    std::vector<detail::Token> toks;
    if (!detail::Lex(src, toks, err)) return false;
    detail::Parser p(toks, err);
    Ast result;
    if (!p.ParseExpr(result)) return false;
    if (p.Peek().kind != detail::Tok::End) return p.Fail("expected end of expression");
    out = std::move(result);
    return true;
}

}  // namespace expr
}  // namespace imforge
