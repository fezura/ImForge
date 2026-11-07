#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <clocale>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "imforge/imforge_expr.h"

using namespace imforge;
using namespace imforge::expr;

namespace {

Value VInt(long long i) { Value v; v.kind = Value::Kind::Int; v.i = i; return v; }
Value VFloat(double f) { Value v; v.kind = Value::Kind::Float; v.f = f; return v; }
Value VBool(bool b) { Value v; v.kind = Value::Kind::Bool; v.b = b; return v; }
Value VStr(const std::string& s) { Value v; v.kind = Value::Kind::String; v.s = s; return v; }

EnumInfo MakeEnum(const char* name, std::vector<std::pair<std::string, long long>> values) {
    EnumInfo e;
    e.name = name;
    e.values = std::move(values);
    return e;
}

EnumInfo g_mode_enum = MakeEnum("Mode", {{"Off", 0}, {"Aim", 1}, {"Silent", 2}});
EnumInfo g_weapon_enum = MakeEnum("Weapon", {{"Pistol", 0}, {"Rifle", 1}});

CType CT(CKind k) { CType t; t.kind = k; return t; }

struct MapTypeEnv : TypeEnv {
    std::map<std::string, CType> types;
    bool TypeOfPath(const std::string& path, CType& out) override {
        auto it = types.find(path);
        if (it == types.end()) return false;
        out = it->second;
        return true;
    }
    bool EnumValue(const std::string& enum_name, const std::string& member, long long& out) override {
        const EnumInfo* e = enum_name == "Mode" ? &g_mode_enum : enum_name == "Weapon" ? &g_weapon_enum : nullptr;
        if (!e) return false;
        for (auto& kv : e->values)
            if (kv.first == member) { out = kv.second; return true; }
        return false;
    }
};

struct MapValueEnv : ValueEnv {
    std::map<std::string, Value> values;
    std::vector<std::string> reads;
    bool ReadPath(const std::string& path, Value& out) override {
        reads.push_back(path);
        auto it = values.find(path);
        if (it == values.end()) return false;
        out = it->second;
        return true;
    }
    bool EnumValue(const std::string& enum_name, const std::string& member, long long& out) override {
        MapTypeEnv t;
        return t.EnumValue(enum_name, member, out);
    }
};

MapTypeEnv MakeTypeEnv() {
    MapTypeEnv env;
    env.types["player.health"] = CT(CKind::Float);
    env.types["player.hp"] = CT(CKind::I32);
    env.types["player.name"] = CT(CKind::StdString);
    env.types["player.tag"] = CT(CKind::CharArray);
    env.types["player.alive"] = CT(CKind::Bool);
    env.types["player.pos"] = CT(CKind::Vec2);
    CType mode = CT(CKind::Enum);
    mode.enm = &g_mode_enum;
    env.types["cfg.mode"] = mode;
    CType weapon = CT(CKind::Enum);
    weapon.enm = &g_weapon_enum;
    env.types["cfg.weapon"] = weapon;
    env.types["items"] = CT(CKind::Vector);
    env.types["items[0].name"] = CT(CKind::StdString);
    env.types["items[2].name"] = CT(CKind::StdString);
    env.types["slots"] = CT(CKind::Array);
    env.types["i"] = CT(CKind::I32);
    env.types["player"] = CT(CKind::Struct);
    return env;
}

MapValueEnv MakeValueEnv() {
    MapValueEnv env;
    env.values["player.health"] = VFloat(75.5);
    env.values["player.hp"] = VInt(75);
    env.values["player.name"] = VStr("bob");
    env.values["player.alive"] = VBool(true);
    env.values["cfg.mode"] = VInt(1);
    env.values["items.size"] = VInt(2);
    env.values["items[0].name"] = VStr("first");
    env.values["items[1].name"] = VStr("second");
    env.values["i"] = VInt(0);
    env.values["flag"] = VBool(false);
    return env;
}

bool ParseOk(const std::string& src, Ast& ast) {
    Error e;
    bool ok = Parse(src, ast, &e);
    if (!ok) MESSAGE("parse error: " << e.message << " col " << e.col);
    return ok;
}

Value EvalOk(const std::string& src, ValueEnv& env) {
    Ast ast;
    REQUIRE(ParseOk(src, ast));
    Value out;
    Error e;
    bool ok = Eval(ast, env, out, &e);
    if (!ok) MESSAGE("eval error: " << e.message << " col " << e.col);
    REQUIRE(ok);
    return out;
}

Error EvalErr(const std::string& src, ValueEnv& env) {
    Ast ast;
    REQUIRE(ParseOk(src, ast));
    Value out;
    Error e;
    REQUIRE_FALSE(Eval(ast, env, out, &e));
    return e;
}

Type CheckOk(const std::string& src, TypeEnv& env) {
    Ast ast;
    REQUIRE(ParseOk(src, ast));
    Type t = Type::Error;
    Error e;
    bool ok = TypeCheck(ast, env, t, &e);
    if (!ok) MESSAGE("type error: " << e.message << " col " << e.col);
    REQUIRE(ok);
    return t;
}

Error CheckErr(const std::string& src, TypeEnv& env) {
    Ast ast;
    REQUIRE(ParseOk(src, ast));
    Type t = Type::Error;
    Error e;
    REQUIRE_FALSE(TypeCheck(ast, env, t, &e));
    CHECK(t == Type::Error);
    return e;
}

Error ParseErr(const std::string& src) {
    Ast ast;
    Error e;
    REQUIRE_FALSE(Parse(src, ast, &e));
    CHECK(e.line == 1);
    return e;
}

std::string Emit(const std::string& src) {
    Ast ast;
    REQUIRE(ParseOk(src, ast));
    return EmitCpp(ast, [](const std::string& p) { return p; });
}

}  // namespace

TEST_CASE("lexer: numbers") {
    std::vector<detail::Token> t;
    Error e;
    REQUIRE(detail::Lex("1 1.5 1e3 0x1F 2. .5 1.5f 7E-2", t, &e));
    REQUIRE(t.size() == 9);
    CHECK(t[0].kind == detail::Tok::Int);
    CHECK(t[1].kind == detail::Tok::Float);
    CHECK(t[2].kind == detail::Tok::Float);
    CHECK(t[3].kind == detail::Tok::Int);
    CHECK(t[3].text == "0x1F");
    CHECK(t[4].kind == detail::Tok::Float);
    CHECK(t[5].kind == detail::Tok::Float);
    CHECK(t[6].kind == detail::Tok::Float);
    CHECK(t[7].kind == detail::Tok::Float);
    CHECK(t[8].kind == detail::Tok::End);
    CHECK(t[8].pos == 30);

    Ast a;
    REQUIRE(ParseOk("0x1F", a));
    CHECK(a.literal.i == 31);
    REQUIRE(ParseOk("010", a));
    CHECK(a.literal.i == 10);
    REQUIRE(ParseOk("1.5f", a));
    CHECK(a.literal.kind == Value::Kind::Float);
    CHECK(a.literal.f == doctest::Approx(1.5));

    CHECK(ParseErr("1.2.3").message == "malformed number");
    CHECK(ParseErr("1e").message == "malformed exponent");
    CHECK(ParseErr("0x").message == "malformed hex literal");
    CHECK(ParseErr("12abc").col == 1);
}

TEST_CASE("lexer: strings and escapes") {
    Ast a;
    REQUIRE(ParseOk("\"a\\\"b\\n\\t\\\\\"", a));
    CHECK(a.literal.kind == Value::Kind::String);
    CHECK(a.literal.s == "a\"b\n\t\\");
    REQUIRE(ParseOk("'single'", a));
    CHECK(a.literal.s == "single");
    REQUIRE(ParseOk("\"it's\"", a));
    CHECK(a.literal.s == "it's");
    REQUIRE(ParseOk("\"\"", a));
    CHECK(a.literal.s.empty());

    Error e = ParseErr("\"abc");
    CHECK(e.message == "unterminated string");
    CHECK(e.col == 1);
    e = ParseErr("\"a\\qb\"");
    CHECK(e.message == "bad escape sequence");
    CHECK(e.col == 3);
}

TEST_CASE("lexer: operators and bad characters") {
    std::vector<detail::Token> t;
    Error e;
    REQUIRE(detail::Lex("a||b&&c==d!=e<f<=g>h>=i+j-k*l/m%n!(o)[p].q::r", t, &e));
    std::vector<detail::Tok> kinds;
    for (auto& tok : t) kinds.push_back(tok.kind);
    using detail::Tok;
    std::vector<Tok> expect = {
        Tok::Ident, Tok::Or, Tok::Ident, Tok::And, Tok::Ident, Tok::Eq, Tok::Ident, Tok::Ne, Tok::Ident,
        Tok::Lt, Tok::Ident, Tok::Le, Tok::Ident, Tok::Gt, Tok::Ident, Tok::Ge, Tok::Ident, Tok::Plus,
        Tok::Ident, Tok::Minus, Tok::Ident, Tok::Star, Tok::Ident, Tok::Slash, Tok::Ident, Tok::Percent,
        Tok::Ident, Tok::Not, Tok::LParen, Tok::Ident, Tok::RParen, Tok::LBracket, Tok::Ident, Tok::RBracket,
        Tok::Dot, Tok::Ident, Tok::ColonColon, Tok::Ident, Tok::End};
    CHECK(kinds == expect);

    REQUIRE(detail::Lex("  true\tfalse\n trueish ", t, &e));
    CHECK(t[0].kind == Tok::True);
    CHECK(t[1].kind == Tok::False);
    CHECK(t[2].kind == Tok::Ident);
    CHECK(t[2].text == "trueish");

    CHECK(ParseErr("a | b").message == "expected '||'");
    CHECK(ParseErr("a & b").message == "expected '&&'");
    CHECK(ParseErr("a = b").message == "expected '=='");
    CHECK(ParseErr("a : b").message == "expected '::'");
    e = ParseErr("a $ b");
    CHECK(e.message == "unexpected character '$'");
    CHECK(e.col == 3);
}

TEST_CASE("parser: precedence and shape") {
    Ast a;
    REQUIRE(ParseOk("1 + 2 * 3 == 7", a));
    CHECK(a.kind == Ast::Kind::Binary);
    CHECK(a.text == "==");
    CHECK(a.kids[0].text == "+");
    CHECK(a.kids[0].kids[1].text == "*");
    CHECK(a.kids[1].literal.i == 7);

    REQUIRE(ParseOk("!a && b || c", a));
    CHECK(a.text == "||");
    CHECK(a.kids[0].text == "&&");
    CHECK(a.kids[0].kids[0].kind == Ast::Kind::Unary);
    CHECK(a.kids[0].kids[0].text == "!");
    CHECK(a.kids[0].kids[0].kids[0].text == "a");
    CHECK(a.kids[1].kind == Ast::Kind::Path);
    CHECK(a.kids[1].text == "c");

    REQUIRE(ParseOk("a - b - c", a));
    CHECK(a.text == "-");
    CHECK(a.kids[0].text == "-");
    CHECK(a.kids[1].text == "c");

    REQUIRE(ParseOk("(1 + 2) * 3", a));
    CHECK(a.text == "*");
    CHECK(a.kids[0].text == "+");

    REQUIRE(ParseOk("a < b == c > d", a));
    CHECK(a.text == "==");
    CHECK(a.kids[0].text == "<");
    CHECK(a.kids[1].text == ">");
}

TEST_CASE("parser: unary, enum literals, paths") {
    Ast a;
    REQUIRE(ParseOk("-x", a));
    CHECK(a.kind == Ast::Kind::Unary);
    CHECK(a.text == "-");
    CHECK(a.pos == 0);
    CHECK(a.kids[0].kind == Ast::Kind::Path);

    REQUIRE(ParseOk("- -x", a));
    CHECK(a.kids[0].kind == Ast::Kind::Unary);

    REQUIRE(ParseOk("-2 * -3", a));
    CHECK(a.text == "*");
    CHECK(a.kids[0].kind == Ast::Kind::Unary);
    CHECK(a.kids[1].kind == Ast::Kind::Unary);

    REQUIRE(ParseOk("Mode::Aim", a));
    CHECK(a.kind == Ast::Kind::EnumLit);
    CHECK(a.text == "Mode::Aim");

    REQUIRE(ParseOk("inv.items[i + 1].name", a));
    CHECK(a.kind == Ast::Kind::Path);
    CHECK(a.text == "inv.items[#].name");
    REQUIRE(a.kids.size() == 1);
    CHECK(a.kids[0].kind == Ast::Kind::Index);
    CHECK(a.kids[0].pos == 9);
    CHECK(a.kids[0].kids[0].text == "+");
    CHECK(StaticPath(a) == "inv.items[0].name");

    REQUIRE(ParseOk("grid[2][items[0].idx]", a));
    CHECK(a.text == "grid[#][#]");
    REQUIRE(a.kids.size() == 2);
    CHECK(a.kids[1].kids[0].text == "items[#].idx");
    CHECK(StaticPath(a) == "grid[2][0]");

    REQUIRE(ParseOk("items.size", a));
    CHECK(a.text == "items.size");
    CHECK(a.kids.empty());

    REQUIRE(ParseOk("  state.selectedTab  ", a));
    CHECK(a.text == "state.selectedTab");
    CHECK(a.pos == 2);
}

TEST_CASE("parser: error positions") {
    Error e = ParseErr("1 + ");
    CHECK(e.message == "expected expression but found end of expression");
    CHECK(e.col == 5);
    e = ParseErr("(1 + 2");
    CHECK(e.message == "expected ')' but found end of expression");
    CHECK(e.col == 7);
    e = ParseErr("1 +* 2");
    CHECK(e.message == "expected expression but found '*'");
    CHECK(e.col == 4);
    e = ParseErr("foo.");
    CHECK(e.message == "expected identifier after '.' but found end of expression");
    CHECK(e.col == 5);
    e = ParseErr("foo[1");
    CHECK(e.message == "expected ']' but found end of expression");
    CHECK(e.col == 6);
    e = ParseErr("1 2");
    CHECK(e.message == "expected end of expression but found '2'");
    CHECK(e.col == 3);
    e = ParseErr("Mode::");
    CHECK(e.col == 7);
    e = ParseErr("");
    CHECK(e.col == 1);
    e = ParseErr("a b)");
    CHECK(e.col == 3);
    e = ParseErr("(a))");
    CHECK(e.message == "expected end of expression but found ')'");
    CHECK(e.col == 4);
    e = ParseErr("true.x");
    CHECK(e.col == 5);
}

TEST_CASE("typecheck: valid expressions") {
    MapTypeEnv env = MakeTypeEnv();
    CHECK(CheckOk("player.health > 0 && player.alive", env) == Type::Bool);
    CHECK(CheckOk("player.hp + 1", env) == Type::Int);
    CHECK(CheckOk("player.hp + 1.5", env) == Type::Float);
    CHECK(CheckOk("player.hp * player.health", env) == Type::Float);
    CHECK(CheckOk("player.hp % 2", env) == Type::Int);
    CHECK(CheckOk("-player.hp", env) == Type::Int);
    CHECK(CheckOk("-player.health", env) == Type::Float);
    CHECK(CheckOk("!player.alive", env) == Type::Bool);
    CHECK(CheckOk("player.name + \"!\"", env) == Type::String);
    CHECK(CheckOk("player.name == player.tag", env) == Type::Bool);
    CHECK(CheckOk("player.name < \"z\"", env) == Type::Bool);
    CHECK(CheckOk("player.alive == true", env) == Type::Bool);
    CHECK(CheckOk("Mode::Aim", env) == Type::Enum);
    CHECK(CheckOk("cfg.mode == Mode::Aim", env) == Type::Bool);
    CHECK(CheckOk("cfg.mode != 2", env) == Type::Bool);
    CHECK(CheckOk("1 < cfg.mode", env) == Type::Bool);
    CHECK(CheckOk("cfg.mode >= Mode::Aim", env) == Type::Bool);
    CHECK(CheckOk("items.size > 0", env) == Type::Bool);
    CHECK(CheckOk("items.size", env) == Type::Int);
    CHECK(CheckOk("slots.size - 1", env) == Type::Int);
    CHECK(CheckOk("items[i].name", env) == Type::String);
    CHECK(CheckOk("items[2].name", env) == Type::String);
    CHECK(CheckOk("items[items.size - 1].name == \"x\"", env) == Type::Bool);
    CHECK(CheckOk("(player.hp > 10) == player.alive", env) == Type::Bool);
    CHECK(CheckOk("3 == 3.0", env) == Type::Bool);
    CHECK(CheckOk("\"a\"", env) == Type::String);
    CHECK(CheckOk("true", env) == Type::Bool);
    CHECK(CheckOk("1.5", env) == Type::Float);
}

TEST_CASE("typecheck: errors") {
    MapTypeEnv env = MakeTypeEnv();
    Error e = CheckErr("true + 1", env);
    CHECK(e.message == "operator '+' cannot be applied to bool and int");
    CHECK(e.col == 6);
    e = CheckErr("\"a\" < 1", env);
    CHECK(e.message == "operator '<' cannot be applied to string and int");
    e = CheckErr("1.5 % 2", env);
    CHECK(e.message == "operator '%' cannot be applied to float and int");
    e = CheckErr("player.hp % player.health", env);
    CHECK(e.message == "operator '%' cannot be applied to int and float");
    e = CheckErr("!1", env);
    CHECK(e.message == "operator '!' requires bool, got int");
    CHECK(e.col == 1);
    e = CheckErr("-\"a\"", env);
    CHECK(e.message == "operator '-' requires a number, got string");
    e = CheckErr("player.hp && true", env);
    CHECK(e.message == "operator '&&' cannot be applied to int and bool");
    e = CheckErr("player.name - \"a\"", env);
    CHECK(e.message == "operator '-' cannot be applied to string and string");
    e = CheckErr("player.alive < true", env);
    CHECK(e.message == "operator '<' cannot be applied to bool and bool");
    e = CheckErr("player.alive == 1", env);
    CHECK(e.message == "operator '==' cannot be applied to bool and int");
    e = CheckErr("Mode::Aim + 1", env);
    CHECK(e.message == "operator '+' cannot be applied to enum and int");
    e = CheckErr("-Mode::Aim", env);
    CHECK(e.message == "operator '-' requires a number, got enum");
    e = CheckErr("cfg.mode == Weapon::Rifle", env);
    CHECK(e.message == "cannot compare enum 'Mode' with enum 'Weapon'");
    CHECK(e.col == 10);
    e = CheckErr("cfg.mode == 1.5", env);
    CHECK(e.message == "operator '==' cannot be applied to enum and float");
    e = CheckErr("Mode::Nope", env);
    CHECK(e.message == "unknown enum value 'Mode::Nope'");
    e = CheckErr("Other::X == 1", env);
    CHECK(e.message == "unknown enum value 'Other::X'");
    e = CheckErr("1 + x.y", env);
    CHECK(e.message == "unknown path 'x.y'");
    CHECK(e.col == 5);
    e = CheckErr("player.pos", env);
    CHECK(e.message == "path 'player.pos' is not a scalar");
    e = CheckErr("player.size", env);
    CHECK(e.message == "unknown path 'player.size'");
    e = CheckErr("items[\"a\"].name", env);
    CHECK(e.message == "index must be int, got string");
    CHECK(e.col == 6);
    e = CheckErr("items[1.5].name", env);
    CHECK(e.message == "index must be int, got float");
    e = CheckErr("items[9].name", env);
    CHECK(e.message == "unknown path 'items[9].name'");
    e = CheckErr("items[x].name", env);
    CHECK(e.message == "unknown path 'x'");
    CHECK(e.col == 7);
}

TEST_CASE("eval: arithmetic and logic") {
    MapValueEnv env = MakeValueEnv();
    Value v = EvalOk("1 + 2 * 3 == 7", env);
    CHECK(v.kind == Value::Kind::Bool);
    CHECK(v.b);
    CHECK(EvalOk("(1 + 2) * 3", env).i == 9);
    CHECK(EvalOk("7 / 2", env).i == 3);
    CHECK(EvalOk("7 / 2", env).kind == Value::Kind::Int);
    CHECK(EvalOk("7.0 / 2", env).f == doctest::Approx(3.5));
    CHECK(EvalOk("1 + 2.5", env).kind == Value::Kind::Float);
    CHECK(EvalOk("7 % 3", env).i == 1);
    CHECK(EvalOk("-7 % 3", env).i == -1);
    CHECK(EvalOk("-2 * -3", env).i == 6);
    CHECK(EvalOk("- -5", env).i == 5);
    CHECK(EvalOk("-1.5", env).f == doctest::Approx(-1.5));
    CHECK(EvalOk("3 == 3.0", env).b);
    CHECK(EvalOk("3 != 3", env).b == false);
    CHECK(EvalOk("2 < 3 && 3 <= 3 && 4 > 3 && 4 >= 4", env).b);
    CHECK(EvalOk("!true || false", env).b == false);
    CHECK(EvalOk("!false && true", env).b);
    CHECK(EvalOk("true == true", env).b);
    CHECK(EvalOk("true != false", env).b);
    CHECK(EvalOk("0x10 + 1", env).i == 17);
}

TEST_CASE("eval: strings") {
    MapValueEnv env = MakeValueEnv();
    Value v = EvalOk("\"HP: \" + player.name", env);
    CHECK(v.kind == Value::Kind::String);
    CHECK(v.s == "HP: bob");
    CHECK(EvalOk("\"a\" < \"b\"", env).b);
    CHECK(EvalOk("\"b\" >= \"b\"", env).b);
    CHECK(EvalOk("player.name == \"bob\"", env).b);
    CHECK(EvalOk("player.name != 'bob'", env).b == false);
    CHECK(EvalErr("\"a\" + 1", env).message == "operator '+' cannot be applied to string and int");
    CHECK(EvalErr("\"a\" - \"b\"", env).message == "operator '-' cannot be applied to string and string");
    CHECK(EvalErr("\"a\" * 2", env).message == "operator '*' cannot be applied to string and int");
}

TEST_CASE("eval: paths, vectors, size, enums") {
    MapValueEnv env = MakeValueEnv();
    CHECK(EvalOk("player.health > 50 && player.alive", env).b);
    CHECK(EvalOk("player.health > 50 && flag", env).b == false);
    CHECK(EvalOk("items.size", env).i == 2);
    CHECK(EvalOk("items[0].name", env).s == "first");
    CHECK(EvalOk("items[i + 1].name", env).s == "second");
    CHECK(EvalOk("items[items.size - 1].name", env).s == "second");
    CHECK(env.reads.back() == "items[1].name");
    CHECK(EvalOk("cfg.mode == Mode::Aim", env).b);
    CHECK(EvalOk("cfg.mode == Mode::Off", env).b == false);
    Value v = EvalOk("Mode::Silent", env);
    CHECK(v.kind == Value::Kind::Int);
    CHECK(v.i == 2);

    Error e = EvalErr("missing.path + 1", env);
    CHECK(e.message == "cannot read 'missing.path'");
    CHECK(e.col == 1);
    e = EvalErr("items[5].name", env);
    CHECK(e.message == "cannot read 'items[5].name'");
    e = EvalErr("items[1.5].name", env);
    CHECK(e.message == "index must be int, got float");
    CHECK(e.col == 6);
    e = EvalErr("Mode::Nope", env);
    CHECK(e.message == "unknown enum value 'Mode::Nope'");

    MapValueEnv plain;
    plain.values["Mode::Aim"] = VInt(1);
    struct PlainEnv : ValueEnv {
        std::map<std::string, Value>* vals;
        bool ReadPath(const std::string& p, Value& out) override {
            auto it = vals->find(p);
            if (it == vals->end()) return false;
            out = it->second;
            return true;
        }
    } pe;
    pe.vals = &plain.values;
    CHECK(EvalOk("Mode::Aim == 1", pe).b);
}

TEST_CASE("eval: short circuit and errors") {
    MapValueEnv env = MakeValueEnv();
    CHECK(EvalOk("false && missing.path", env).b == false);
    CHECK(EvalOk("true || missing.path", env).b);
    Error e = EvalErr("true && missing.path", env);
    CHECK(e.message == "cannot read 'missing.path'");
    e = EvalErr("1 / 0", env);
    CHECK(e.message == "division by zero");
    CHECK(e.col == 3);
    CHECK(EvalErr("7 % 0", env).message == "division by zero");
    CHECK(EvalErr("1.0 / 0", env).message == "division by zero");
    CHECK(EvalErr("1.5 % 2", env).message == "operator '%' cannot be applied to float and int");
    CHECK(EvalErr("!\"a\"", env).message == "operator '!' requires bool, got string");
    CHECK(EvalErr("-\"a\"", env).message == "operator '-' requires a number, got string");
    CHECK(EvalOk("1 && true", env).b);
    CHECK(EvalErr("\"x\" && true", env).message == "operator '&&' requires bool, got string");
    CHECK(EvalOk("true || 1.5", env).b);
    CHECK(EvalErr("false || 1.5", env).message == "operator '||' requires bool, got float");
    CHECK(EvalErr("true < false", env).message == "operator '<' cannot be applied to bool and bool");
}

TEST_CASE("emit: parenthesization") {
    CHECK(Emit("player.health > 0 && cfg.esp.enabled") == "(player.health > 0 && cfg.esp.enabled)");
    CHECK(Emit("1 + 2 * 3") == "(1 + 2 * 3)");
    CHECK(Emit("(1 + 2) * 3") == "((1 + 2) * 3)");
    CHECK(Emit("1 + 2 * 3 == 7") == "(1 + 2 * 3 == 7)");
    CHECK(Emit("a || b && c") == "(a || (b && c))");
    CHECK(Emit("!a && b || c") == "((!a && b) || c)");
    CHECK(Emit("a && b && c") == "(a && b && c)");
    CHECK(Emit("a && (b && c)") == "(a && (b && c))");
    CHECK(Emit("a - (b - c)") == "(a - (b - c))");
    CHECK(Emit("a - b - c") == "(a - b - c)");
    CHECK(Emit("a / b * c") == "(a / b * c)");
    CHECK(Emit("a * (b + c)") == "(a * (b + c))");
    CHECK(Emit("a == b == c") == "((a == b) == c)");
    CHECK(Emit("a < b == c") == "((a < b) == c)");
    CHECK(Emit("a == b && c != d") == "(a == b && c != d)");
    CHECK(Emit("a + b == c") == "(a + b == c)");
    CHECK(Emit("-x") == "(-x)");
    CHECK(Emit("!flag") == "(!flag)");
    CHECK(Emit("- -x") == "(-(-x))");
    CHECK(Emit("!!a") == "(!(!a))");
    CHECK(Emit("!(a && b)") == "(!(a && b))");
    CHECK(Emit("-a + b") == "(-a + b)");
    CHECK(Emit("a - -b") == "(a - (-b))");
    CHECK(Emit("a * -b") == "(a * -b)");
    CHECK(Emit("-(a + b)") == "(-(a + b))");
    CHECK(Emit("x") == "x");
    CHECK(Emit("42") == "42");
    CHECK(Emit("(x)") == "x");
}

TEST_CASE("emit: literals and enums") {
    CHECK(Emit("0.5") == "0.5f");
    CHECK(Emit("2.") == "2.f");
    CHECK(Emit(".5") == ".5f");
    CHECK(Emit("1e3") == "1e3f");
    CHECK(Emit("1.5f") == "1.5f");
    CHECK(Emit("1.5F") == "1.5f");
    CHECK(Emit("42") == "42");
    CHECK(Emit("0x1F") == "0x1F");
    CHECK(Emit("-1") == "(-1)");
    CHECK(Emit("true") == "true");
    CHECK(Emit("false") == "false");
    CHECK(Emit("\"a\\\"b\\n\"") == "\"a\\\"b\\n\"");
    CHECK(Emit("'tab\\there\\\\'") == "\"tab\\there\\\\\"");
    CHECK(Emit("\"\\0x\"") == "\"\\000x\"");
    CHECK(Emit("Mode::Aim") == "Mode::Aim");
    CHECK(Emit("mode == Mode::Aim") == "(mode == Mode::Aim)");
    CHECK(Emit("x * 0.5 + 1") == "(x * 0.5f + 1)");

    Ast synthetic;
    synthetic.kind = Ast::Kind::Literal;
    synthetic.literal = VFloat(0.25);
    CHECK(EmitCpp(synthetic, EmitOptions()) == "0.25f");
    synthetic.literal = VInt(-3);
    CHECK(EmitCpp(synthetic, EmitOptions()) == "-3");
}

TEST_CASE("emit: path callback and indices") {
    Ast a;
    std::vector<std::string> seen;
    auto cb = [&](const std::string& p) {
        seen.push_back(p);
        if (p == "items.size") return std::string("items.size()");
        if (p == "slots.size") return std::string("IM_ARRAYSIZE(slots)");
        if (p.compare(0, 6, "state.") == 0) return "s." + p.substr(6);
        return "params." + p;
    };
    REQUIRE(ParseOk("items.size > 0 && state.selectedTab == 1", a));
    CHECK(EmitCpp(a, cb) == "(items.size() > 0 && s.selectedTab == 1)");
    CHECK(seen == std::vector<std::string>{"items.size", "state.selectedTab"});

    seen.clear();
    REQUIRE(ParseOk("items[i + 1].name", a));
    CHECK(EmitCpp(a, cb) == "params.items[params.i + 1].name");
    CHECK(seen == std::vector<std::string>{"items[0].name", "i"});

    REQUIRE(ParseOk("grid[2][items[state.sel].idx] > slots.size", a));
    CHECK(EmitCpp(a, cb) == "(params.grid[2][params.items[s.sel].idx] > IM_ARRAYSIZE(slots))");

    REQUIRE(ParseOk("items[0].name", a));
    CHECK(EmitCpp(a, EmitOptions()) == "items[0].name");

    auto wrapping = [](const std::string& p) {
        bool size = p.size() > 5 && p.compare(p.size() - 5, 5, ".size") == 0;
        return size ? "IM_ARRAYSIZE(" + p.substr(0, p.size() - 5) + ")" : p;
    };
    REQUIRE(ParseOk("rows[i].cols.size", a));
    CHECK(EmitCpp(a, wrapping) == "IM_ARRAYSIZE(rows[i].cols)");
}

TEST_CASE("emit: string comparisons") {
    Ast a;
    REQUIRE(ParseOk("name == \"bob\"", a));
    CHECK(EmitCpp(a, EmitOptions()) == "(name == \"bob\")");

    EmitOptions o;
    o.path_is_char_array = [](const std::string& p) { return p == "name" || p == "tags[0]"; };
    CHECK(EmitCpp(a, o) == "(strcmp(name, \"bob\") == 0)");

    REQUIRE(ParseOk("\"bob\" != name && alive", a));
    CHECK(EmitCpp(a, o) == "(strcmp(\"bob\", name) != 0 && alive)");

    REQUIRE(ParseOk("name == other", a));
    CHECK(EmitCpp(a, o) == "(strcmp(name, other) == 0)");
    o.path_to_cstr = [](const std::string& p) { return p == "other" ? "other.c_str()" : p; };
    CHECK(EmitCpp(a, o) == "(strcmp(name, other.c_str()) == 0)");

    REQUIRE(ParseOk("tags[i] == \"x\"", a));
    CHECK(EmitCpp(a, o) == "(strcmp(tags[i], \"x\") == 0)");

    REQUIRE(ParseOk("other == \"x\"", a));
    CHECK(EmitCpp(a, o) == "(other == \"x\")");

    REQUIRE(ParseOk("(name == \"a\") == flag", a));
    CHECK(EmitCpp(a, o) == "((strcmp(name, \"a\") == 0) == flag)");
}

TEST_CASE("collect paths") {
    Ast a;
    REQUIRE(ParseOk("player.health > 0 && items[i + 1].name == player.name && items.size > player.health", a));
    std::vector<std::string> expect = {"player.health", "items[0].name", "i", "player.name", "items.size"};
    CHECK(CollectPaths(a) == expect);
    REQUIRE(ParseOk("items[2].name == items[2].name", a));
    CHECK(CollectPaths(a) == std::vector<std::string>{"items[2].name"});
    REQUIRE(ParseOk("1 + 2", a));
    CHECK(CollectPaths(a).empty());
    REQUIRE(ParseOk("Mode::Aim", a));
    CHECK(CollectPaths(a).empty());
}

TEST_CASE("interpolation: parsing") {
    std::vector<InterpSegment> segs;
    Error e;
    REQUIRE(ParseInterpolation("HP: {player.health:%.0f} / {player.max_hp}", segs, &e));
    REQUIRE(segs.size() == 4);
    CHECK(segs[0].is_expr == false);
    CHECK(segs[0].text_or_expr == "HP: ");
    CHECK(segs[1].is_expr);
    CHECK(segs[1].text_or_expr == "player.health");
    CHECK(segs[1].fmt == "%.0f");
    CHECK(segs[1].is_icon == false);
    CHECK(segs[2].text_or_expr == " / ");
    CHECK(segs[3].is_expr);
    CHECK(segs[3].text_or_expr == "player.max_hp");
    CHECK(segs[3].fmt.empty());

    REQUIRE(ParseInterpolation("{{x}} {y} }}", segs, &e));
    REQUIRE(segs.size() == 3);
    CHECK(segs[0].text_or_expr == "{x} ");
    CHECK(segs[1].is_expr);
    CHECK(segs[1].text_or_expr == "y");
    CHECK(segs[2].text_or_expr == " }");

    REQUIRE(ParseInterpolation("{icon:fa-play} Play", segs, &e));
    REQUIRE(segs.size() == 2);
    CHECK(segs[0].is_icon);
    CHECK(segs[0].is_expr == false);
    CHECK(segs[0].text_or_expr == "fa-play");
    CHECK(segs[1].text_or_expr == " Play");

    REQUIRE(ParseInterpolation("{ mode == Mode::Aim }", segs, &e));
    REQUIRE(segs.size() == 1);
    CHECK(segs[0].is_expr);
    CHECK(segs[0].text_or_expr == "mode == Mode::Aim");
    CHECK(segs[0].fmt.empty());

    REQUIRE(ParseInterpolation("{ items.size - 1 : %5d }|{icon:%d}", segs, &e));
    REQUIRE(segs.size() == 3);
    CHECK(segs[0].text_or_expr == "items.size - 1");
    CHECK(segs[0].fmt == "%5d");
    CHECK(segs[2].is_expr);
    CHECK(segs[2].is_icon == false);
    CHECK(segs[2].text_or_expr == "icon");
    CHECK(segs[2].fmt == "%d");

    REQUIRE(ParseInterpolation("{\"}\" + name}", segs, &e));
    REQUIRE(segs.size() == 1);
    CHECK(segs[0].text_or_expr == "\"}\" + name");

    REQUIRE(ParseInterpolation("plain text", segs, &e));
    REQUIRE(segs.size() == 1);
    CHECK(segs[0].is_expr == false);
    CHECK(segs[0].text_or_expr == "plain text");

    REQUIRE(ParseInterpolation("", segs, &e));
    CHECK(segs.empty());

    REQUIRE(ParseInterpolation("a } b", segs, &e));
    REQUIRE(segs.size() == 1);
    CHECK(segs[0].text_or_expr == "a } b");

    REQUIRE(ParseInterpolation("{a}{b}", segs, &e));
    REQUIRE(segs.size() == 2);
    CHECK(segs[0].text_or_expr == "a");
    CHECK(segs[1].text_or_expr == "b");
}

TEST_CASE("interpolation: errors") {
    std::vector<InterpSegment> segs;
    Error e;
    REQUIRE_FALSE(ParseInterpolation("HP: {player.health", segs, &e));
    CHECK(e.message == "unterminated '{' in interpolation");
    CHECK(e.col == 5);
    CHECK(e.line == 1);
    REQUIRE_FALSE(ParseInterpolation("x {} y", segs, &e));
    CHECK(e.message == "empty '{}' in interpolation");
    CHECK(e.col == 3);
    REQUIRE_FALSE(ParseInterpolation("{  }", segs, &e));
    CHECK(e.message == "empty '{}' in interpolation");
    REQUIRE_FALSE(ParseInterpolation("{icon:}", segs, &e));
    CHECK(e.message == "missing icon name");
    REQUIRE_FALSE(ParseInterpolation("{:%d}", segs, &e));
    CHECK(e.message == "missing expression before ':'");
    REQUIRE_FALSE(ParseInterpolation("{\"unterminated}", segs, &e));
    CHECK(e.message == "unterminated '{' in interpolation");
    REQUIRE(ParseInterpolation("ok {x}", segs, nullptr));
    REQUIRE_FALSE(ParseInterpolation("{x", segs, nullptr));
}

TEST_CASE("format value: defaults") {
    CHECK(FormatValue(VInt(42), "") == "42");
    CHECK(FormatValue(VInt(-7), "") == "-7");
    CHECK(FormatValue(VFloat(3.14159), "") == "3.14");
    CHECK(FormatValue(VFloat(2), "") == "2.00");
    CHECK(FormatValue(VStr("abc"), "") == "abc");
    CHECK(FormatValue(VBool(true), "") == "true");
    CHECK(FormatValue(VBool(false), "") == "false");
    CHECK(FormatValue(Value(), "") == "");
    Value v2;
    v2.kind = Value::Kind::Vec2;
    v2.v2.x = 1.5f;
    v2.v2.y = 2;
    CHECK(FormatValue(v2, "") == "1.5, 2");
    Value col;
    col.kind = Value::Kind::Color;
    col.v4.x = 1;
    col.v4.y = 0;
    col.v4.z = 0.5f;
    col.v4.w = 1;
    CHECK(FormatValue(col, "") == "#ff0080ff");
    Value list;
    list.kind = Value::Kind::List;
    list.list.push_back(VInt(1));
    list.list.push_back(VStr("a"));
    CHECK(FormatValue(list, "") == "[1, a]");
    CHECK(std::string(DefaultFormatFor(Value::Kind::Int)) == "%d");
    CHECK(std::string(DefaultFormatFor(Value::Kind::Float)) == "%.2f");
    CHECK(std::string(DefaultFormatFor(Value::Kind::String)) == "%s");
    CHECK(std::string(DefaultFormatFor(Value::Kind::Bool)) == "%s");
}

TEST_CASE("format value: explicit formats") {
    CHECK(FormatValue(VInt(42), "%05d") == "00042");
    CHECK(FormatValue(VInt(42), "%i") == "42");
    CHECK(FormatValue(VInt(42), "%lld") == "42");
    CHECK(FormatValue(VInt(42), "%ld") == "42");
    CHECK(FormatValue(VInt(255), "%x") == "ff");
    CHECK(FormatValue(VInt(255), "%X") == "FF");
    CHECK(FormatValue(VInt(255), "%#x") == "0xff");
    CHECK(FormatValue(VInt(8), "%o") == "10");
    CHECK(FormatValue(VInt(42), "%u") == "42");
    CHECK(FormatValue(VInt(65), "%c") == "A");
    CHECK(FormatValue(VInt(42), "%.1f") == "42.0");
    CHECK(FormatValue(VInt(42), "%8.3e") == "4.200e+01");
    CHECK(FormatValue(VFloat(3.14159), "%.0f") == "3");
    CHECK(FormatValue(VFloat(3.99), "%d") == "3");
    CHECK(FormatValue(VFloat(0.5), "%g") == "0.5");
    CHECK(FormatValue(VFloat(0.5), "%-6.1f|") == "0.5   |");
    CHECK(FormatValue(VBool(true), "%d") == "1");
    CHECK(FormatValue(VBool(true), "%s") == "true");
    CHECK(FormatValue(VBool(false), "%.1f") == "0.0");
    CHECK(FormatValue(VStr("ab"), "%5s|") == "   ab|");
    CHECK(FormatValue(VStr("ab"), "%-5s|") == "ab   |");
    CHECK(FormatValue(VInt(42), "%s") == "42");
    CHECK(FormatValue(VFloat(1.5), "%6s|") == "  1.50|");
    CHECK(FormatValue(VInt(5), "100%% done: %d%%") == "100% done: 5%");
    CHECK(FormatValue(VStr("x"), "[%s]") == "[x]");
    CHECK(FormatValue(VInt(7), "no spec %%") == "no spec %");
    CHECK(FormatValue(VInt(7), "plain") == "plain");
}

TEST_CASE("format value: mismatched or invalid specs fall back") {
    CHECK(FormatValue(VStr("abc"), "%d") == "abc");
    CHECK(FormatValue(VStr("abc"), "%.2f") == "abc");
    CHECK(FormatValue(VStr("abc"), "%x") == "abc");
    CHECK(FormatValue(VInt(5), "%d %d") == "5");
    CHECK(FormatValue(VInt(5), "%*d") == "5");
    CHECK(FormatValue(VInt(5), "%.*f") == "5");
    CHECK(FormatValue(VInt(5), "%p") == "5");
    CHECK(FormatValue(VInt(5), "%n") == "5");
    CHECK(FormatValue(VInt(5), "%") == "5");
    CHECK(FormatValue(VInt(5), "%5") == "5");
    CHECK(FormatValue(VInt(5), "%q") == "5");
    CHECK(FormatValue(VFloat(1.25), "%s%s") == "1.25");
    CHECK(FormatValue(Value(), "%d") == "");
    CHECK(FormatValue(VBool(true), "%x") == "1");
    Value list;
    list.kind = Value::Kind::List;
    list.list.push_back(VInt(1));
    CHECK(FormatValue(list, "%d") == "[1]");
    CHECK(FormatValue(list, "<%s>") == "<[1]>");
}

TEST_CASE("interpolation end to end with eval") {
    MapValueEnv env = MakeValueEnv();
    std::vector<InterpSegment> segs;
    Error e;
    REQUIRE(ParseInterpolation("HP: {player.health:%.0f} / {player.hp} {player.name} {items.size * 2}", segs, &e));
    std::string out;
    for (const InterpSegment& seg : segs) {
        if (!seg.is_expr) {
            out += seg.text_or_expr;
            continue;
        }
        Ast ast;
        REQUIRE(Parse(seg.text_or_expr, ast, &e));
        Value v;
        REQUIRE(Eval(ast, env, v, &e));
        out += FormatValue(v, seg.fmt);
    }
    CHECK(out == "HP: 76 / 75 bob 4");
}

TEST_CASE("int arithmetic wraps instead of trapping") {
    const long long kMin = (-9223372036854775807LL) - 1;
    MapValueEnv env = MakeValueEnv();
    env.values["big"] = VInt(kMin);
    CHECK(EvalOk("big / -1", env) == VInt(kMin));
    CHECK(EvalOk("big % -1", env) == VInt(0));
    CHECK(EvalOk("-big", env) == VInt(kMin));
    CHECK(EvalOk("big - 1", env) == VInt(9223372036854775807LL));
    CHECK(EvalOk("9223372036854775807 + 1", env) == VInt(kMin));
    CHECK(EvalOk("9223372036854775807 * 2", env) == VInt(-2));
    CHECK(EvalOk("7 / -1", env) == VInt(-7));
    CHECK(EvalOk("7 % -1", env) == VInt(0));
    CHECK(EvalErr("big / 0", env).message == "division by zero");
    CHECK(EvalErr("big % 0", env).message == "division by zero");
}

TEST_CASE("parser limits nesting depth and token count") {
    Ast ast;
    std::string ok = std::string(60, '(') + "1" + std::string(60, ')');
    CHECK(ParseOk(ok, ast));
    std::string deep = std::string(200, '(') + "1" + std::string(200, ')');
    CHECK(ParseErr(deep).message == "expression nested too deeply");
    std::string nots = std::string(200, '!') + "true";
    CHECK(ParseErr(nots).message == "expression nested too deeply");
    std::string index = "a";
    for (int i = 0; i < 100; ++i) index += "[b";
    for (int i = 0; i < 100; ++i) index += "]";
    CHECK(ParseErr(index).message == "expression nested too deeply");
    std::string chain = "1";
    for (int i = 0; i < 1500; ++i) chain += " + 1";
    CHECK(ParseErr(chain).message == "expression too long");
    std::string chain2 = "1";
    for (int i = 0; i < 400; ++i) chain2 += " + 1";
    MapValueEnv env = MakeValueEnv();
    CHECK(EvalOk(chain2, env) == VInt(401));
}

TEST_CASE("float literals are locale independent") {
    const char* names[] = {"de_DE.UTF-8", "de_DE.utf8", "de_DE", "German_Germany.1252", "fr_FR.UTF-8",
                           "French_France.1252"};
    bool comma = false;
    for (const char* n : names) {
        if (std::setlocale(LC_NUMERIC, n) && std::localeconv()->decimal_point[0] == ',') {
            comma = true;
            break;
        }
    }
    if (comma) {
        MapValueEnv env = MakeValueEnv();
        CHECK(EvalOk("1.5 + 0.25", env) == VFloat(1.75));
        CHECK(EvalOk("2.5e1", env) == VFloat(25.0));
    }
    std::setlocale(LC_NUMERIC, "C");
    MapValueEnv env = MakeValueEnv();
    CHECK(EvalOk("1.5 + 0.25", env) == VFloat(1.75));
}
