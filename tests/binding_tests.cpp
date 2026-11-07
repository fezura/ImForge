#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "imforge/imforge_expr.h"
#include "imforge/imforge_json.h"
#include "imforge/imforge_value.h"

#include "clang_scanner.h"
#include "compile_commands.h"
#include "panel_type_env.h"
#include "reflect_header.h"
#include "type_database.h"

#ifdef IMFORGE_HAVE_LIBCLANG
#ifdef _WIN32
#include <direct.h>
#define IMFORGE_MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#define IMFORGE_MKDIR(p) mkdir(p, 0755)
#endif
#endif

using namespace imforge;
using namespace imforge::binding;

namespace {

const char* const kTypesJson = R"({
  "schemaVersion": 1, "kind": "types",
  "structs": {
    "Item": { "header": "game/item.h", "fields": [
      {"name": "name", "type": "std::string", "ctype": "StdString"},
      {"name": "count", "type": "int", "ctype": "I32"},
      {"name": "price", "type": "float", "ctype": "Float"} ] },
    "Player": { "header": "game/player.h", "fields": [
      {"name": "name", "type": "char[32]", "ctype": "CharArray", "count": 32},
      {"name": "health", "type": "float", "ctype": "Float"},
      {"name": "pos", "type": "ImVec2", "ctype": "Vec2"},
      {"name": "tint", "type": "float[4]", "ctype": "FloatArray", "count": 4},
      {"name": "inv", "type": "std::vector<Item>", "ctype": "Vector", "elem": "Item"},
      {"name": "slots", "type": "Item[3]", "ctype": "Array", "elem": "Item", "count": 3},
      {"name": "mode", "type": "Mode", "ctype": "Enum"},
      {"name": "tex", "type": "ImTextureID", "ctype": "TexID"} ] },
    "EspSettings": { "header": "game/settings.h", "fields": [
      {"name": "enabled", "type": "bool", "ctype": "Bool"},
      {"name": "color", "type": "ImVec4", "ctype": "Vec4"} ] },
    "Settings": { "header": "game/settings.h", "fields": [
      {"name": "esp", "type": "EspSettings", "ctype": "Struct"},
      {"name": "aim_fov", "type": "float", "ctype": "Float"} ] }
  },
  "enums": { "Mode": { "header": "game/player.h", "values": [["Off", 0], ["Aim", 1], ["Silent", 2]] } }
})";

Json ParseOrFail(const char* text) {
    Json j;
    JsonError err;
    if (!ParseJson(text, j, &err)) FAIL("json: " << err.message << " at " << err.line << ":" << err.col);
    return j;
}

TypeDatabase LoadSpecDb() {
    TypeDatabase db;
    Error err;
    REQUIRE_MESSAGE(db.Load(ParseOrFail(kTypesJson), &err), err.message);
    return db;
}

std::vector<Param> SpecParams() {
    return {Param{"player", "Player", Json()}, Param{"cfg", "const Settings&", Json()}};
}

std::vector<StateVar> SpecState() {
    return {StateVar{"selectedTab", "int", Json(0)}, StateVar{"open", "bool", Json(true)},
            StateVar{"accent", "color", Json("#ff0000ff")}, StateVar{"cursor", "vec2", Json()}};
}

CType PathType(const TypeDatabase& db, const char* path) {
    CType t;
    std::string err;
    REQUIRE_MESSAGE(db.TypeOfPath(SpecParams(), path, t, &err), path << ": " << err);
    return t;
}

std::string PathError(const TypeDatabase& db, const char* path) {
    CType t;
    std::string err;
    REQUIRE_FALSE(db.TypeOfPath(SpecParams(), path, t, &err));
    REQUIRE_FALSE(err.empty());
    return err;
}

bool Contains(const std::string& s, const char* needle) { return s.find(needle) != std::string::npos; }

}  // namespace

TEST_CASE("NormalizeTypeName strips qualifiers and whitespace") {
    CHECK(NormalizeTypeName("const Player&") == "Player");
    CHECK(NormalizeTypeName("  struct  Settings ") == "Settings");
    CHECK(NormalizeTypeName("std::vector< Item >") == "std::vector<Item>");
    CHECK(NormalizeTypeName("unsigned   int") == "unsigned int");
    CHECK(NormalizeTypeName("std::__cxx11::string") == "std::string");
    CHECK(NormalizeTypeName("enum class Mode") == "Mode");
    CHECK(NormalizeTypeName("int const &") == "int");
    CHECK(NormalizeTypeName("::game::Player") == "game::Player");
}

TEST_CASE("TypeDatabase loads structs and enums from types.json") {
    TypeDatabase db = LoadSpecDb();
    CHECK(db.StructNames() == std::vector<std::string>{"EspSettings", "Item", "Player", "Settings"});
    CHECK(db.EnumNames() == std::vector<std::string>{"Mode"});
    const StructInfo* player = db.Struct("Player");
    REQUIRE(player);
    REQUIRE(player->fields.size() == 8);
    CHECK(player->fields[0].type.kind == CKind::CharArray);
    CHECK(player->fields[0].type.count == 32);
    CHECK(player->fields[2].type.kind == CKind::Vec2);
    CHECK(player->fields[4].type.kind == CKind::Vector);
    REQUIRE(player->fields[4].type.elem);
    CHECK(player->fields[4].type.elem->kind == CKind::Struct);
    CHECK(player->fields[4].type.elem->strct == db.Struct("Item"));
    CHECK(player->fields[5].type.kind == CKind::Array);
    CHECK(player->fields[5].type.count == 3);
    CHECK(player->fields[6].type.kind == CKind::Enum);
    CHECK(player->fields[6].type.enm == db.Enum("Mode"));
    CHECK(player->fields[7].type.kind == CKind::TexID);
    const EnumInfo* mode = db.Enum("Mode");
    REQUIRE(mode);
    CHECK(std::string(mode->name) == "Mode");
    REQUIRE(mode->values.size() == 3);
    CHECK(mode->values[1].first == "Aim");
    CHECK(mode->values[2].second == 2);
    CHECK(db.Struct("Nope") == nullptr);
    CHECK(db.Struct("const Player&") == player);
    CHECK(db.StructSource("Player")->header == "game/player.h");
}

TEST_CASE("TypeDatabase rejects malformed documents") {
    TypeDatabase db;
    Error err;
    CHECK_FALSE(db.Load(Json(3), &err));
    CHECK_FALSE(db.Load(ParseOrFail(R"({"kind":"panel"})"), &err));
    CHECK(Contains(err.message, "kind"));
    CHECK_FALSE(db.Load(ParseOrFail(R"({"structs":{"A":{"fields":[{"type":"int"}]}}})"), &err));
    CHECK(Contains(err.message, "name"));
    CHECK_FALSE(db.Load(ParseOrFail(R"({"structs":{"A":{"fields":[{"name":"x","ctype":"Nope"}]}}})"), &err));
    CHECK(Contains(err.message, "ctype"));
    CHECK(db.Empty());
    CHECK(db.Load(ParseOrFail(R"({"structs":{},"enums":{}})"), &err));
}

TEST_CASE("TypeOfPath walks structs, vectors, arrays, size and enums") {
    TypeDatabase db = LoadSpecDb();
    CHECK(PathType(db, "player").kind == CKind::Struct);
    CHECK(PathType(db, "player.health").kind == CKind::Float);
    CHECK(PathType(db, "cfg.esp.enabled").kind == CKind::Bool);
    CHECK(PathType(db, "cfg.esp.color").kind == CKind::Vec4);
    CHECK(PathType(db, "cfg.esp.color.w").kind == CKind::Float);
    CHECK(PathType(db, "player.pos").kind == CKind::Vec2);
    CHECK(PathType(db, "player.pos.y").kind == CKind::Float);
    CHECK(PathType(db, "player.inv").kind == CKind::Vector);
    CHECK(PathType(db, "player.inv.size").kind == CKind::I64);
    CHECK(PathType(db, "player.inv[2].name").kind == CKind::StdString);
    CHECK(PathType(db, "player.inv[i + 1].count").kind == CKind::I32);
    CHECK(PathType(db, "player.slots[1].price").kind == CKind::Float);
    CHECK(PathType(db, "player.slots.size").kind == CKind::I64);
    CType name = PathType(db, "player.name");
    CHECK(name.kind == CKind::CharArray);
    CHECK(name.count == 32);
    CHECK(PathType(db, "player.name.size").kind == CKind::I64);
    CHECK(PathType(db, "player.name[3]").kind == CKind::I8);
    CHECK(PathType(db, "player.tint[0]").kind == CKind::Float);
    CHECK(PathType(db, "player.tint.size").kind == CKind::I64);
    CType mode = PathType(db, "player.mode");
    CHECK(mode.kind == CKind::Enum);
    REQUIRE(mode.enm);
    CHECK(std::string(mode.enm->name) == "Mode");
    CHECK(PathType(db, "player.tex").kind == CKind::TexID);

    CHECK(Contains(PathError(db, "player.nope"), "no field 'nope' in 'Player'"));
    CHECK(Contains(PathError(db, "bogus.x"), "unknown parameter 'bogus'"));
    CHECK(Contains(PathError(db, "state.open"), "state"));
    CHECK(Contains(PathError(db, "player.health.x"), "cannot access field 'x'"));
    CHECK(Contains(PathError(db, "player.health[0]"), "cannot index"));
    CHECK(Contains(PathError(db, "player.slots[5]"), "out of range"));
    CHECK(Contains(PathError(db, "player.tint[4]"), "out of range"));
    CHECK(Contains(PathError(db, "player.inv["), "']'"));
    CHECK(Contains(PathError(db, "player."), "field name"));
    CHECK(Contains(PathError(db, "player.pos.z"), "no component 'z'"));
    CHECK(Contains(PathError(db, ""), "parameter name"));
    CHECK(Contains(PathError(db, "player health"), "unexpected character"));

    std::vector<Param> unknown = {Param{"foo", "Mystery", Json()}};
    CType t;
    std::string err;
    CHECK_FALSE(db.TypeOfPath(unknown, "foo.x", t, &err));
    CHECK(Contains(err, "unknown type 'Mystery'"));
    std::vector<Param> vec_param = {Param{"items", "std::vector<Item>", Json()}};
    REQUIRE(db.TypeOfPath(vec_param, "items[0].price", t, &err));
    CHECK(t.kind == CKind::Float);
    std::vector<Param> fund = {Param{"speed", "float", Json()}};
    REQUIRE(db.TypeOfPath(fund, "speed", t, &err));
    CHECK(t.kind == CKind::Float);
}

TEST_CASE("Loading a second document merges and resolves earlier references") {
    TypeDatabase db;
    Error err;
    REQUIRE(db.Load(ParseOrFail(R"({"structs":{"Inv":{"fields":[
        {"name":"items","type":"std::vector<Widget>","ctype":"Vector","elem":"Widget"},
        {"name":"grid","type":"int[2][3]","ctype":"Array"}]}}})"), &err));
    std::vector<Param> params = {Param{"inv", "Inv", Json()}};
    CType t;
    std::string perr;
    CHECK_FALSE(db.TypeOfPath(params, "inv.items[0].w", t, &perr));
    CHECK(Contains(perr, "no type information"));
    REQUIRE(db.TypeOfPath(params, "inv.grid[1][2]", t, &perr));
    CHECK(t.kind == CKind::I32);
    REQUIRE(db.TypeOfPath(params, "inv.grid[0]", t, &perr));
    CHECK(t.kind == CKind::IntArray);
    CHECK(t.count == 3);
    REQUIRE(db.Load(ParseOrFail(R"({"structs":{"Widget":{"fields":[{"name":"w","type":"float"}]}}})"), &err));
    REQUIRE(db.TypeOfPath(params, "inv.items[0].w", t, &perr));
    CHECK(t.kind == CKind::Float);
    CHECK(db.Struct("Inv")->fields[0].type.elem->strct == db.Struct("Widget"));

    TypeDatabase spec = LoadSpecDb();
    Json saved = spec.Save();
    TypeDatabase again;
    REQUIRE(again.Load(saved, &err));
    CHECK(DumpJson(again.Save()) == DumpJson(saved));
    CHECK(saved["structs"]["Player"]["fields"][4]["elem"].AsString() == "Item");
    CHECK(saved["structs"]["Player"]["fields"][0]["count"].AsInt() == 32);
    CHECK(saved["enums"]["Mode"]["values"][1][0].AsString() == "Aim");
    CHECK(saved["kind"].AsString() == "types");
}

TEST_CASE("Complete offers the next path segment") {
    TypeDatabase db = LoadSpecDb();
    std::vector<Param> params = SpecParams();
    CHECK(db.Complete(params, "") == std::vector<std::string>{"Mode", "cfg", "player"});
    CHECK(db.Complete(params, "pl") == std::vector<std::string>{"player"});
    CHECK(db.Complete(params, "player") == std::vector<std::string>{"player"});
    CHECK(db.Complete(params, "player.") ==
          std::vector<std::string>{"player.health", "player.inv", "player.mode", "player.name",
                                   "player.pos", "player.slots", "player.tex", "player.tint"});
    CHECK(db.Complete(params, "player.in") == std::vector<std::string>{"player.inv"});
    CHECK(db.Complete(params, "player.inv.") == std::vector<std::string>{"player.inv.size"});
    CHECK(db.Complete(params, "player.name.s") == std::vector<std::string>{"player.name.size"});
    CHECK(db.Complete(params, "player.inv[0].") ==
          std::vector<std::string>{"player.inv[0].count", "player.inv[0].name", "player.inv[0].price"});
    CHECK(db.Complete(params, "player.inv[0]") ==
          std::vector<std::string>{"player.inv[0].count", "player.inv[0].name", "player.inv[0].price"});
    CHECK(db.Complete(params, "player.pos.") == std::vector<std::string>{"player.pos.x", "player.pos.y"});
    CHECK(db.Complete(params, "cfg.esp.en") == std::vector<std::string>{"cfg.esp.enabled"});
    CHECK(db.Complete(params, "cfg.esp.color.") ==
          std::vector<std::string>{"cfg.esp.color.w", "cfg.esp.color.x", "cfg.esp.color.y",
                                   "cfg.esp.color.z"});
    CHECK(db.Complete(params, "Mode::") == std::vector<std::string>{"Mode::Aim", "Mode::Off", "Mode::Silent"});
    CHECK(db.Complete(params, "Mode::S") == std::vector<std::string>{"Mode::Silent"});
    CHECK(db.Complete(params, "Mo") == std::vector<std::string>{"Mode"});
    CHECK(db.Complete(params, "nope.").empty());
    CHECK(db.Complete(params, "player.inv[").empty());
    CHECK(db.Complete(params, "player.health.").empty());
    CHECK(db.Complete(params, "player.zzz").empty());
}

TEST_CASE("GenerateReflectHeader emits includes and macros in dependency order") {
    TypeDatabase db = LoadSpecDb();
    std::string out, err;
    ReflectHeaderOptions opt;
    REQUIRE_MESSAGE(GenerateReflectHeader(db, opt, out, &err), err);
    CHECK(out == "#pragma once\n"
                 "// Generated by imforge-reflect. Do not edit.\n"
                 "\n"
                 "#include \"game/item.h\"\n"
                 "#include \"game/player.h\"\n"
                 "#include \"game/settings.h\"\n"
                 "\n"
                 "#include \"imforge_value.h\"\n"
                 "\n"
                 "IMFORGE_REFLECT_ENUM(Mode, Off, Aim, Silent);\n"
                 "IMFORGE_REFLECT(EspSettings, enabled, color);\n"
                 "IMFORGE_REFLECT(Item, name, count, price);\n"
                 "IMFORGE_REFLECT(Player, name, health, pos, tint, inv, slots, mode, tex);\n"
                 "IMFORGE_REFLECT(Settings, esp, aim_fov);\n");

    opt.types = {"Settings"};
    opt.value_include = "imforge/imforge_value.h";
    REQUIRE(GenerateReflectHeader(db, opt, out, &err));
    CHECK(out == "#pragma once\n"
                 "// Generated by imforge-reflect. Do not edit.\n"
                 "\n"
                 "#include \"game/settings.h\"\n"
                 "\n"
                 "#include \"imforge/imforge_value.h\"\n"
                 "\n"
                 "IMFORGE_REFLECT(EspSettings, enabled, color);\n"
                 "IMFORGE_REFLECT(Settings, esp, aim_fov);\n");

    opt.types = {"Player"};
    opt.include_type_headers = false;
    REQUIRE(GenerateReflectHeader(db, opt, out, &err));
    CHECK(out == "#pragma once\n"
                 "// Generated by imforge-reflect. Do not edit.\n"
                 "\n"
                 "#include \"imforge/imforge_value.h\"\n"
                 "\n"
                 "IMFORGE_REFLECT_ENUM(Mode, Off, Aim, Silent);\n"
                 "IMFORGE_REFLECT(Item, name, count, price);\n"
                 "IMFORGE_REFLECT(Player, name, health, pos, tint, inv, slots, mode, tex);\n");

    opt.types = {"Ghost"};
    CHECK_FALSE(GenerateReflectHeader(db, opt, out, &err));
    CHECK(Contains(err, "Ghost"));

    std::vector<std::string> enums, structs;
    REQUIRE(ReflectOrder(db, {}, enums, structs, &err));
    CHECK(enums == std::vector<std::string>{"Mode"});
    CHECK(structs == std::vector<std::string>{"EspSettings", "Item", "Player", "Settings"});
}

TEST_CASE("PanelTypeEnv resolves params, state and enum literals for the expression checker") {
    TypeDatabase db = LoadSpecDb();
    PanelTypeEnv env(db, SpecParams(), SpecState());
    CType t;
    REQUIRE(env.TypeOfPath("player.health", t));
    CHECK(t.kind == CKind::Float);
    REQUIRE(env.TypeOfPath("state.selectedTab", t));
    CHECK(t.kind == CKind::I32);
    REQUIRE(env.TypeOfPath("state.open", t));
    CHECK(t.kind == CKind::Bool);
    REQUIRE(env.TypeOfPath("state.accent", t));
    CHECK(t.kind == CKind::Vec4);
    REQUIRE(env.TypeOfPath("state.cursor.x", t));
    CHECK(t.kind == CKind::Float);
    CHECK_FALSE(env.TypeOfPath("state.nope", t));
    CHECK(Contains(env.last_error, "unknown state variable 'nope'"));
    CHECK_FALSE(env.TypeOfPath("state", t));
    CHECK_FALSE(env.TypeOfPath("player.nope", t));
    CHECK(Contains(env.last_error, "no field 'nope'"));
    long long v = -1;
    CHECK(env.EnumValue("Mode", "Silent", v));
    CHECK(v == 2);
    CHECK_FALSE(env.EnumValue("Mode", "Nope", v));
    CHECK_FALSE(env.EnumValue("Nope", "Off", v));

    struct Case {
        const char* src;
        expr::Type expected;
    };
    const Case cases[] = {
        {"player.health > 0 && cfg.esp.enabled", expr::Type::Bool},
        {"player.mode == Mode::Aim", expr::Type::Bool},
        {"player.inv.size > 2", expr::Type::Bool},
        {"player.inv[state.selectedTab].count * 2", expr::Type::Int},
        {"player.health / 2", expr::Type::Float},
        {"state.selectedTab == 1 || !state.open", expr::Type::Bool},
        {"player.name + \"!\"", expr::Type::String},
        {"player.inv[0].name", expr::Type::String},
    };
    for (const Case& c : cases) {
        expr::Ast ast;
        Error err;
        REQUIRE_MESSAGE(expr::Parse(c.src, ast, &err), c.src << ": " << err.message);
        expr::Type result = expr::Type::Error;
        CHECK_MESSAGE(expr::TypeCheck(ast, env, result, &err), c.src << ": " << err.message);
        CHECK(result == c.expected);
    }
    expr::Ast bad;
    Error err;
    REQUIRE(expr::Parse("player.ghost > 1", bad, &err));
    expr::Type result;
    CHECK_FALSE(expr::TypeCheck(bad, env, result, &err));
    CHECK(Contains(err.message, "player.ghost"));

    std::vector<std::string> c = env.Complete("");
    CHECK(c == std::vector<std::string>{"Mode", "cfg", "player", "state"});
    CHECK(env.Complete("st") == std::vector<std::string>{"state"});
    CHECK(env.Complete("state.") ==
          std::vector<std::string>{"state.accent", "state.cursor", "state.open", "state.selectedTab"});
    CHECK(env.Complete("state.se") == std::vector<std::string>{"state.selectedTab"});
    CHECK(env.Complete("state.cursor.") == std::vector<std::string>{"state.cursor.x", "state.cursor.y"});
    CHECK(env.Complete("player.he") == std::vector<std::string>{"player.health"});

    TypeDatabase ns;
    REQUIRE(ns.Load(ParseOrFail(R"({"enums":{"game::Mode":{"values":[["Off",0],["On",7]]}}})"), nullptr));
    PanelTypeEnv nsenv(ns, {}, {});
    CHECK(nsenv.EnumValue("Mode", "On", v));
    CHECK(v == 7);
}

struct RtItem {
    float price;
    int count;
};
enum class RtMode { Idle, Run };
struct RtPlayer {
    float health;
    char name[16];
    float tint[3];
    RtItem best;
    std::vector<RtItem> items;
    RtMode mode;
    imforge::Vec2 pos;
};
IMFORGE_REFLECT(RtItem, price, count);
IMFORGE_REFLECT_ENUM(RtMode, Idle, Run);
IMFORGE_REFLECT(RtPlayer, health, name, tint, best, items, mode, pos);

TEST_CASE("FromRegistry imports IMFORGE_REFLECT tables") {
    TypeDatabase db;
    REQUIRE(db.FromRegistry(TypeRegistry::Global()));
    const StructInfo* p = db.Struct("RtPlayer");
    REQUIRE(p);
    REQUIRE(p->fields.size() == 7);
    CHECK(p->fields[1].type.kind == CKind::CharArray);
    CHECK(p->fields[1].type.count == 16);
    CHECK(p->fields[2].type.kind == CKind::FloatArray);
    CHECK(p->fields[3].type.kind == CKind::Struct);
    CHECK(p->fields[3].type.strct == db.Struct("RtItem"));
    CHECK(p->fields[4].type.kind == CKind::Vector);
    REQUIRE(p->fields[4].type.elem);
    CHECK(p->fields[4].type.elem->strct == db.Struct("RtItem"));
    CHECK(p->fields[5].type.kind == CKind::Enum);
    CHECK(p->fields[5].type.enm == db.Enum("RtMode"));
    CHECK(p->fields[6].type.kind == CKind::Vec2);
    CHECK(p->fields[1].offset == offsetof(RtPlayer, name));
    CHECK(p->size == sizeof(RtPlayer));
    Json saved = db.Save();
    CHECK(saved["structs"]["RtPlayer"]["fields"][4]["type"].AsString() == "std::vector<RtItem>");
    CHECK(saved["structs"]["RtPlayer"]["fields"][4]["elem"].AsString() == "RtItem");
    CHECK(saved["structs"]["RtPlayer"]["fields"][1]["type"].AsString() == "char[16]");
    CHECK(saved["enums"]["RtMode"]["values"][1][0].AsString() == "Run");
    std::vector<Param> params = {Param{"p", "RtPlayer", Json()}};
    CType t;
    REQUIRE(db.TypeOfPath(params, "p.items[0].price", t, nullptr));
    CHECK(t.kind == CKind::Float);
    std::string header, err;
    ReflectHeaderOptions opt;
    opt.types = {"RtPlayer"};
    REQUIRE(GenerateReflectHeader(db, opt, header, &err));
    CHECK(Contains(header.c_str(), "IMFORGE_REFLECT(RtItem, price, count);\nIMFORGE_REFLECT(RtPlayer, health, name, tint, best, items, mode, pos);\n"));
    CHECK(Contains(header.c_str(), "IMFORGE_REFLECT_ENUM(RtMode, Idle, Run);\n"));
}

TEST_CASE("compile_commands parsing and clang argument cleanup") {
    std::vector<std::string> a = SplitCommandLine("g++ -DNAME=\"a b\" '-Ic:/x y' -c src\\ file.cpp -o out.o");
    CHECK(a == std::vector<std::string>{"g++", "-DNAME=a b", "-Ic:/x y", "-c", "src file.cpp", "-o", "out.o"});
    Json doc = ParseOrFail(R"([
      {"directory": "/proj/build", "command": "/usr/bin/c++ -I../include -isystem /opt/inc -std=c++17 -MD -MF a.d -o a.o -c ../src/a.cpp", "file": "../src/a.cpp"},
      {"directory": "C:/proj/build", "arguments": ["cl.exe", "/I", "C:/proj/include", "/std:c++17", "/c", "C:/proj/src/b.cpp", "/Fob.obj"], "file": "C:/proj/src/b.cpp"}
    ])");
    std::vector<CompileCommand> ccs;
    std::string err;
    REQUIRE_MESSAGE(ParseCompileCommands(doc, ccs, &err), err);
    REQUIRE(ccs.size() == 2);
    CHECK(ClangArgsFor(ccs[0]) ==
          std::vector<std::string>{"-working-directory=/proj/build", "-I../include", "-isystem", "/opt/inc", "-std=c++17"});
    CHECK(IncludeDirsOf(ccs[0]) == std::vector<std::string>{"/proj/build/../include", "/opt/inc"});
    CHECK(ClangArgsFor(ccs[1]) ==
          std::vector<std::string>{"-working-directory=C:/proj/build", "--driver-mode=cl", "/I", "C:/proj/include", "/std:c++17"});
    CHECK(IncludeDirsOf(ccs[1]) == std::vector<std::string>{"C:/proj/include"});
    CHECK(JoinPath("C:\\a\\b", "..\\c.h") == "C:/a/b/../c.h");
    CHECK(JoinPath("/x", "/abs/y.h") == "/abs/y.h");
    CHECK(FileBaseName("a/b\\c.h") == "c.h");
    CHECK_FALSE(ParseCompileCommands(Json(1), ccs, &err));
}

TEST_CASE("ScanTypes without libclang reports a clear message") {
    if (HaveLibClang()) return;
    ScanOptions opt;
    opt.headers = {"nothing.h"};
    opt.types = {"Player"};
    Json out;
    std::vector<std::string> log;
    CHECK_FALSE(ScanTypes(opt, out, log));
    REQUIRE(log.size() == 1);
    CHECK(Contains(log[0], "without libclang"));
}

#ifdef IMFORGE_HAVE_LIBCLANG

namespace {

std::string TempDir() {
    const char* candidates[] = {std::getenv("TMPDIR"), std::getenv("TEMP"), std::getenv("TMP")};
    for (const char* c : candidates)
        if (c && *c) return NormalizePath(c);
#ifdef _WIN32
    return ".";
#else
    return "/tmp";
#endif
}

const char* const kScanHeader = R"(#pragma once
struct ImVec2 { float x, y; };
struct ImVec4 { float x, y, z, w; };
typedef void* ImTextureID;
namespace std {
inline namespace __1 {
template <class T> class vector { T* b; T* e; T* c; public: unsigned long size() const; };
template <class C> class basic_string { C* p; unsigned long n; };
typedef basic_string<char> string;
}
}
namespace game {
enum class Mode : unsigned char { Off, Aim = 5, Silent };
enum Legacy { kA = 1, kB = 2 };
struct Item { std::string name; int count; float price; };
class Secretive { public: int shown; private: int hidden; protected: int guarded; };
struct Player {
    char name[32];
    float health;
    ImVec2 pos;
    float tint[4];
    int keys[8];
    std::vector<Item> items;
    Item slots[4];
    Mode mode;
    Legacy legacy;
    ImTextureID icon;
    ImVec4 color;
    unsigned char flags;
    long long score;
    double precise;
    bool alive;
    Player* next;
    int bits : 3;
    Secretive sec;
    std::vector<std::vector<int>> grid;
    static int counter;
};
}
struct Base { int base_id; };
struct Settings : Base { game::Player player; std::vector<float> fps; game::Mode defaults[2]; };
)";

const FieldDesc* FindField(const StructDesc* s, const char* name) {
    if (!s) return nullptr;
    for (const FieldDesc& f : s->fields)
        if (f.name == name) return &f;
    return nullptr;
}

bool LogHas(const std::vector<std::string>& log, const char* needle) {
    for (const std::string& l : log)
        if (Contains(l, needle)) return true;
    return false;
}

}  // namespace

TEST_CASE("libclang scanner maps fields to the ctype vocabulary") {
    std::string root = TempDir() + "/imforge_binding_scan";
    IMFORGE_MKDIR(root.c_str());
    IMFORGE_MKDIR((root + "/inc").c_str());
    IMFORGE_MKDIR((root + "/inc/game").c_str());
    REQUIRE(WriteTextFile(root + "/inc/game/player.h", kScanHeader));
    REQUIRE(WriteTextFile(root + "/main.cpp", "#include \"game/player.h\"\nint main() { return 0; }\n"));
    Json cc = Json::Array();
    Json entry = Json::Object();
    entry.Set("directory", Json(root));
    entry.Set("file", Json(root + "/main.cpp"));
    Json args = Json::Array();
    for (const char* a : {"c++", "-std=c++17", "-Iinc", "-DIMFORGE_TEST=1", "-c", "main.cpp", "-o", "main.o"})
        args.Push(Json(a));
    entry.Set("arguments", args);
    cc.Push(entry);
    REQUIRE(WriteTextFile(root + "/compile_commands.json", DumpJson(cc)));

    ScanOptions opt;
    opt.compile_commands = root + "/compile_commands.json";
    opt.types = {"game::Player", "Settings"};
    Json out;
    std::vector<std::string> log;
    bool ok = ScanTypes(opt, out, log);
    std::string joined;
    for (const std::string& l : log) joined += l + "\n";
    REQUIRE_MESSAGE(ok, joined);

    TypeDatabase db;
    Error err;
    REQUIRE_MESSAGE(db.Load(out, &err), err.message);
    CHECK(db.StructNames() == std::vector<std::string>{"Settings", "game::Item", "game::Player", "game::Secretive"});
    CHECK(db.EnumNames() == std::vector<std::string>{"game::Legacy", "game::Mode"});
    const StructDesc* player = db.StructSource("game::Player");
    REQUIRE(player);
    CHECK(player->header == "game/player.h");
    CHECK(player->size > 32 + 4 + 8 + 16 + 32);
    CHECK(player->fields.size() == 17);
    CHECK(FindField(player, "name")->ctype == "CharArray");
    CHECK(FindField(player, "name")->count == 32);
    CHECK(FindField(player, "name")->offset == 0);
    CHECK(FindField(player, "health")->ctype == "Float");
    CHECK(FindField(player, "health")->offset == 32);
    CHECK(FindField(player, "pos")->ctype == "Vec2");
    CHECK(FindField(player, "pos")->type == "ImVec2");
    CHECK(FindField(player, "tint")->ctype == "FloatArray");
    CHECK(FindField(player, "tint")->count == 4);
    CHECK(FindField(player, "keys")->ctype == "IntArray");
    CHECK(FindField(player, "items")->ctype == "Vector");
    CHECK(FindField(player, "items")->elem == "game::Item");
    CHECK(FindField(player, "items")->type == "std::vector<game::Item>");
    CHECK(FindField(player, "slots")->ctype == "Array");
    CHECK(FindField(player, "slots")->elem == "game::Item");
    CHECK(FindField(player, "slots")->count == 4);
    CHECK(FindField(player, "mode")->ctype == "Enum");
    CHECK(FindField(player, "mode")->type == "game::Mode");
    CHECK(FindField(player, "legacy")->ctype == "Enum");
    CHECK(FindField(player, "icon")->ctype == "TexID");
    CHECK(FindField(player, "color")->ctype == "Vec4");
    CHECK(FindField(player, "flags")->ctype == "U8");
    CHECK(FindField(player, "score")->ctype == "I64");
    CHECK(FindField(player, "precise")->ctype == "Double");
    CHECK(FindField(player, "alive")->ctype == "Bool");
    CHECK(FindField(player, "sec")->ctype == "Struct");
    CHECK(FindField(player, "grid")->ctype == "Vector");
    CHECK(FindField(player, "grid")->elem == "std::vector<int>");
    CHECK(FindField(player, "next") == nullptr);
    CHECK(FindField(player, "bits") == nullptr);
    CHECK(FindField(player, "counter") == nullptr);
    CHECK(LogHas(log, "game::Player::next: pointer"));
    CHECK(LogHas(log, "game::Player::bits: bit-field"));
    CHECK(LogHas(log, "game::Secretive::hidden: private"));
    CHECK(LogHas(log, "game::Secretive::guarded: protected"));
    const StructDesc* sec = db.StructSource("game::Secretive");
    REQUIRE(sec);
    CHECK(sec->fields.size() == 1);
    const StructDesc* item = db.StructSource("game::Item");
    REQUIRE(item);
    CHECK(FindField(item, "name")->ctype == "StdString");
    CHECK(FindField(item, "name")->type == "std::string");
    const StructDesc* settings = db.StructSource("Settings");
    REQUIRE(settings);
    CHECK(FindField(settings, "fps")->elem == "float");
    CHECK(FindField(settings, "defaults")->ctype == "Array");
    CHECK(FindField(settings, "defaults")->elem == "game::Mode");
    if (const FieldDesc* base_id = FindField(settings, "base_id")) {
        CHECK(base_id->ctype == "I32");
        CHECK(base_id->offset == 0);
        CHECK(FindField(settings, "player")->offset >= 4);
    } else {
        CHECK(LogHas(log, "fields of base 'Base'"));
    }
    const EnumDesc* mode = db.EnumSource("game::Mode");
    REQUIRE(mode);
    CHECK(mode->scoped);
    CHECK(mode->underlying == "unsigned char");
    CHECK(mode->header == "game/player.h");
    REQUIRE(mode->values.size() == 3);
    CHECK(mode->values[1].first == "Aim");
    CHECK(mode->values[1].second == 5);
    CHECK(mode->values[2].second == 6);
    CHECK_FALSE(db.EnumSource("game::Legacy")->scoped);

    std::vector<Param> params = {Param{"player", "game::Player", Json()}};
    CType t;
    std::string perr;
    REQUIRE_MESSAGE(db.TypeOfPath(params, "player.items[0].price", t, &perr), perr);
    CHECK(t.kind == CKind::Float);
    REQUIRE(db.TypeOfPath(params, "player.grid[1][2]", t, &perr));
    CHECK(t.kind == CKind::I32);

    std::string header;
    REQUIRE(GenerateReflectHeader(db, ReflectHeaderOptions(), header, &perr));
    CHECK(Contains(header.c_str(), "#include \"game/player.h\"\n"));
    CHECK(Contains(header.c_str(), "IMFORGE_REFLECT_ENUM(game::Mode, Off, Aim, Silent);\n"));
    CHECK(Contains(header.c_str(), "IMFORGE_REFLECT(game::Item, name, count, price);\n"));
    CHECK(header.find("IMFORGE_REFLECT(game::Item") < header.find("IMFORGE_REFLECT(game::Player"));
    CHECK(header.find("IMFORGE_REFLECT(game::Player") < header.find("IMFORGE_REFLECT(Settings"));

    ScanOptions direct;
    direct.headers = {root + "/inc/game/player.h"};
    direct.include_dirs = {root + "/inc"};
    direct.types = {"Item", "Ghost"};
    log.clear();
    CHECK_FALSE(ScanTypes(direct, out, log));
    CHECK(LogHas(log, "'Ghost' was not found"));
    CHECK(out["structs"]["game::Item"].IsObject());
    CHECK(out["structs"]["game::Item"]["header"].AsString() == "game/player.h");
    CHECK_FALSE(out["structs"].Has("game::Player"));
}

#endif
