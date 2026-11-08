#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "../src/builder/panels/anim_keyframe_edit.cpp"
#include "imforge/imforge_motion_json.h"

using namespace imforge;
using namespace imforge::builder::kf;

namespace {

TrackEdit ThreeKeys() {
    TrackEdit tr = MakeTrack("scale");
    float v = 1.2f;
    InsertKey(tr, 0.5f, &v, "Linear");
    return tr;
}

}  // namespace

TEST_CASE("snap rounds to 0.05 unless free") {
    CHECK(SnapTime(0.12f, false) == doctest::Approx(0.10f));
    CHECK(SnapTime(0.13f, false) == doctest::Approx(0.15f));
    CHECK(SnapTime(0.13f, true) == doctest::Approx(0.13f));
    CHECK(SnapTime(-0.4f, false) == 0.0f);
    CHECK(SnapTime(1.7f, true) == 1.0f);
}

TEST_CASE("retime clamps between neighbours and never moves endpoints") {
    TrackEdit tr = ThreeKeys();
    REQUIRE(tr.keys.size() == 3);
    CHECK(RetimeKey(tr, 1, 0.33f, false) == doctest::Approx(0.35f));
    CHECK(RetimeKey(tr, 1, 0.333f, true) == doctest::Approx(0.333f));
    CHECK(RetimeKey(tr, 1, 1.5f, false) < 1.0f);
    CHECK(IsSortedWithEndpoints(tr));
    CHECK(RetimeKey(tr, 1, -2.0f, true) > 0.0f);
    CHECK(IsSortedWithEndpoints(tr));
    CHECK(RetimeKey(tr, 0, 0.5f, false) == 0.0f);
    CHECK(RetimeKey(tr, 2, 0.5f, false) == 1.0f);
    CHECK(IsSortedWithEndpoints(tr));
}

TEST_CASE("insert keeps keys sorted and samples the curve") {
    TrackEdit tr = MakeTrack("opacity");
    tr.keys[0].v[0] = 0.0f;
    int i = InsertKey(tr, 0.25f, nullptr, "Linear");
    CHECK(i == 1);
    CHECK(tr.keys[1].v[0] == doctest::Approx(0.25f));
    int j = InsertKey(tr, 0.75f, nullptr, "Linear");
    CHECK(j == 2);
    int k = InsertKey(tr, 0.5f, nullptr, "Linear");
    CHECK(k == 2);
    CHECK(tr.keys.size() == 5);
    CHECK(IsSortedWithEndpoints(tr));
    CHECK(InsertKey(tr, 0.501f, nullptr, "Linear") == 2);
    CHECK(tr.keys.size() == 5);

    EaseFn half = [](const std::string&, float) { return 0.5f; };
    TrackEdit tr2 = MakeTrack("opacity");
    tr2.keys[0].v[0] = 0.0f;
    InsertKey(tr2, 0.1f, nullptr, "Linear", half);
    CHECK(tr2.keys[1].v[0] == doctest::Approx(0.5f));
}

TEST_CASE("new key inherits the segment ease") {
    TrackEdit tr = MakeTrack("rotate");
    tr.keys[0].ease = "OutBack";
    int i = InsertKey(tr, 0.4f, nullptr, "Linear");
    CHECK(tr.keys[(size_t)i].ease == "OutBack");
}

TEST_CASE("delete refuses endpoints and keeps two keys") {
    TrackEdit tr = ThreeKeys();
    CHECK_FALSE(DeleteKey(tr, 0));
    CHECK_FALSE(DeleteKey(tr, 2));
    CHECK(DeleteKey(tr, 1));
    CHECK(tr.keys.size() == 2);
    CHECK_FALSE(DeleteKey(tr, 1));
    CHECK(IsSortedWithEndpoints(tr));
}

TEST_CASE("track is capped at 64 keys") {
    TrackEdit tr = MakeTrack("glow");
    int added = 0;
    for (int i = 1; i < 200; ++i)
        if (InsertKey(tr, (float)i / 200.0f, nullptr, "Linear") >= 0) ++added;
    CHECK((int)tr.keys.size() == kMaxKeysPerTrack);
    CHECK(IsSortedWithEndpoints(tr));
}

TEST_CASE("normalize pins endpoints, sorts and drops duplicates") {
    TrackEdit tr;
    tr.prop = "translateX";
    Key a, b, c, d;
    a.t = 0.8f;
    b.t = 0.2f;
    c.t = 0.2f;
    d.t = 1.4f;
    tr.keys = {a, b, c, d};
    NormalizeTrack(tr);
    CHECK(tr.keys.size() == 3);
    CHECK(IsSortedWithEndpoints(tr));

    TrackEdit one;
    one.prop = "opacity";
    Key k;
    k.t = 0.3f;
    k.v[0] = 0.7f;
    one.keys = {k};
    NormalizeTrack(one);
    REQUIRE(one.keys.size() == 2);
    CHECK(one.keys[1].v[0] == doctest::Approx(0.7f));
    CHECK(IsSortedWithEndpoints(one));
}

TEST_CASE("ease strings round trip") {
    const char* canon[] = {"Linear", "OutCubic", "OutBack", "cubic-bezier(0.25,0.1,0.25,1)", "cubic-bezier(0,-0.5,1,1.5)",
                           "steps(4)", "spring(170,26)", "spring(300,12.5)"};
    for (const char* s : canon) {
        EaseParsed e;
        REQUIRE_MESSAGE(ParseEase(s, e), s);
        CHECK(FormatEase(e) == s);
        EaseParsed again;
        REQUIRE(ParseEase(FormatEase(e), again));
        CHECK(again.type == e.type);
        for (int i = 0; i < 4; ++i) CHECK(again.p[i] == doctest::Approx(e.p[i]));
    }
    EaseParsed spaced;
    REQUIRE(ParseEase(" cubic-bezier( 0.4 , 0 , 0.2 , 1 ) ", spaced));
    CHECK(FormatEase(spaced) == "cubic-bezier(0.4,0,0.2,1)");

    EaseParsed b;
    b.type = EaseType::CubicBezier;
    b.p[0] = 0.123456f;
    b.p[1] = 1.0f / 3.0f;
    b.p[2] = 0.5f;
    b.p[3] = 1.0f;
    CHECK(FormatEase(b) == "cubic-bezier(0.123,0.333,0.5,1)");
}

TEST_CASE("bad ease strings are rejected") {
    const char* bad[] = {"", "Bouncy", "cubic-bezier(1.5,0,0,1)", "cubic-bezier(0,0,1)", "steps(0)", "steps(2.5)",
                         "spring(0,10)", "spring(100,0)", "spring(100)", "steps(101)", "cubic-bezier(0,6,1,1)", "steps(x)", "cubic-bezier(0,0,1,1"};
    for (const char* s : bad) CHECK_MESSAGE(!IsValidEase(s), s);
}

TEST_CASE("anim json round trip keeps tracks and header") {
    const char* text = R"({"name":"Wiggle","category":"attention","duration":0.8,"delay":0.1,"iterations":2,
        "direction":"alternate","fill":"none","origin":{"x":0.5,"y":1},"revealDir":"up","trigger":"attention",
        "ease":"InOutSine","tags":["fun"],
        "tracks":{"rotate":[{"t":0,"v":0,"ease":"OutBack"},{"t":0.5,"v":8},{"t":1,"v":0}],
                  "tint":[{"t":0,"v":"#ffffffff"},{"t":1,"v":"#ff000080"}]}})";
    Json j;
    REQUIRE(ParseJson(text, j));
    AnimEdit a;
    std::string err;
    REQUIRE_MESSAGE(AnimEditFromJson(j, a, &err), err);
    CHECK(a.name == "Wiggle");
    CHECK(a.iterations == 2);
    CHECK(a.origin_y == doctest::Approx(1.0f));
    REQUIRE(a.tracks.size() == 2);
    CHECK(a.tracks[0].keys.size() == 3);
    CHECK(a.tracks[0].keys[0].ease == "OutBack");
    CHECK(a.tracks[1].keys[1].v[3] == doctest::Approx(128.0f / 255.0f));
    Json back = AnimEditToJson(a);
    CHECK(DumpJson(back) == DumpJson(AnimEditToJson(a)));
    AnimEdit b;
    REQUIRE(AnimEditFromJson(back, b));
    CHECK(DumpJson(AnimEditToJson(b)) == DumpJson(back));
    CHECK(back["tracks"]["tint"][1]["v"].AsString() == "#ff000080");
    CHECK(back["tracks"]["rotate"][1]["v"].AsDouble() == doctest::Approx(8.0));
}

TEST_CASE("anim json with bad tracks fails with a message") {
    Json j;
    REQUIRE(ParseJson(R"({"name":"X","tracks":{"opacity":3}})", j));
    AnimEdit a;
    std::string err;
    CHECK_FALSE(AnimEditFromJson(j, a, &err));
    CHECK(err.find("opacity") != std::string::npos);
}

TEST_CASE("unique names") {
    std::vector<std::string> taken = {"MyAnim", "MyAnim2", "BounceInCopy"};
    CHECK(UniqueAnimName("MyAnim", taken) == "MyAnim3");
    CHECK(UniqueAnimName("my anim", taken) == "MyAnim3");
    CHECK(UniqueAnimName("BounceInCopy", taken) == "BounceInCopy2");
    CHECK(UniqueAnimName("3d spin", taken) == "Anim3dSpin");
    CHECK(IsValidAnimName("Wiggle2"));
    CHECK_FALSE(IsValidAnimName("Wiggle_2"));
    CHECK_FALSE(IsValidAnimName("wiggle"));
    CHECK_FALSE(IsValidAnimName("2Wiggle"));
    CHECK_FALSE(IsValidAnimName("Wig gle"));
    CHECK_FALSE(IsValidAnimName("None"));
    CHECK(UniqueAnimName("none", taken) == "None2");
    CHECK(UniqueAnimName("my_anim", taken) == "MyAnim3");
}

TEST_CASE("edited json goes through the motion loader") {
    AnimEdit a = MakeDefaultAnim("Rise");
    float v = 0.9f;
    InsertKey(a.tracks[0], 0.3f, &v, a.ease);
    a.tracks[0].keys[1].ease = "cubic-bezier(0.2,0.8,0.3,1.2)";
    TrackEdit tint = MakeTrack("tint");
    tint.keys[1].ease = "steps(3)";
    a.tracks.push_back(tint);
    Json j = AnimEditToJson(a);
    AnimStorage st;
    Error e;
    REQUIRE_MESSAGE(AnimFromJson(j, st, &e), e.message);
    REQUIRE(st.Def());
    CHECK(st.Def()->track_count == 3);
    CHECK(std::string(st.Def()->name) == "Rise");

    AnimEdit back;
    REQUIRE(AnimEditFromJson(AnimToJson(st), back));
    CHECK(back.ease == "OutCubic");
    REQUIRE(back.tracks.size() == 3);
}

TEST_CASE("my ease strings parse the same way as the loader") {
    const char* list[] = {"InOutSine", "cubic-bezier(0.68,-0.55,0.265,1.55)", "steps(5)", "spring(220,18)"};
    for (const char* s : list) {
        EaseParsed mine;
        imb::EaseSpec theirs;
        REQUIRE(ParseEase(s, mine));
        REQUIRE(ParseEaseSpec(FormatEase(mine), theirs));
        CHECK((int)theirs.kind == (int)mine.type);
        if (mine.type != EaseType::Named)
            for (int i = 0; i < 4; ++i) CHECK(theirs.p[i] == doctest::Approx(mine.p[i]));
    }
}

TEST_CASE("key indices past the end are ignored") {
    TrackEdit tr = ThreeKeys();
    CHECK_FALSE(CanDeleteKey(tr, -1));
    CHECK_FALSE(CanDeleteKey(tr, 3));
    CHECK_FALSE(DeleteKey(tr, 99));
    CHECK(RetimeKey(tr, 7, 0.5f, true) == 0.0f);
    CHECK(RetimeKey(tr, -2, 0.5f, true) == 0.0f);
    CHECK(tr.keys.size() == 3);
}

TEST_CASE("deleting the last interior key after tracks shrink leaves a valid track") {
    AnimEdit a = MakeDefaultAnim("Shrink");
    TrackEdit& tr = a.tracks[0];
    float v = 0.5f;
    InsertKey(tr, 0.3f, &v, "Linear");
    InsertKey(tr, 0.6f, &v, "Linear");
    int key = (int)tr.keys.size() - 2;
    CHECK(DeleteKey(tr, key));
    CHECK(DeleteKey(tr, key - 1));
    CHECK(tr.keys.size() == 2);
    CHECK_FALSE(DeleteKey(tr, key));
    a.tracks.erase(a.tracks.begin());
    CHECK(AnimEditToJson(a)["tracks"].obj.size() == 1);
}

TEST_CASE("json with a one key track or an unknown property still loads") {
    Json j = AnimEditToJson(MakeDefaultAnim("Odd"));
    Json one = Json::Array();
    Json k = Json::Object();
    k.Set("t", Json(0.5));
    k.Set("v", Json(0.25));
    one.Push(k);
    Json tracks = j["tracks"];
    tracks.Set("opacity", one);
    tracks.Set("madeUp", one);
    j.Set("tracks", tracks);
    AnimEdit out;
    std::string err;
    REQUIRE(AnimEditFromJson(j, out, &err));
    for (const TrackEdit& tr : out.tracks) {
        CHECK(IsSortedWithEndpoints(tr));
        CHECK(tr.keys.size() >= 2);
    }
}
