#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "imgui.h"

#include "imforge/imforge_value.h"
#include "imgui_headless.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

using namespace imforge;

enum class Mode { Idle, Aim = 5, Fire };
enum Legacy { kOne = 1, kTwo = 2, kTen = 10 };
enum class Unregistered : unsigned char { A, B };

struct Item {
    std::string name;
    int qty = 0;
    float weight = 0;
};

struct Player {
    float health = 0;
    int ammo = 0;
    ImVec2 pos;
    char name[32] = {};
    std::string title;
    std::vector<Item> items;
    Mode mode = Mode::Idle;
};

namespace game {
struct Stats {
    int kills = 0;
    double ratio = 0;
};
}  // namespace game

struct Everything {
    bool b = false;
    int8_t i8 = 0;
    uint8_t u8 = 0;
    int16_t i16 = 0;
    uint16_t u16 = 0;
    int32_t i32 = 0;
    uint32_t u32 = 0;
    int64_t i64 = 0;
    uint64_t u64 = 0;
    long l = 0;
    unsigned long ul = 0;
    char c = 0;
    float f = 0;
    double d = 0;
    Vec2 v2;
    Vec4 v4;
    ImVec4 col;
    float f2[2] = {};
    float f4[4] = {};
    int iarr[3] = {};
    char text[8] = {};
    Legacy legacy = kOne;
    Unregistered unreg = Unregistered::A;
    void* ptr = nullptr;
    Item inner;
    Item pair[2];
    std::vector<float> floats;
    std::vector<std::vector<int>> nested;
    game::Stats stats;
};

struct Wide {
    int f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18, f19, f20,
        f21, f22, f23, f24, f25, f26, f27, f28, f29, f30, f31, f32;
};

IMFORGE_REFLECT_ENUM(Mode, Idle, Aim, Fire);
IMFORGE_REFLECT_ENUM(Legacy, kOne, kTwo, kTen);
IMFORGE_REFLECT(Item, name, qty, weight);
IMFORGE_REFLECT(Player, health, ammo, pos, name, title, items, mode);
IMFORGE_REFLECT(game::Stats, kills, ratio);
IMFORGE_REFLECT(Everything, b, i8, u8, i16, u16, i32, u32, i64, u64, l, ul, c, f, d, v2, v4, col,
                f2, f4, iarr, text, legacy, unreg, ptr, inner, pair, floats, nested, stats);
IMFORGE_REFLECT(Wide, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17,
                f18, f19, f20, f21, f22, f23, f24, f25, f26, f27, f28, f29, f30, f31, f32);

static Json J(const char* text) {
    Json j;
    JsonError e;
    REQUIRE_MESSAGE(ParseJson(std::string(text), j, &e), e.message);
    return j;
}

TEST_CASE("registry is populated at static init") {
    TypeRegistry& reg = TypeRegistry::Global();
    const StructInfo* player = reg.FindStruct("Player");
    REQUIRE(player);
    CHECK(player->size == sizeof(Player));
    REQUIRE(player->fields.size() == 7);
    CHECK(player->fields[0].name == "health");
    CHECK(player->fields[0].type.kind == CKind::Float);
    CHECK(player->fields[0].offset == offsetof(Player, health));
    CHECK(player->fields[1].name == "ammo");
    CHECK(player->fields[1].type.kind == CKind::I32);
    CHECK(player->fields[2].type.kind == CKind::Vec2);
    CHECK(player->fields[2].type.name == "ImVec2");
    CHECK(player->fields[3].type.kind == CKind::CharArray);
    CHECK(player->fields[3].type.count == 32);
    CHECK(player->fields[4].type.kind == CKind::StdString);
    CHECK(player->fields[5].type.kind == CKind::Vector);
    REQUIRE(player->fields[5].type.elem);
    CHECK(player->fields[5].type.elem->kind == CKind::Struct);
    CHECK(player->fields[5].type.elem->strct == reg.FindStruct("Item"));
    CHECK(player->fields[5].type.elem->size == sizeof(Item));
    CHECK(player->fields[5].type.name == "std::vector<Item>");
    CHECK(player->fields[6].type.kind == CKind::Enum);
    CHECK(player->fields[6].type.enm == reg.FindEnum("Mode"));
    CHECK(player->fields[6].type.name == "Mode");
    CHECK(player->Find("title") == &player->fields[4]);
    CHECK(player->Find("nope") == nullptr);

    const StructInfo* item = reg.FindStruct("Item");
    REQUIRE(item);
    CHECK(item->fields.size() == 3);
    CHECK(item->fields[1].offset == offsetof(Item, qty));

    const StructInfo* stats = reg.FindStruct("game::Stats");
    REQUIRE(stats);
    CHECK(stats->fields[1].type.kind == CKind::Double);
    CHECK(StructInfoOf<game::Stats>() == stats);
    CHECK(StructInfoOf<Player>() == player);
    CHECK(StructInfoOf<int>() == nullptr);
    CHECK(Reflector<Player>::registered == player);

    const StructInfo* wide = reg.FindStruct("Wide");
    REQUIRE(wide);
    REQUIRE(wide->fields.size() == 32);
    CHECK(wide->fields[31].name == "f32");
    CHECK(wide->fields[31].offset == offsetof(Wide, f32));

    const EnumInfo* mode = reg.FindEnum("Mode");
    REQUIRE(mode);
    CHECK(std::string(mode->name) == "Mode");
    REQUIRE(mode->values.size() == 3);
    CHECK(mode->values[0].first == "Idle");
    CHECK(mode->values[0].second == 0);
    CHECK(mode->values[1].first == "Aim");
    CHECK(mode->values[1].second == 5);
    CHECK(mode->values[2].second == 6);
    CHECK(std::string(mode->NameOf(5)) == "Aim");
    CHECK(mode->NameOf(99) == nullptr);
    long long v = 0;
    CHECK(mode->ValueOf("Fire", v));
    CHECK(v == 6);
    CHECK_FALSE(mode->ValueOf("Nope", v));
    CHECK(EnumInfoOf<Mode>() == mode);
    CHECK(EnumInfoOf<Unregistered>() == nullptr);

    const EnumInfo* legacy = reg.FindEnum("Legacy");
    REQUIRE(legacy);
    CHECK(legacy->values[2].first == "kTen");
    CHECK(legacy->values[2].second == 10);
    CHECK(reg.FindEnum("Nope") == nullptr);
    CHECK(reg.FindStruct(nullptr) == nullptr);
}

TEST_CASE("registry re-registration keeps pointers stable") {
    TypeRegistry reg;
    StructInfo a;
    a.name = "X";
    a.size = 4;
    const StructInfo* p = reg.RegisterStruct(a);
    a.size = 8;
    const StructInfo* q = reg.RegisterStruct(a);
    CHECK(p == q);
    CHECK(p->size == 8);
    CHECK(reg.structs.size() == 1);

    EnumInfo e;
    e.name_str = "RuntimeEnum";
    e.values = {{"A", 1}};
    const EnumInfo* ep = reg.RegisterEnum(e);
    REQUIRE(ep->name);
    CHECK(std::string(ep->name) == "RuntimeEnum");
    CHECK(reg.FindEnum("RuntimeEnum") == ep);
    e.values.push_back({"B", 2});
    CHECK(reg.RegisterEnum(e) == ep);
    CHECK(ep->values.size() == 2);
    CHECK(std::string(ep->name) == "RuntimeEnum");
}

TEST_CASE("CTypeOf fundamentals") {
    CHECK(CTypeOf<bool>().kind == CKind::Bool);
    CHECK(CTypeOf<bool>().name == "bool");
    CHECK(CTypeOf<bool>().size == 1);
    CHECK(CTypeOf<int8_t>().kind == CKind::I8);
    CHECK(CTypeOf<uint8_t>().kind == CKind::U8);
    CHECK(CTypeOf<int16_t>().kind == CKind::I16);
    CHECK(CTypeOf<uint16_t>().kind == CKind::U16);
    CHECK(CTypeOf<int32_t>().kind == CKind::I32);
    CHECK(CTypeOf<uint32_t>().kind == CKind::U32);
    CHECK(CTypeOf<int64_t>().kind == CKind::I64);
    CHECK(CTypeOf<uint64_t>().kind == CKind::U64);
    CHECK(CTypeOf<int>().name == "int");
    CHECK(CTypeOf<unsigned>().name == "unsigned");
    CHECK(CTypeOf<long long>().name == "long long");
    CHECK(CTypeOf<long>().kind == (sizeof(long) == 8 ? CKind::I64 : CKind::I32));
    CHECK(CTypeOf<unsigned long>().kind == (sizeof(long) == 8 ? CKind::U64 : CKind::U32));
    CHECK(CTypeOf<char>().name == "char");
    CHECK((CTypeOf<char>().kind == CKind::I8 || CTypeOf<char>().kind == CKind::U8));
    CHECK(CTypeOf<const int>().kind == CKind::I32);
    CHECK(CTypeOf<float>().kind == CKind::Float);
    CHECK(CTypeOf<float>().size == 4);
    CHECK(CTypeOf<double>().kind == CKind::Double);
    CHECK(CTypeOf<double>().name == "double");
    CHECK(CTypeOf<std::string>().kind == CKind::StdString);
    CHECK(CTypeOf<std::string>().size == sizeof(std::string));
    CHECK(CTypeOf<Vec2>().kind == CKind::Vec2);
    CHECK(CTypeOf<Vec4>().kind == CKind::Vec4);
    CHECK(CTypeOf<ImVec2>().kind == CKind::Vec2);
    CHECK(CTypeOf<ImVec2>().size == 8);
    CHECK(CTypeOf<ImVec4>().kind == CKind::Vec4);
    CHECK(CTypeOf<ImVec4>().name == "ImVec4");
    CHECK(CTypeOf<void*>().kind == CKind::TexID);
    CHECK(CTypeOf<void*>().size == sizeof(void*));
    CHECK(CTypeOf<Item*>().kind == CKind::TexID);
    CHECK(CTypeOf<long double>().kind == CKind::Unknown);
    CHECK(CTypeOf<long double>().size == sizeof(long double));
    struct Local { int x; };
    CHECK(CTypeOf<Local>().kind == CKind::Unknown);
}

TEST_CASE("CTypeOf arrays vectors enums structs") {
    CType ca = CTypeOf<char[16]>();
    CHECK(ca.kind == CKind::CharArray);
    CHECK(ca.count == 16);
    CHECK(ca.size == 16);
    CType fa = CTypeOf<float[4]>();
    CHECK(fa.kind == CKind::FloatArray);
    CHECK(fa.count == 4);
    CHECK(fa.size == 16);
    CHECK(CTypeOf<float[2]>().count == 2);
    CHECK(CTypeOf<float[7]>().kind == CKind::FloatArray);
    CType ia = CTypeOf<int[3]>();
    CHECK(ia.kind == CKind::IntArray);
    CHECK(ia.count == 3);
    CType da = CTypeOf<double[5]>();
    CHECK(da.kind == CKind::Array);
    CHECK(da.count == 5);
    REQUIRE(da.elem);
    CHECK(da.elem->kind == CKind::Double);
    CHECK(da.size == 40);
    CType sa = CTypeOf<Item[2]>();
    CHECK(sa.kind == CKind::Array);
    CHECK(sa.elem->kind == CKind::Struct);
    CHECK(sa.elem->strct == StructInfoOf<Item>());
    CHECK(sa.name == "Item");

    CType vf = CTypeOf<std::vector<float>>();
    CHECK(vf.kind == CKind::Vector);
    REQUIRE(vf.elem);
    CHECK(vf.elem->kind == CKind::Float);
    CHECK(vf.elem == CTypeOf<std::vector<float>>().elem);
    CHECK(vf.size == sizeof(std::vector<float>));
    CHECK(vf.name == "std::vector<float>");
    REQUIRE(vf.vec_data);
    REQUIRE(vf.vec_size);
    std::vector<float> fv = {1, 2, 3};
    CHECK(vf.vec_size(&fv) == 3);
    CHECK(vf.vec_data(&fv) == fv.data());
    CType vv = CTypeOf<std::vector<std::vector<int>>>();
    CHECK(vv.elem->kind == CKind::Vector);
    CHECK(vv.elem->elem->kind == CKind::I32);
    CHECK(vv.name == "std::vector<std::vector<int>>");

    CType em = CTypeOf<Mode>();
    CHECK(em.kind == CKind::Enum);
    CHECK(em.enm == EnumInfoOf<Mode>());
    CHECK(em.name == "Mode");
    CHECK(em.size == sizeof(Mode));
    REQUIRE(em.elem);
    CHECK(em.elem->kind == CKind::I32);
    CType el = CTypeOf<Legacy>();
    CHECK(el.kind == CKind::Enum);
    CHECK(el.enm == EnumInfoOf<Legacy>());
    CType eu = CTypeOf<Unregistered>();
    CHECK(eu.kind == CKind::Enum);
    CHECK(eu.enm == nullptr);
    CHECK(eu.name == "enum");
    CHECK(eu.size == 1);
    CHECK(eu.elem->kind == CKind::U8);

    CType st = CTypeOf<Player>();
    CHECK(st.kind == CKind::Struct);
    CHECK(st.strct == StructInfoOf<Player>());
    CHECK(st.name == "Player");
    CHECK(st.size == sizeof(Player));
    CHECK(CTypeOf<game::Stats>().name == "game::Stats");
    CHECK(std::string(CKindName(CKind::Vector)) == "Vector");
    CHECK(std::string(CKindName(CKind::Unknown)) == "Unknown");
}

TEST_CASE("Value constructors and conversions") {
    CHECK(Value().IsNull());
    CHECK(Value(true).IsBool());
    CHECK(Value(true).b == true);
    CHECK(Value(3).IsInt());
    CHECK(Value(3).i == 3);
    CHECK(Value(3u).i == 3);
    CHECK(Value(3LL).i == 3);
    CHECK(Value(3ULL).i == 3);
    CHECK(Value(static_cast<size_t>(4)).i == 4);
    CHECK(Value(2.5).IsFloat());
    CHECK(Value(2.5f).f == 2.5);
    CHECK(Value("s").IsString());
    CHECK(Value(std::string("t")).s == "t");
    CHECK(Value(static_cast<const char*>(nullptr)).s.empty());
    CHECK(Value(Vec2{1, 2}).IsVec2());
    CHECK(Value(Vec4{1, 2, 3, 4}).IsVec4());
    CHECK(Value(ImVec2(1, 2)).v2 == Vec2{1, 2});
    CHECK(Value(ImVec4(1, 2, 3, 4)).v4 == Vec4{1, 2, 3, 4});
    CHECK(Value::Color(Vec4{1, 0, 0, 1}).IsColor());
    CHECK(Value::Color(0.5f, 0.25f, 0, 1).v4.y == 0.25f);
    CHECK(Value::List().IsList());
    Value lst(std::vector<Value>{Value(1), Value("a")});
    CHECK(lst.IsList());
    CHECK(lst.list.size() == 2);
    CHECK(Value(1).IsNumber());
    CHECK(Value(1.0).IsNumber());
    CHECK_FALSE(Value("1").IsNumber());

    CHECK(Value(true).ToDouble() == 1.0);
    CHECK(Value(7).ToDouble() == 7.0);
    CHECK(Value(2.5).ToDouble() == 2.5);
    CHECK(Value("3.25").ToDouble() == 3.25);
    CHECK(Value("abc").ToDouble() == 0.0);
    CHECK(Value().ToDouble() == 0.0);
    CHECK(Value(Vec2{4, 5}).ToDouble() == 4.0);

    CHECK(Value(true).ToInt() == 1);
    CHECK(Value(9).ToInt() == 9);
    CHECK(Value(2.9).ToInt() == 2);
    CHECK(Value(-2.9).ToInt() == -2);
    CHECK(Value("42").ToInt() == 42);
    CHECK(Value("0x10").ToInt() == 16);
    CHECK(Value(1e30).ToInt() == 9223372036854775807LL);
    CHECK(Value(-1e30).ToInt() == (-9223372036854775807LL - 1));

    CHECK(Value().ToBool() == false);
    CHECK(Value(false).ToBool() == false);
    CHECK(Value(0).ToBool() == false);
    CHECK(Value(2).ToBool() == true);
    CHECK(Value(0.0).ToBool() == false);
    CHECK(Value(0.5).ToBool() == true);
    CHECK(Value("").ToBool() == false);
    CHECK(Value("false").ToBool() == false);
    CHECK(Value("0").ToBool() == false);
    CHECK(Value("yes").ToBool() == true);
    CHECK(Value::List().ToBool() == false);
    CHECK(lst.ToBool() == true);
    CHECK(Value(Vec2{}).ToBool() == true);

    CHECK(Value().ToString().empty());
    CHECK(Value(true).ToString() == "true");
    CHECK(Value(false).ToString() == "false");
    CHECK(Value(-12).ToString() == "-12");
    CHECK(Value(1.0).ToString() == "1");
    CHECK(Value(2.5).ToString() == "2.5");
    CHECK(Value(0.1).ToString() == "0.1");
    CHECK(Value(1e20).ToString() == "1e+20");
    CHECK(Value("x").ToString() == "x");
    CHECK(Value(Vec2{1, 2.5f}).ToString() == "(1, 2.5)");
    CHECK(Value(Vec4{1, 2, 3, 4}).ToString() == "(1, 2, 3, 4)");
    CHECK(Value::Color(1, 0, 0.5f, 1).ToString() == "#ff0080ff");
    CHECK(Value::Color(1, 0, 0.5f, 0.5f).ToString() == "#ff008080");
    CHECK(lst.ToString() == "[1, a]");

    CHECK(Value(Vec2{1, 2}).ToVec2() == Vec2{1, 2});
    CHECK(Value(Vec4{1, 2, 3, 4}).ToVec2() == Vec2{1, 2});
    CHECK(Value(3).ToVec2() == Vec2{3, 3});
    CHECK(Value(std::vector<Value>{Value(1), Value(2)}).ToVec2() == Vec2{1, 2});
    CHECK(Value(std::vector<Value>{Value(1)}).ToVec2() == Vec2{1, 0});
    CHECK(Value(Vec2{1, 2}).ToVec4() == Vec4{1, 2, 0, 0});
    CHECK(Value(Vec4{1, 2, 3, 4}).ToVec4() == Vec4{1, 2, 3, 4});
    CHECK(Value::Color(1, 2, 3, 4).ToVec4() == Vec4{1, 2, 3, 4});
    CHECK(Value("#ff0000ff").ToVec4() == Vec4{1, 0, 0, 1});
    CHECK(Value("#00ff00").ToVec4() == Vec4{0, 1, 0, 1});
    CHECK(Value("#zz").ToVec4() == Vec4{});
    CHECK(Value(2).ToVec4() == Vec4{2, 2, 2, 2});
    CHECK(Value(std::vector<Value>{Value(1), Value(2), Value(3), Value(4)}).ToVec4() == Vec4{1, 2, 3, 4});
}

TEST_CASE("Value equality") {
    CHECK(Value() == Value());
    CHECK(Value(1) == Value(1));
    CHECK(Value(1) != Value(1.0));
    CHECK(Value(1.0) == Value(1.0f));
    CHECK(Value("a") == Value("a"));
    CHECK(Value("a") != Value("b"));
    CHECK(Value(Vec2{1, 2}) == Value(Vec2{1, 2}));
    CHECK(Value(Vec2{1, 2}) != Value(Vec2{2, 1}));
    CHECK(Value(Vec4{1, 2, 3, 4}) != Value::Color(Vec4{1, 2, 3, 4}));
    CHECK(Value::Color(Vec4{1, 2, 3, 4}) == Value::Color(Vec4{1, 2, 3, 4}));
    CHECK(Value(std::vector<Value>{Value(1)}) == Value(std::vector<Value>{Value(1)}));
    CHECK(Value(std::vector<Value>{Value(1)}) != Value(std::vector<Value>{Value(2)}));
    CHECK(Value(true) != Value(1));
}

TEST_CASE("Value FromJson inference") {
    CHECK(Value::FromJson(J("null")).IsNull());
    CHECK(Value::FromJson(J("true")) == Value(true));
    CHECK(Value::FromJson(J("12")) == Value(12));
    CHECK(Value::FromJson(J("-3")) == Value(-3));
    CHECK(Value::FromJson(J("12.5")) == Value(12.5));
    CHECK(Value::FromJson(J("1e3")) == Value(1000));
    CHECK(Value::FromJson(J("1e30")) == Value(1e30));
    CHECK(Value::FromJson(J("\"str\"")) == Value("str"));
    CHECK(Value::FromJson(J("\"#ff0000ff\"")) == Value("#ff0000ff"));
    CHECK(Value::FromJson(J("{\"x\":1,\"y\":2}")) == Value(Vec2{1, 2}));
    CHECK(Value::FromJson(J("{\"y\":2,\"x\":1}")) == Value(Vec2{1, 2}));
    CHECK(Value::FromJson(J("{\"x\":1,\"y\":2,\"z\":3,\"w\":4}")) == Value(Vec4{1, 2, 3, 4}));
    CHECK(Value::FromJson(J("{\"x\":1,\"y\":2,\"z\":3}")) == Value(Vec2{1, 2}));
    CHECK(Value::FromJson(J("{\"a\":1}")).IsNull());
    Value l = Value::FromJson(J("[1, 2.5, \"s\", [true], {\"x\":0,\"y\":0}]"));
    REQUIRE(l.IsList());
    REQUIRE(l.list.size() == 5);
    CHECK(l.list[0] == Value(1));
    CHECK(l.list[1] == Value(2.5));
    CHECK(l.list[2] == Value("s"));
    CHECK(l.list[3].IsList());
    CHECK(l.list[3].list[0] == Value(true));
    CHECK(l.list[4] == Value(Vec2{}));
}

TEST_CASE("Value FromJson with hints") {
    using K = Value::Kind;
    CHECK(Value::FromJson(J("true"), K::Bool) == Value(true));
    CHECK(Value::FromJson(J("1"), K::Bool) == Value(true));
    CHECK(Value::FromJson(J("0"), K::Bool) == Value(false));
    CHECK(Value::FromJson(J("\"true\""), K::Bool) == Value(true));
    CHECK(Value::FromJson(J("\"no\""), K::Bool) == Value(false));
    CHECK(Value::FromJson(J("null"), K::Bool) == Value(false));

    CHECK(Value::FromJson(J("7"), K::Int) == Value(7));
    CHECK(Value::FromJson(J("7.9"), K::Int) == Value(7));
    CHECK(Value::FromJson(J("true"), K::Int) == Value(1));
    CHECK(Value::FromJson(J("\"42\""), K::Int) == Value(42));
    CHECK(Value::FromJson(J("[1]"), K::Int) == Value(0));

    CHECK(Value::FromJson(J("7"), K::Float) == Value(7.0));
    CHECK(Value::FromJson(J("7.5"), K::Float) == Value(7.5));
    CHECK(Value::FromJson(J("false"), K::Float) == Value(0.0));
    CHECK(Value::FromJson(J("\"1.25\""), K::Float) == Value(1.25));
    CHECK(Value::FromJson(J("{}"), K::Float) == Value(0.0));

    CHECK(Value::FromJson(J("\"s\""), K::String) == Value("s"));
    CHECK(Value::FromJson(J("12"), K::String) == Value("12"));
    CHECK(Value::FromJson(J("1.5"), K::String) == Value("1.5"));
    CHECK(Value::FromJson(J("true"), K::String) == Value("true"));
    CHECK(Value::FromJson(J("null"), K::String) == Value(""));

    CHECK(Value::FromJson(J("{\"x\":3,\"y\":4}"), K::Vec2) == Value(Vec2{3, 4}));
    CHECK(Value::FromJson(J("[3,4]"), K::Vec2) == Value(Vec2{3, 4}));
    CHECK(Value::FromJson(J("5"), K::Vec2) == Value(Vec2{5, 5}));
    CHECK(Value::FromJson(J("{\"x\":1,\"y\":2,\"z\":3,\"w\":4}"), K::Vec4) == Value(Vec4{1, 2, 3, 4}));
    CHECK(Value::FromJson(J("[1,2,3,4]"), K::Vec4) == Value(Vec4{1, 2, 3, 4}));
    CHECK(Value::FromJson(J("{\"x\":1,\"y\":2}"), K::Vec4) == Value(Vec4{1, 2, 0, 0}));

    Value c = Value::FromJson(J("\"#ff8000ff\""), K::Color);
    REQUIRE(c.IsColor());
    CHECK(c.v4.x == 1.0f);
    CHECK(c.v4.y == doctest::Approx(128.0 / 255.0));
    CHECK(c.v4.z == 0.0f);
    CHECK(c.v4.w == 1.0f);
    Value c2 = Value::FromJson(J("\"#102030\""), K::Color);
    CHECK(c2.IsColor());
    CHECK(c2.v4.w == 1.0f);
    CHECK(c2.v4.x == doctest::Approx(16.0 / 255.0));
    Value c3 = Value::FromJson(J("[1,0,0,0.5]"), K::Color);
    CHECK(c3.IsColor());
    CHECK(c3.v4 == Vec4{1, 0, 0, 0.5f});
    Value c4 = Value::FromJson(J("{\"x\":0,\"y\":1,\"z\":0,\"w\":1}"), K::Color);
    CHECK(c4 == Value::Color(0, 1, 0, 1));

    Value l = Value::FromJson(J("[1,\"a\"]"), K::List);
    REQUIRE(l.IsList());
    CHECK(l.list.size() == 2);
    Value single = Value::FromJson(J("3"), K::List);
    REQUIRE(single.IsList());
    CHECK(single.list.size() == 1);
    CHECK(single.list[0] == Value(3));
    CHECK(Value::FromJson(J("null"), K::List).list.empty());
}

TEST_CASE("Value ToJson and round trip") {
    CHECK(DumpJson(Value().ToJson(), 0) == "null");
    CHECK(DumpJson(Value(true).ToJson(), 0) == "true");
    CHECK(DumpJson(Value(42).ToJson(), 0) == "42");
    CHECK(DumpJson(Value(-1.5).ToJson(), 0) == "-1.5");
    CHECK(DumpJson(Value("q\"").ToJson(), 0) == "\"q\\\"\"");
    CHECK(DumpJson(Value(Vec2{1, 2.5f}).ToJson(), 0) == "{\"x\":1,\"y\":2.5}");
    CHECK(DumpJson(Value(Vec4{1, 2, 3, 4}).ToJson(), 0) == "{\"w\":4,\"x\":1,\"y\":2,\"z\":3}");
    CHECK(DumpJson(Value(Vec4{1, 2, 3, 4}).ToJson(), 0, false) == "{\"x\":1,\"y\":2,\"z\":3,\"w\":4}");
    CHECK(DumpJson(Value::Color(1, 0, 0, 1).ToJson(), 0) == "\"#ff0000ff\"");
    CHECK(DumpJson(Value::Color(0, 0.5f, 1, 0).ToJson(), 0) == "\"#0080ff00\"");
    CHECK(DumpJson(Value::Color(2, -1, 0.5f, 1).ToJson(), 0) == "\"#ff0080ff\"");
    Value l(std::vector<Value>{Value(1), Value("a"), Value(Vec2{})});
    CHECK(DumpJson(l.ToJson(), 0) == "[1,\"a\",{\"x\":0,\"y\":0}]");

    Value values[] = {Value(), Value(true), Value(7), Value(-7.25), Value("txt"), Value(Vec2{1, 2}),
                      Value(Vec4{1, 2, 3, 4}), Value::Color(1, 0, 0, 1), Value::Color(0, 1, 0, 0),
                      Value(std::vector<Value>{Value(1), Value(std::vector<Value>{Value("x")})})};
    for (const Value& v : values) {
        Json j = v.ToJson();
        Value back = Value::FromJson(j, v.kind);
        CHECK(back == v);
        Json j2 = back.ToJson();
        CHECK(j2 == j);
    }
    Value c = Value::Color(0.123f, 0.456f, 0.789f, 0.5f);
    Value cb = Value::FromJson(c.ToJson(), Value::Kind::Color);
    CHECK(cb.v4.x == doctest::Approx(c.v4.x).epsilon(0.01));
    CHECK(cb.v4.w == doctest::Approx(c.v4.w).epsilon(0.01));
}

static Player MakePlayer() {
    Player p;
    p.health = 75.5f;
    p.ammo = 30;
    p.pos = ImVec2(10, 20);
    std::memcpy(p.name, "Hero", 5);
    p.title = "Captain";
    p.items.push_back(Item{"sword", 1, 3.5f});
    p.items.push_back(Item{"potion", 5, 0.25f});
    p.mode = Mode::Aim;
    return p;
}

TEST_CASE("ResolvePath nested fields") {
    Player p = MakePlayer();
    const StructInfo* pt = StructInfoOf<Player>();
    void* ptr = nullptr;
    CType t;
    std::string err;

    REQUIRE(ResolvePath(&p, pt, "health", &ptr, &t, &err));
    CHECK(ptr == &p.health);
    CHECK(t.kind == CKind::Float);

    REQUIRE(ResolvePath(&p, pt, "ammo", &ptr, &t, &err));
    CHECK(ptr == &p.ammo);
    CHECK(t.kind == CKind::I32);

    REQUIRE(ResolvePath(&p, pt, "pos", &ptr, &t, &err));
    CHECK(ptr == &p.pos);
    CHECK(t.kind == CKind::Vec2);
    REQUIRE(ResolvePath(&p, pt, "pos.x", &ptr, &t, &err));
    CHECK(ptr == &p.pos.x);
    CHECK(t.kind == CKind::Float);
    REQUIRE(ResolvePath(&p, pt, "pos.y", &ptr, &t, &err));
    CHECK(ptr == &p.pos.y);
    REQUIRE(ResolvePath(&p, pt, "pos[1]", &ptr, &t, &err));
    CHECK(ptr == &p.pos.y);
    CHECK_FALSE(ResolvePath(&p, pt, "pos.z", &ptr, &t, &err));
    CHECK_FALSE(ResolvePath(&p, pt, "pos[2]", &ptr, &t, &err));

    REQUIRE(ResolvePath(&p, pt, "name", &ptr, &t, &err));
    CHECK(ptr == &p.name);
    CHECK(t.kind == CKind::CharArray);
    CHECK(t.count == 32);
    REQUIRE(ResolvePath(&p, pt, "name[1]", &ptr, &t, &err));
    CHECK(ptr == &p.name[1]);
    CHECK(t.kind == CKind::I8);

    REQUIRE(ResolvePath(&p, pt, "title", &ptr, &t, &err));
    CHECK(ptr == &p.title);
    CHECK(t.kind == CKind::StdString);

    REQUIRE(ResolvePath(&p, pt, "mode", &ptr, &t, &err));
    CHECK(ptr == &p.mode);
    CHECK(t.kind == CKind::Enum);
    CHECK(t.enm == EnumInfoOf<Mode>());

    REQUIRE(ResolvePath(&p, pt, "", &ptr, &t, &err));
    CHECK(ptr == &p);
    CHECK(t.kind == CKind::Struct);
    CHECK(t.strct == pt);
    REQUIRE(ResolvePath(&p, pt, nullptr, &ptr, &t, &err));
    CHECK(ptr == &p);
    REQUIRE(ResolvePath(&p, pt, "health", nullptr, nullptr, nullptr));
}

TEST_CASE("ResolvePath vectors arrays and size") {
    Player p = MakePlayer();
    const StructInfo* pt = StructInfoOf<Player>();
    void* ptr = nullptr;
    CType t;
    std::string err;

    REQUIRE(ResolvePath(&p, pt, "items", &ptr, &t, &err));
    CHECK(ptr == &p.items);
    CHECK(t.kind == CKind::Vector);
    REQUIRE(ResolvePath(&p, pt, "items[0]", &ptr, &t, &err));
    CHECK(ptr == &p.items[0]);
    CHECK(t.kind == CKind::Struct);
    CHECK(t.strct == StructInfoOf<Item>());
    REQUIRE(ResolvePath(&p, pt, "items[1].name", &ptr, &t, &err));
    CHECK(ptr == &p.items[1].name);
    CHECK(t.kind == CKind::StdString);
    CHECK(*static_cast<std::string*>(ptr) == "potion");
    REQUIRE(ResolvePath(&p, pt, "items[1].qty", &ptr, &t, &err));
    CHECK(ptr == &p.items[1].qty);
    REQUIRE(ResolvePath(&p, pt, "items[1].weight", &ptr, &t, &err));
    CHECK(ptr == &p.items[1].weight);

    REQUIRE(ResolvePath(&p, pt, "items.size", &ptr, &t, &err));
    CHECK(t.kind == CKind::I64);
    CHECK(*static_cast<long long*>(ptr) == 2);
    p.items.push_back(Item{"bow", 1, 1.0f});
    REQUIRE(ResolvePath(&p, pt, "items.size", &ptr, &t, &err));
    CHECK(*static_cast<long long*>(ptr) == 3);
    REQUIRE(ResolvePath(&p, pt, "items[2].name", &ptr, &t, &err));
    CHECK(*static_cast<std::string*>(ptr) == "bow");
    REQUIRE(ResolvePath(&p, pt, "name.size", &ptr, &t, &err));
    CHECK(*static_cast<long long*>(ptr) == 32);
    Value sz;
    REQUIRE(ReadValue(ptr, t, sz));
    CHECK(sz == Value(32LL));

    CHECK_FALSE(ResolvePath(&p, pt, "items[3]", &ptr, &t, &err));
    CHECK(err == "index 3 out of range (size 3)");
    CHECK_FALSE(ResolvePath(&p, pt, "items[99].name", &ptr, &t, &err));
    CHECK_FALSE(ResolvePath(&p, pt, "name[32]", &ptr, &t, &err));
    CHECK(err == "index 32 out of range (size 32)");
    REQUIRE(ResolvePath(&p, pt, "name[31]", &ptr, &t, &err));
    CHECK_FALSE(ResolvePath(&p, pt, "items.size.x", &ptr, &t, &err));
    CHECK_FALSE(ResolvePath(&p, pt, "health.size", &ptr, &t, &err));
    CHECK_FALSE(ResolvePath(&p, pt, "pos.size", &ptr, &t, &err));
    CHECK_FALSE(ResolvePath(&p, pt, "items.length", &ptr, &t, &err));
    p.items.clear();
    CHECK_FALSE(ResolvePath(&p, pt, "items[0]", &ptr, &t, &err));
    CHECK(err == "index 0 out of range (size 0)");
    REQUIRE(ResolvePath(&p, pt, "items.size", &ptr, &t, &err));
    CHECK(*static_cast<long long*>(ptr) == 0);

    Everything e;
    e.floats = {1.5f, 2.5f};
    e.nested = {{1, 2}, {3, 4, 5}};
    const StructInfo* et = StructInfoOf<Everything>();
    REQUIRE(ResolvePath(&e, et, "floats[1]", &ptr, &t, &err));
    CHECK(ptr == &e.floats[1]);
    CHECK(t.kind == CKind::Float);
    REQUIRE(ResolvePath(&e, et, "nested[1][2]", &ptr, &t, &err));
    CHECK(ptr == &e.nested[1][2]);
    CHECK(t.kind == CKind::I32);
    REQUIRE(ResolvePath(&e, et, "nested[1].size", &ptr, &t, &err));
    CHECK(*static_cast<long long*>(ptr) == 3);
    CHECK_FALSE(ResolvePath(&e, et, "nested[2]", &ptr, &t, &err));
    REQUIRE(ResolvePath(&e, et, "f4[3]", &ptr, &t, &err));
    CHECK(ptr == &e.f4[3]);
    CHECK(t.kind == CKind::Float);
    REQUIRE(ResolvePath(&e, et, "f4.size", &ptr, &t, &err));
    CHECK(*static_cast<long long*>(ptr) == 4);
    REQUIRE(ResolvePath(&e, et, "iarr[2]", &ptr, &t, &err));
    CHECK(ptr == &e.iarr[2]);
    CHECK(t.kind == CKind::I32);
    CHECK_FALSE(ResolvePath(&e, et, "iarr[3]", &ptr, &t, &err));
    REQUIRE(ResolvePath(&e, et, "pair[1].qty", &ptr, &t, &err));
    CHECK(ptr == &e.pair[1].qty);
    REQUIRE(ResolvePath(&e, et, "pair.size", &ptr, &t, &err));
    CHECK(*static_cast<long long*>(ptr) == 2);
    REQUIRE(ResolvePath(&e, et, "inner.name", &ptr, &t, &err));
    CHECK(ptr == &e.inner.name);
    REQUIRE(ResolvePath(&e, et, "stats.ratio", &ptr, &t, &err));
    CHECK(ptr == &e.stats.ratio);
    CHECK(t.kind == CKind::Double);
    REQUIRE(ResolvePath(&e, et, "col.w", &ptr, &t, &err));
    CHECK(ptr == &e.col.w);
    REQUIRE(ResolvePath(&e, et, "v4[2]", &ptr, &t, &err));
    CHECK(ptr == &e.v4.z);
}

TEST_CASE("ResolvePath syntax errors") {
    Player p = MakePlayer();
    const StructInfo* pt = StructInfoOf<Player>();
    void* ptr = nullptr;
    CType t;
    std::string err;
    const char* bad[] = {".health", "health.", "nope", "items[", "items[a]", "items[0", "items[-1]",
                         "items[0]name", "health.x", "title.size", "items..size", "1abc", "items[]",
                         "health ", "items[0]..name", "mode.x"};
    for (const char* b : bad) {
        err.clear();
        CAPTURE(b);
        CHECK_FALSE(ResolvePath(&p, pt, b, &ptr, &t, &err));
        CHECK_FALSE(err.empty());
    }
    CHECK_FALSE(ResolvePath(nullptr, pt, "health", &ptr, &t, &err));
    CHECK_FALSE(ResolvePath(&p, nullptr, "health", &ptr, &t, &err));
    CHECK_FALSE(ResolvePath(&p, pt, "nope", &ptr, &t, nullptr));
    ResolvePath(&p, pt, "nope", &ptr, &t, &err);
    CHECK(err == "no field 'nope' in 'Player'");
    ResolvePath(&p, pt, "health.x", &ptr, &t, &err);
    CHECK(err == "cannot access field 'x' of type 'float'");
    ResolvePath(&p, pt, "health[0]", &ptr, &t, &err);
    CHECK(err == "cannot index type 'float'");

    CType loose;
    loose.kind = CKind::Vector;
    StructInfo si;
    si.name = "Loose";
    si.size = sizeof(std::vector<int>);
    si.fields.push_back(FieldInfo{"v", loose, 0});
    std::vector<int> vec = {1};
    CHECK_FALSE(ResolvePath(&vec, &si, "v[0]", &ptr, &t, &err));
    CHECK_FALSE(ResolvePath(&vec, &si, "v.size", &ptr, &t, &err));
}

TEST_CASE("ReadValue and WriteValue every kind") {
    Everything e;
    const StructInfo* et = StructInfoOf<Everything>();
    auto rw = [&](const char* path, const Value& in, const Value& expect) {
        void* ptr = nullptr;
        CType t;
        std::string err;
        CAPTURE(path);
        REQUIRE_MESSAGE(ResolvePath(&e, et, path, &ptr, &t, &err), err);
        REQUIRE(WriteValue(ptr, t, in));
        Value out;
        REQUIRE(ReadValue(ptr, t, out));
        CHECK(out == expect);
    };
    rw("b", Value(true), Value(true));
    rw("b", Value(0), Value(false));
    rw("b", Value("yes"), Value(true));
    rw("i8", Value(-5), Value(-5));
    rw("i8", Value(200), Value(-56));
    rw("u8", Value(200), Value(200));
    rw("u8", Value(-1), Value(255));
    rw("i16", Value(-30000), Value(-30000));
    rw("u16", Value(65535), Value(65535));
    rw("i32", Value(-123456), Value(-123456));
    rw("i32", Value(2.9), Value(2));
    rw("i32", Value("77"), Value(77));
    rw("i32", Value(true), Value(1));
    rw("u32", Value(4000000000LL), Value(4000000000LL));
    rw("i64", Value(-9000000000LL), Value(-9000000000LL));
    rw("u64", Value(123LL), Value(123LL));
    rw("u64", Value(-1), Value(-1));
    rw("l", Value(-42), Value(-42));
    rw("ul", Value(42), Value(42));
    rw("c", Value(65), Value(65));
    rw("f", Value(1.5), Value(1.5));
    rw("f", Value(3), Value(3.0));
    rw("f", Value("2.25"), Value(2.25));
    rw("d", Value(0.1), Value(0.1));
    rw("d", Value(7), Value(7.0));
    rw("v2", Value(Vec2{1, 2}), Value(Vec2{1, 2}));
    rw("v2", Value(std::vector<Value>{Value(3), Value(4)}), Value(Vec2{3, 4}));
    rw("v2", Value(ImVec2(5, 6)), Value(Vec2{5, 6}));
    rw("v4", Value(Vec4{1, 2, 3, 4}), Value(Vec4{1, 2, 3, 4}));
    rw("v4", Value::Color(1, 0, 0, 1), Value(Vec4{1, 0, 0, 1}));
    rw("col", Value(Vec4{0.5f, 0.25f, 0, 1}), Value(Vec4{0.5f, 0.25f, 0, 1}));
    CHECK(e.col.x == 0.5f);
    rw("col", Value("#ff0000ff"), Value(Vec4{1, 0, 0, 1}));
    rw("f2", Value(std::vector<Value>{Value(1.5), Value(2.5)}),
       Value(std::vector<Value>{Value(1.5), Value(2.5)}));
    rw("f2", Value(Vec2{7, 8}), Value(std::vector<Value>{Value(7.0), Value(8.0)}));
    rw("f4", Value(std::vector<Value>{Value(1), Value(2), Value(3), Value(4)}),
       Value(std::vector<Value>{Value(1.0), Value(2.0), Value(3.0), Value(4.0)}));
    rw("f4", Value::Color(0, 1, 0, 1), Value(std::vector<Value>{Value(0.0), Value(1.0), Value(0.0), Value(1.0)}));
    rw("f4", Value(Vec4{9, 8, 7, 6}), Value(std::vector<Value>{Value(9.0), Value(8.0), Value(7.0), Value(6.0)}));
    rw("iarr", Value(std::vector<Value>{Value(1), Value(2), Value(3)}),
       Value(std::vector<Value>{Value(1), Value(2), Value(3)}));
    rw("iarr", Value(std::vector<Value>{Value(9)}), Value(std::vector<Value>{Value(9), Value(2), Value(3)}));
    rw("iarr", Value(std::vector<Value>{Value(1), Value(2), Value(3), Value(4), Value(5)}),
       Value(std::vector<Value>{Value(1), Value(2), Value(3)}));
    rw("text", Value("hi"), Value("hi"));
    rw("text", Value("0123456789"), Value("0123456"));
    CHECK(e.text[7] == 0);
    rw("text", Value(42), Value("42"));
    rw("text", Value(""), Value(""));
    rw("legacy", Value(10), Value(10));
    CHECK(e.legacy == kTen);
    rw("legacy", Value(2.0), Value(2));
    rw("unreg", Value(1), Value(1));
    CHECK(e.unreg == Unregistered::B);
    rw("inner.name", Value("abc"), Value("abc"));
    rw("inner.qty", Value(5), Value(5));
    rw("pair[1].weight", Value(1.25), Value(1.25));
    rw("stats.kills", Value(3), Value(3));
    rw("stats.ratio", Value(0.75), Value(0.75));

    e.floats = {0, 0, 0};
    rw("floats", Value(std::vector<Value>{Value(1), Value(2)}),
       Value(std::vector<Value>{Value(1.0), Value(2.0), Value(0.0)}));
    rw("floats[2]", Value(9.5), Value(9.5));
    CHECK(e.floats.size() == 3);
    e.nested = {{1, 2}, {3}};
    rw("nested[0][1]", Value(20), Value(20));
    rw("nested", Value(std::vector<Value>{Value(std::vector<Value>{Value(7), Value(8)})}),
       Value(std::vector<Value>{Value(std::vector<Value>{Value(7), Value(8)}),
                                Value(std::vector<Value>{Value(3)})}));

    void* ptr = nullptr;
    CType t;
    std::string err;
    REQUIRE(ResolvePath(&e, et, "pair", &ptr, &t, &err));
    Value out;
    CHECK_FALSE(ReadValue(ptr, t, out));
    REQUIRE(ResolvePath(&e, et, "inner", &ptr, &t, &err));
    CHECK_FALSE(ReadValue(ptr, t, out));
    CHECK_FALSE(WriteValue(ptr, t, Value(1)));
    REQUIRE(ResolvePath(&e, et, "", &ptr, &t, &err));
    CHECK_FALSE(ReadValue(ptr, t, out));
}

TEST_CASE("ReadValue and WriteValue TexID and player") {
    Everything e;
    e.ptr = reinterpret_cast<void*>(static_cast<uintptr_t>(0x1234));
    const StructInfo* et = StructInfoOf<Everything>();
    void* ptr = nullptr;
    CType t;
    std::string err;
    REQUIRE(ResolvePath(&e, et, "ptr", &ptr, &t, &err));
    CHECK(t.kind == CKind::TexID);
    Value out;
    REQUIRE(ReadValue(ptr, t, out));
    CHECK(out == Value(0x1234LL));
    CHECK_FALSE(WriteValue(ptr, t, Value(1)));
    CHECK(e.ptr == reinterpret_cast<void*>(static_cast<uintptr_t>(0x1234)));
    ImTextureID tex = (ImTextureID)(intptr_t)77;
    CType texType = CTypeOf<ImTextureID>();
    CHECK((texType.kind == CKind::TexID || texType.kind == CKind::U64));
    REQUIRE(ReadValue(&tex, texType, out));
    CHECK(out == Value(77LL));

    Player p = MakePlayer();
    const StructInfo* pt = StructInfoOf<Player>();
    REQUIRE(ResolvePath(&p, pt, "health", &ptr, &t, &err));
    REQUIRE(ReadValue(ptr, t, out));
    CHECK(out == Value(75.5));
    REQUIRE(WriteValue(ptr, t, Value(10)));
    CHECK(p.health == 10.0f);
    REQUIRE(ResolvePath(&p, pt, "name", &ptr, &t, &err));
    REQUIRE(ReadValue(ptr, t, out));
    CHECK(out == Value("Hero"));
    REQUIRE(WriteValue(ptr, t, Value(std::string(40, 'x'))));
    CHECK(std::strlen(p.name) == 31);
    REQUIRE(ResolvePath(&p, pt, "pos", &ptr, &t, &err));
    REQUIRE(ReadValue(ptr, t, out));
    CHECK(out == Value(Vec2{10, 20}));
    REQUIRE(WriteValue(ptr, t, Value(Vec2{1, 2})));
    CHECK(p.pos.x == 1.0f);
    CHECK(p.pos.y == 2.0f);
    REQUIRE(ResolvePath(&p, pt, "mode", &ptr, &t, &err));
    REQUIRE(ReadValue(ptr, t, out));
    CHECK(out == Value(5));
    REQUIRE(WriteValue(ptr, t, Value(6)));
    CHECK(p.mode == Mode::Fire);
    REQUIRE(ResolvePath(&p, pt, "items[0].name", &ptr, &t, &err));
    REQUIRE(WriteValue(ptr, t, Value("axe")));
    CHECK(p.items[0].name == "axe");
    REQUIRE(ResolvePath(&p, pt, "title", &ptr, &t, &err));
    REQUIRE(WriteValue(ptr, t, Value(3.5)));
    CHECK(p.title == "3.5");
    REQUIRE(ResolvePath(&p, pt, "items.size", &ptr, &t, &err));
    REQUIRE(ReadValue(ptr, t, out));
    CHECK(out == Value(2LL));
    CHECK_FALSE(ReadValue(nullptr, t, out));
    CHECK_FALSE(WriteValue(nullptr, t, out));
    CType unknown;
    CHECK_FALSE(ReadValue(&p, unknown, out));
    CHECK_FALSE(WriteValue(&p, unknown, out));
}

TEST_CASE("hand-built enum CType without elem uses size") {
    CType t;
    t.kind = CKind::Enum;
    t.size = 4;
    int storage = 0;
    REQUIRE(WriteValue(&storage, t, Value(9)));
    CHECK(storage == 9);
    Value out;
    REQUIRE(ReadValue(&storage, t, out));
    CHECK(out == Value(9));
    t.size = 1;
    unsigned char small = 0;
    REQUIRE(WriteValue(&small, t, Value(3)));
    CHECK(small == 3);
    t.size = 8;
    long long big = 0;
    REQUIRE(WriteValue(&big, t, Value(1LL << 40)));
    CHECK(big == (1LL << 40));
    t.size = 0;
    storage = 0;
    REQUIRE(WriteValue(&storage, t, Value(4)));
    CHECK(storage == 4);
}

TEST_CASE("Error and Diagnostic defaults") {
    Error e;
    CHECK(e.message.empty());
    CHECK(e.line == 0);
    CHECK(e.col == 0);
    Diagnostic d;
    CHECK(d.level == Diagnostic::Level::Error);
    Diagnostic w{Diagnostic::Level::Warning, "m", "p", "n", "prop"};
    CHECK(w.level == Diagnostic::Level::Warning);
    CHECK(w.prop == "prop");
    CHECK(Vec2{1, 2} == Vec2{1, 2});
    CHECK(Vec2{1, 2} != Vec2{1, 3});
    CHECK(Vec4{1, 2, 3, 4} != Vec4{1, 2, 3, 5});
    CHECK(ToVec2(ImVec2(1, 2)) == Vec2{1, 2});
    CHECK(ToImVec2(Vec2{3, 4}).y == 4.0f);
    CHECK(ToVec4(ImVec4(1, 2, 3, 4)) == Vec4{1, 2, 3, 4});
    CHECK(ToImVec4(Vec4{1, 2, 3, 4}).w == 4.0f);
}

TEST_CASE("headless imgui helper renders a frame") {
    imforge::test::HeadlessImGui gui;
    REQUIRE(gui.ctx);
    CHECK(ImGui::GetIO().DisplaySize.x == 1280.0f);
    CHECK(ImGui::GetIO().DisplaySize.y == 720.0f);
    bool clicked = false;
    ImDrawData* dd = gui.Frames(2, [&] {
        ImGui::Begin("Test");
        if (ImGui::Button("Press")) clicked = true;
        ImGui::Text("hello");
        ImGui::End();
    });
    REQUIRE(dd);
#if IMGUI_VERSION_NUM >= 18980
    CHECK(dd->CmdLists.Size >= 1);
#else
    CHECK(dd->CmdListsCount >= 1);
#endif
    CHECK(dd->TotalVtxCount > 0);
    CHECK(gui.frames == 2);
    CHECK_FALSE(clicked);
}

struct TreeNode {
    int id = 0;
    std::vector<TreeNode> kids;
};
IMFORGE_REFLECT(TreeNode, id, kids);

TEST_CASE("self-referential struct reflects without recursive registration") {
    const StructInfo* si = StructInfoOf<TreeNode>();
    REQUIRE(si);
    CHECK(TypeRegistry::Global().FindStruct("TreeNode") == si);
    REQUIRE(si->fields.size() == 2);
    const CType& kids = si->fields[1].type;
    CHECK(kids.kind == CKind::Vector);
    CHECK(kids.name == "std::vector<TreeNode>");
    REQUIRE(kids.elem);
    CHECK(kids.elem->kind == CKind::Struct);
    CHECK(kids.elem->strct == si);
    CHECK(CTypeOf<TreeNode>().strct == si);
    TreeNode root;
    root.id = 1;
    root.kids.resize(2);
    root.kids[1].id = 7;
    root.kids[1].kids.resize(1);
    root.kids[1].kids[0].id = 9;
    void* p = nullptr;
    CType t;
    std::string err;
    REQUIRE_MESSAGE(ResolvePath(&root, si, "kids[1].kids[0].id", &p, &t, &err), err);
    Value v;
    REQUIRE(ReadValue(p, t, v));
    CHECK(v == Value(9));
    REQUIRE_MESSAGE(ResolvePath(&root, si, "kids.size", &p, &t, &err), err);
    REQUIRE(ReadValue(p, t, v));
    CHECK(v == Value(2));
}
