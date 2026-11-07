#include "codegen.h"
#include "roundtrip_internal.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

namespace imforge {
namespace roundtrip {

namespace {

using Tokens = std::vector<Token>;

bool IsPunct(const Token& t, const char* s) { return t.kind == Token::Punct && t.text == s; }
bool IsIdent(const Token& t, const char* s) { return t.kind == Token::Ident && t.text == s; }

size_t MatchParen(const Tokens& t, size_t open) {
    int depth = 0;
    for (size_t i = open; i < t.size(); ++i) {
        if (IsPunct(t[i], "(")) ++depth;
        else if (IsPunct(t[i], ")") && --depth == 0) return i;
    }
    return std::string::npos;
}

size_t StringExtent(const Tokens& t, size_t i) {
    size_t n = 0;
    while (i + n < t.size() && t[i + n].kind == Token::String) ++n;
    return n;
}

size_t SignedNumberExtent(const Tokens& t, size_t i) {
    size_t n = 0;
    if (i < t.size() && (IsPunct(t[i], "-") || IsPunct(t[i], "+"))) ++n;
    if (i + n < t.size() && t[i + n].kind == Token::Number) return n + 1;
    return 0;
}

bool NumberLike(const Tokens& t, size_t i) {
    return i < t.size() && (t[i].kind == Token::Number ||
                            (IsPunct(t[i], "-") && i + 1 < t.size() && t[i + 1].kind == Token::Number));
}

bool IsChainAtom(const Token& t) { return t.kind == Token::Ident || t.kind == Token::Number; }

size_t ChainExtent(const Tokens& t, size_t i) {
    if (i >= t.size() || !IsChainAtom(t[i])) return 0;
    size_t n = 1;
    while (i + n + 1 < t.size() && IsPunct(t[i + n], "|") && IsChainAtom(t[i + n + 1])) n += 2;
    return n;
}

size_t CallExtent(const Tokens& t, size_t i, std::initializer_list<const char*> names) {
    if (i + 1 >= t.size() || t[i].kind != Token::Ident || !IsPunct(t[i + 1], "(")) return 0;
    bool known = false;
    for (const char* n : names) known = known || t[i].text == n;
    if (!known) return 0;
    size_t close = MatchParen(t, i + 1);
    return close == std::string::npos ? 0 : close - i + 1;
}

size_t LiteralExtent(const Tokens& t, size_t i, PropType type) {
    if (i >= t.size()) return 0;
    switch (type) {
        case PropType::String: return StringExtent(t, i);
        case PropType::Float:
        case PropType::Int: return SignedNumberExtent(t, i);
        case PropType::Bool: return IsIdent(t[i], "true") || IsIdent(t[i], "false") ? 1 : 0;
        case PropType::Flags: return ChainExtent(t, i);
        case PropType::Enum: return t[i].kind == Token::Ident ? 1 : 0;
        case PropType::Color: return CallExtent(t, i, {"ImVec4", "ImColor", "IM_COL32"});
        case PropType::Vec2:
        case PropType::Size: return CallExtent(t, i, {"ImVec2"});
        case PropType::Vec4: return CallExtent(t, i, {"ImVec4"});
        default: return 0;
    }
}

bool InferType(const Tokens& t, size_t i, PropType& out) {
    if (i >= t.size()) return false;
    const Token& k = t[i];
    if (k.kind == Token::String) out = PropType::String;
    else if (NumberLike(t, i)) out = PropType::Float;
    else if (IsIdent(k, "true") || IsIdent(k, "false")) out = PropType::Bool;
    else if (IsIdent(k, "ImVec4") || IsIdent(k, "ImColor") || IsIdent(k, "IM_COL32"))
        out = PropType::Color;
    else if (IsIdent(k, "ImVec2")) out = PropType::Vec2;
    else if (k.kind == Token::Ident && detail::StartsWith(k.text, "ImGui"))
        out = k.text.find("Flags_") != std::string::npos ? PropType::Flags : PropType::Enum;
    else return false;
    return true;
}

bool ParseNumber(const std::string& text, double& out) {
    std::string s;
    for (char c : text)
        if (c != '\'') s += c;
    double sign = 1;
    if (!s.empty() && (s[0] == '-' || s[0] == '+')) {
        sign = s[0] == '-' ? -1 : 1;
        s.erase(0, 1);
    }
    bool hex = s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X');
    size_t e = s.size();
    const char* suffix = hex ? "uUlLzZ" : "fFlLuUzZ";
    while (e > 0 && std::strchr(suffix, s[e - 1])) --e;
    s.resize(e);
    if (s.empty()) return false;
    if (!hex) {
        if (!core_detail::ParseNumber(s, out)) return false;
        out *= sign;
        return true;
    }
    char* end = nullptr;
    out = (double)std::strtoull(s.c_str(), &end, 16);
    out *= sign;
    return end && *end == '\0';
}

bool ParseSignedNumber(const Tokens& t, size_t i, size_t n, double& out) {
    double sign = 1;
    size_t k = i;
    if (n == 2) {
        sign = IsPunct(t[k], "-") ? -1 : 1;
        ++k;
    }
    if (!ParseNumber(t[k].text, out)) return false;
    out *= sign;
    return true;
}

void AppendUtf8(std::string& out, unsigned cp) {
    if (cp < 0x80) out += (char)cp;
    else if (cp < 0x800) {
        out += (char)(0xC0 | (cp >> 6));
        out += (char)(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += (char)(0xE0 | (cp >> 12));
        out += (char)(0x80 | ((cp >> 6) & 0x3F));
        out += (char)(0x80 | (cp & 0x3F));
    } else {
        out += (char)(0xF0 | (cp >> 18));
        out += (char)(0x80 | ((cp >> 12) & 0x3F));
        out += (char)(0x80 | ((cp >> 6) & 0x3F));
        out += (char)(0x80 | (cp & 0x3F));
    }
}

int HexVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool UnescapeOne(const std::string& s, size_t& i, std::string& out) {
    char c = s[i++];
    switch (c) {
        case 'n': out += '\n'; return true;
        case 't': out += '\t'; return true;
        case 'r': out += '\r'; return true;
        case 'a': out += '\a'; return true;
        case 'b': out += '\b'; return true;
        case 'f': out += '\f'; return true;
        case 'v': out += '\v'; return true;
        case '\\': case '"': case '\'': case '?': out += c; return true;
        default: break;
    }
    if (c == 'x') {
        unsigned v = 0;
        int digits = 0;
        while (i < s.size() && HexVal(s[i]) >= 0) {
            v = v * 16 + (unsigned)HexVal(s[i++]);
            ++digits;
        }
        if (!digits) return false;
        out += (char)(v & 0xFF);
        return true;
    }
    if (c == 'u' || c == 'U') {
        int want = c == 'u' ? 4 : 8;
        unsigned v = 0;
        for (int k = 0; k < want; ++k) {
            if (i >= s.size() || HexVal(s[i]) < 0) return false;
            v = v * 16 + (unsigned)HexVal(s[i++]);
        }
        AppendUtf8(out, v);
        return true;
    }
    if (c >= '0' && c <= '7') {
        unsigned v = (unsigned)(c - '0');
        for (int k = 0; k < 2 && i < s.size() && s[i] >= '0' && s[i] <= '7'; ++k)
            v = v * 8 + (unsigned)(s[i++] - '0');
        out += (char)(v & 0xFF);
        return true;
    }
    return false;
}

bool UnescapeString(const std::string& lit, std::string& out) {
    size_t q = lit.find('"');
    if (q == std::string::npos || lit.size() < q + 2 || lit.back() != '"') return false;
    bool raw = q > 0 && lit[q - 1] == 'R';
    if (raw) {
        size_t open = lit.find('(', q);
        if (open == std::string::npos) return false;
        std::string delim = lit.substr(q + 1, open - q - 1);
        size_t close = lit.size() - 1 - delim.size() - 1;
        if (close < open + 1 || lit.compare(close, 1, ")") != 0) return false;
        out += lit.substr(open + 1, close - open - 1);
        return true;
    }
    size_t i = q + 1, end = lit.size() - 1;
    while (i < end) {
        char c = lit[i++];
        if (c != '\\') {
            out += c;
            continue;
        }
        if (i >= end || !UnescapeOne(lit, i, out)) return false;
    }
    return true;
}

// a trailing "###<id>" piece is the id suffix codegen adds for siblings, not part of the label
bool ParseStringRun(const Tokens& t, size_t i, size_t n, std::string& out) {
    out.clear();
    if (n >= 2) {
        std::string last;
        if (UnescapeString(t[i + n - 1].text, last) && detail::StartsWith(last, "###")) --n;
    }
    for (size_t k = 0; k < n; ++k)
        if (!UnescapeString(t[i + k].text, out)) return false;
    return true;
}

std::vector<std::pair<size_t, size_t>> CallArgs(const Tokens& t, size_t i, size_t n) {
    std::vector<std::pair<size_t, size_t>> args;
    size_t begin = i + 2, end = i + n - 1;
    int depth = 0;
    size_t start = begin;
    for (size_t k = begin; k < end; ++k) {
        if (IsPunct(t[k], "(")) ++depth;
        else if (IsPunct(t[k], ")")) --depth;
        else if (depth == 0 && IsPunct(t[k], ",")) {
            args.emplace_back(start, k);
            start = k + 1;
        }
    }
    if (end > begin) args.emplace_back(start, end);
    return args;
}

// one ImVec2/ImVec4 component: [-]N, maybe `* scale` or `scale *`, maybe in parens
bool ParseComponent(const Tokens& t, size_t b, size_t e, double& value, bool& scaled) {
    bool neg = false;
    if (b < e && IsPunct(t[b], "-")) {
        neg = true;
        ++b;
    }
    if (e > b && e - b >= 2 && IsPunct(t[b], "(") && IsPunct(t[e - 1], ")")) {
        ++b;
        --e;
    }
    double v = 0;
    scaled = false;
    if (e <= b) return false;
    size_t n = e - b;
    if (n == 1 && t[b].kind == Token::Number) {
        if (!ParseNumber(t[b].text, v)) return false;
    } else if (n == 2 && IsPunct(t[b], "-") && t[b + 1].kind == Token::Number) {
        if (!ParseNumber(t[b + 1].text, v)) return false;
        v = -v;
    } else if (n == 3 && IsPunct(t[b + 1], "*") && t[b].kind == Token::Number &&
               t[b + 2].kind == Token::Ident) {
        if (!ParseNumber(t[b].text, v)) return false;
        scaled = true;
    } else if (n == 3 && IsPunct(t[b + 1], "*") && t[b + 2].kind == Token::Number &&
               t[b].kind == Token::Ident) {
        if (!ParseNumber(t[b + 2].text, v)) return false;
        scaled = true;
    } else {
        return false;
    }
    value = neg ? -v : v;
    return true;
}

bool IsFillComponent(const Tokens& t, size_t b, size_t e) {
    return e - b == 2 && IsPunct(t[b], "-") && IsIdent(t[b + 1], "FLT_MIN");
}

Json SizeComponent(const Tokens& t, size_t b, size_t e, bool& ok) {
    ok = true;
    if (IsFillComponent(t, b, e)) return Json("fill");
    double v = 0;
    bool scaled = false;
    if (!ParseComponent(t, b, e, v, scaled)) {
        ok = false;
        return Json();
    }
    if (v == 0) return Json("auto");
    if (v < 0) return Json("fill-" + core_detail::FormatFloat((float)-v));
    return Json(v);
}

bool ParseColorCall(const Tokens& t, size_t i, size_t n, Json& out) {
    std::vector<std::pair<size_t, size_t>> args = CallArgs(t, i, n);
    if (args.size() != 3 && args.size() != 4) return false;
    double c[4] = {0, 0, 0, 1};
    bool any_float = false, any_big = false;
    for (size_t k = 0; k < args.size(); ++k) {
        bool scaled = false;
        if (!ParseComponent(t, args[k].first, args[k].second, c[k], scaled) || scaled)
            return false;
        const std::string& txt = t[args[k].second - 1].text;
        any_float = any_float || txt.find('.') != std::string::npos ||
                    txt.find('f') != std::string::npos || txt.find('F') != std::string::npos;
        any_big = any_big || c[k] > 1.0;
    }
    bool bytes = t[i].text == "IM_COL32" || (t[i].text == "ImColor" && (!any_float || any_big));
    Vec4 v;
    float div = bytes ? 255.0f : 1.0f;
    v.x = (float)c[0] / div;
    v.y = (float)c[1] / div;
    v.z = (float)c[2] / div;
    v.w = (float)c[3] / div;
    out = Json(ColorToHex(v));
    return true;
}

bool ParseVecCall(const Tokens& t, size_t i, size_t n, PropType type, Json& out) {
    std::vector<std::pair<size_t, size_t>> args = CallArgs(t, i, n);
    size_t want = type == PropType::Vec4 ? 4 : 2;
    if (args.size() != want) return false;
    Json o = Json::Object();
    static const char* const kVecKeys[] = {"x", "y", "z", "w"};
    static const char* const kSizeKeys[] = {"w", "h"};
    for (size_t k = 0; k < want; ++k) {
        bool ok = true;
        Json comp;
        if (type == PropType::Size) comp = SizeComponent(t, args[k].first, args[k].second, ok);
        else {
            double v = 0;
            bool scaled = false;
            ok = ParseComponent(t, args[k].first, args[k].second, v, scaled);
            comp = Json(v);
        }
        if (!ok) return false;
        o.Set(type == PropType::Size ? kSizeKeys[k] : kVecKeys[k], comp);
    }
    out = o;
    return true;
}

std::string StripImGuiPrefix(const std::string& ident) {
    size_t u = ident.find('_');
    return u == std::string::npos ? ident : ident.substr(u + 1);
}

bool ChainNames(const Tokens& t, size_t i, size_t n, std::vector<std::string>& out,
                std::string* bad) {
    for (size_t k = 0; k < n; k += 2) {
        const Token& a = t[i + k];
        if (a.kind == Token::Number) continue;
        if (!detail::StartsWith(a.text, "ImGui") || a.text.find('_') == std::string::npos) {
            if (bad) *bad = a.text;
            return false;
        }
        std::string name = StripImGuiPrefix(a.text);
        if (name != "None") out.push_back(name);
    }
    return true;
}

bool ParseLiteral(const Tokens& t, size_t i, size_t n, PropType type, Json& out) {
    switch (type) {
        case PropType::String: {
            std::string s;
            if (!ParseStringRun(t, i, n, s)) return false;
            out = Json(s);
            return true;
        }
        case PropType::Float:
        case PropType::Int: {
            double v = 0;
            if (!ParseSignedNumber(t, i, n, v)) return false;
            out = Json(v);
            return true;
        }
        case PropType::Bool: out = Json(t[i].text == "true"); return true;
        case PropType::Enum:
            if (!detail::StartsWith(t[i].text, "ImGui")) return false;
            out = Json(StripImGuiPrefix(t[i].text));
            return true;
        case PropType::Color: return ParseColorCall(t, i, n, out);
        case PropType::Vec2:
        case PropType::Vec4:
        case PropType::Size: return ParseVecCall(t, i, n, type, out);
        default: return false;
    }
}

bool ResolveType(const Node* node, const std::string& prop, const Tokens& fresh, size_t j,
                 PropType& out) {
    if (prop == "tooltip") {
        out = PropType::String;
        return true;
    }
    if (detail::StartsWith(prop, "style.colors.")) {
        out = PropType::Color;
        return true;
    }
    if (detail::StartsWith(prop, "style.vars.")) {
        out = StyleVarIsVec2(prop.c_str() + 11) ? PropType::Vec2 : PropType::Float;
        return true;
    }
    if (node) {
        if (const WidgetDef* w = FindWidget(node->type.c_str())) {
            if (const PropDef* pd = FindProp(*w, prop.c_str())) {
                out = pd->type;
                return true;
            }
        }
    }
    return InferType(fresh, j, out);
}

bool SliceEqual(const Tokens& a, size_t i, size_t na, const Tokens& b, size_t j, size_t nb) {
    if (na != nb) return false;
    for (size_t k = 0; k < na; ++k)
        if (a[i + k].text != b[j + k].text) return false;
    return true;
}

bool Fail(std::string* reason, const std::string& msg) {
    if (reason) *reason = msg;
    return false;
}

std::string At(const Tokens& t, size_t i) {
    return i < t.size() ? "line " + std::to_string(t[i].line) + " near '" + t[i].text + "'"
                        : "end of block";
}

bool MakeEdit(const Tokens& ut, size_t i, size_t nu, const Tokens& ft, size_t j, size_t nf,
              const std::string& prop, PropType type, LiteralEdit& e, std::string* reason) {
    e = LiteralEdit();
    e.prop = prop;
    e.type = type;
    if (type == PropType::Flags) {
        std::vector<std::string> user, fresh;
        std::string bad;
        if (!ChainNames(ut, i, nu, user, &bad))
            return Fail(reason, "'" + bad + "' is not an ImGui flag (prop '" + prop + "')");
        ChainNames(ft, j, nf, fresh, nullptr);
        for (const std::string& f : user)
            if (std::find(fresh.begin(), fresh.end(), f) == fresh.end()) e.flags_added.push_back(f);
        for (const std::string& f : fresh)
            if (std::find(user.begin(), user.end(), f) == user.end()) e.flags_removed.push_back(f);
        if (e.flags_added.empty() && e.flags_removed.empty())
            return Fail(reason, "the edited value of prop '" + prop + "' is not a flag list (" +
                                    At(ut, i) + ")");
        return true;
    }
    if (!ParseLiteral(ut, i, nu, type, e.value))
        return Fail(reason, "cannot read the edited " + std::string(PropTypeName(type)) +
                                " literal for prop '" + prop + "' at " + At(ut, i));
    return true;
}

}  // namespace

bool ExtractLiteralEdits(const Node* node, const Tokens& ut, const Tokens& ft,
                         const std::vector<codegen::LiteralRef>& refs_in,
                         std::vector<LiteralEdit>& out, std::string* reason) {
    std::vector<const codegen::LiteralRef*> refs;
    for (const codegen::LiteralRef& r : refs_in)
        if (r.block_token_index >= 0) refs.push_back(&r);
    std::stable_sort(refs.begin(), refs.end(),
                     [](const codegen::LiteralRef* a, const codegen::LiteralRef* b) {
                         return a->block_token_index < b->block_token_index;
                     });
    size_t i = 0, j = 0, r = 0;
    while (true) {
        while (r < refs.size() && refs[r]->block_token_index < (int)j) ++r;
        if (r < refs.size() && (size_t)refs[r]->block_token_index == j) {
            const codegen::LiteralRef& ref = *refs[r];
            PropType type;
            size_t nf = ResolveType(node, ref.prop, ft, j, type) ? LiteralExtent(ft, j, type) : 0;
            if (nf == 0) {
                ++r;
                continue;
            }
            size_t nu = LiteralExtent(ut, i, type);
            if (nu == 0)
                return Fail(reason, "the edited value of prop '" + ref.prop + "' is not a plain " +
                                        PropTypeName(type) + " literal (" + At(ut, i) + ")");
            if (!SliceEqual(ut, i, nu, ft, j, nf)) {
                LiteralEdit e;
                if (!MakeEdit(ut, i, nu, ft, j, nf, ref.prop, type, e, reason)) return false;
                out.push_back(e);
            }
            i += nu;
            j += nf;
            ++r;
            continue;
        }
        if (i >= ut.size() && j >= ft.size()) return true;
        if (i >= ut.size() || j >= ft.size())
            return Fail(reason, "code structure differs: " +
                                    std::string(i >= ut.size() ? "missing" : "extra") +
                                    " code at " + At(ut, i));
        if (ut[i].text != ft[j].text)
            return Fail(reason, "code structure differs at " + At(ut, i) + " (generated: '" +
                                    ft[j].text + "')");
        ++i;
        ++j;
    }
}

bool ApplyLiteralEdits(Node& node, const std::vector<LiteralEdit>& edits, std::string* reason) {
    for (const LiteralEdit& e : edits) {
        if (e.prop == "tooltip" || detail::StartsWith(e.prop, "style.")) continue;
        const PropValue* pv = node.Prop(e.prop.c_str());
        if (pv && (pv->IsExpr() || pv->IsParam()))
            return Fail(reason, "prop '" + e.prop + "' is bound to an expression or parameter");
    }
    for (const LiteralEdit& e : edits) {
        if (e.prop == "tooltip") {
            node.tooltip = e.value.AsString();
        } else if (detail::StartsWith(e.prop, "style.colors.")) {
            std::string name = e.prop.substr(13);
            Vec4 c = ParseColor(e.value.AsString());
            bool found = false;
            for (auto& kv : node.style.colors)
                if (kv.first == name) kv.second = c, found = true;
            if (!found) node.style.colors.emplace_back(name, c);
        } else if (detail::StartsWith(e.prop, "style.vars.")) {
            std::string name = e.prop.substr(11);
            bool found = false;
            for (auto& kv : node.style.vars)
                if (kv.first == name) kv.second = e.value, found = true;
            if (!found) node.style.vars.emplace_back(name, e.value);
        } else if (e.type == PropType::Flags) {
            std::vector<std::string> flags;
            if (const PropValue* pv = node.Prop(e.prop.c_str()))
                for (const Json& f : pv->raw.arr)
                    if (f.IsString()) flags.push_back(f.AsString());
            for (const std::string& f : e.flags_removed)
                flags.erase(std::remove(flags.begin(), flags.end(), f), flags.end());
            for (const std::string& f : e.flags_added)
                if (std::find(flags.begin(), flags.end(), f) == flags.end()) flags.push_back(f);
            Json arr = Json::Array();
            for (const std::string& f : flags) arr.Push(Json(f));
            node.SetProp(e.prop.c_str(), arr);
        } else {
            node.SetProp(e.prop.c_str(), e.value);
        }
    }
    return true;
}

}  // namespace roundtrip
}  // namespace imforge
