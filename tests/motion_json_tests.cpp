#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "imforge/imb_motion_presets.h"
#include "imforge/imforge_motion_json.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

#ifndef IMFORGE_SOURCE_DIR
#define IMFORGE_SOURCE_DIR "."
#endif

using imforge::Json;

namespace {

Json Parse(const char* text) {
    Json j;
    imforge::JsonError err;
    bool ok = imforge::ParseJson(text, j, &err);
    INFO(err.message);
    REQUIRE(ok);
    return j;
}

bool Close(float a, float b) { return std::fabs(a - b) <= 1e-5f * (std::fabs(a) > 1.0f ? std::fabs(a) : 1.0f); }

bool SameEase(const imb::EaseSpec& a, const imb::EaseSpec& b) {
    if (a.kind != b.kind || a.named != b.named) return false;
    for (int i = 0; i < 4; ++i)
        if (!Close(a.p[i], b.p[i])) return false;
    return true;
}

std::string DiffDefs(const imb::AnimDef& a, const imb::AnimDef& b) {
    if (std::strcmp(a.name, b.name) != 0) return "name";
    if (std::strcmp(a.category, b.category) != 0) return "category";
    if (!Close(a.duration, b.duration) || !Close(a.delay, b.delay) || a.iterations != b.iterations) return "timing";
    if (a.direction != b.direction || a.fill != b.fill) return "direction/fill";
    if (!Close(a.origin.x, b.origin.x) || !Close(a.origin.y, b.origin.y)) return "origin";
    if (a.reveal_dir != b.reveal_dir || a.default_trigger != b.default_trigger) return "revealDir/trigger";
    if (a.track_count != b.track_count) return "track count";
    for (int t = 0; t < a.track_count; ++t) {
        const imb::Track &x = a.tracks[t], &y = b.tracks[t];
        if (x.prop != y.prop || x.count != y.count) return std::string("track ") + imforge::MotionPropName(x.prop);
        for (int k = 0; k < x.count; ++k) {
            const imb::Keyframe &p = x.keys[k], &q = y.keys[k];
            bool same = Close(p.t, q.t) && SameEase(p.ease, q.ease);
            for (int c = 0; c < 4; ++c) same = same && Close(p.v[c], q.v[c]);
            if (!same) return std::string(imforge::MotionPropName(x.prop)) + "[" + std::to_string(k) + "]";
        }
    }
    return "";
}

bool FinitePose(const imb::Pose& p) {
    const float f[] = {p.opacity, p.scale_x,  p.scale_y,  p.rotate,   p.skew_x,   p.skew_y,      p.translate.x, p.translate.y,
                       p.tint.x,  p.tint.y,   p.tint.z,   p.tint.w,   p.reveal,   p.text_reveal, p.glow,        p.shadow};
    for (float v : f)
        if (!std::isfinite(v)) return false;
    return true;
}

bool IdentityPose(const imb::Pose& p) {
    const float eps = 1e-3f;
    auto same = [&](float a, float b) { return std::fabs(a - b) <= eps; };
    return same(p.opacity, 1) && same(p.scale_x, 1) && same(p.scale_y, 1) && same(p.rotate, 0) && same(p.skew_x, 0) &&
           same(p.skew_y, 0) && same(p.translate.x, 0) && same(p.translate.y, 0) && same(p.tint.x, 1) &&
           same(p.tint.y, 1) && same(p.tint.z, 1) && same(p.tint.w, 1) && same(p.reveal, 1) && same(p.text_reveal, 1) &&
           same(p.glow, 0) && same(p.shadow, 0);
}

const char* kFadeUp = R"({"name": "FadeUp", "category": "entrance", "duration": 0.4,
    "tracks": {"opacity": [{"t": 0, "v": 0}, {"t": 1, "v": 1}], "translateY": [{"t": 0, "v": 12}, {"t": 1, "v": 0}]}})";

const char* kPopTint = R"J({"name": "PopTint", "category": "text", "description": "Test pop.", "tags": ["pop", "color"],
    "credit": "me", "duration": 0.6, "delay": 0.1, "iterations": 2, "direction": "alternate", "fill": "forwards",
    "origin": {"x": 0.5, "y": 1}, "revealDir": "center", "trigger": "change", "ease": "OutCubic",
    "tracks": {
        "scale": [{"t": 0, "v": 1}, {"t": 0.3, "v": 1.2, "ease": "OutBack"}, {"t": 1, "v": 1}],
        "tint": [{"t": 0, "v": "#ffffffff"}, {"t": 0.5, "v": "#ff8040c0", "ease": "cubic-bezier(0.25, 0.1, 0.25, 1)"},
                 {"t": 1, "v": "#ffffffff"}],
        "rotate": [{"t": 0, "v": -12.5, "ease": "steps(4)"}, {"t": 1, "v": 0}],
        "glow": [{"t": 0, "v": 0, "ease": "spring(170, 26)"}, {"t": 1, "v": 0.3}]}})J";

std::string FirstError(const Json& j) {
    imforge::AnimStorage s;
    imforge::Error err;
    if (imforge::AnimFromJson(j, s, &err)) return "";
    return err.message.empty() ? "?" : err.message;
}

Json* Track(Json& j, const char* name) { return j.Find("tracks")->Find(name); }

std::vector<std::string> PresetFiles() {
    std::vector<std::string> out;
    std::filesystem::path dir = std::filesystem::path(IMFORGE_SOURCE_DIR) / "presets" / "animations";
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) return out;
    for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
        std::string name = e.path().filename().string();
        if (name.size() > 13 && name.compare(name.size() - 13, 13, ".imanims.json") == 0) out.push_back(e.path().string());
    }
    std::sort(out.begin(), out.end());
    return out;
}

}  // namespace

TEST_CASE("named eases parse") {
    const char* names[] = {"Linear", "InQuad", "OutQuad", "InOutQuad", "InCubic", "OutCubic", "InOutCubic", "OutBack",
                           "InBack", "OutElastic", "OutBounce", "OutExpo", "InSine", "OutSine", "InOutSine", "InQuart",
                           "OutQuart", "InOutQuart", "InQuint", "OutQuint", "InOutQuint", "InExpo", "InOutExpo", "InCirc",
                           "OutCirc", "InOutCirc", "InOutBack", "InElastic", "InOutElastic", "InBounce", "InOutBounce"};
    for (const char* n : names) {
        imb::EaseSpec e;
        INFO(n);
        REQUIRE(imforge::ParseEaseSpec(n, e));
        CHECK(e.kind == imb::EaseKind::Named);
        CHECK(imforge::EaseSpecToString(e) == n);
    }
}

TEST_CASE("cubic-bezier, steps and spring parse") {
    imb::EaseSpec e;
    REQUIRE(imforge::ParseEaseSpec("cubic-bezier(0.25, 0.1,0.25 ,1)", e));
    CHECK(e.kind == imb::EaseKind::CubicBezier);
    CHECK(e.p[0] == doctest::Approx(0.25));
    CHECK(e.p[1] == doctest::Approx(0.1));
    CHECK(e.p[3] == doctest::Approx(1.0));
    CHECK(imforge::EaseSpecToString(e) == "cubic-bezier(0.25, 0.1, 0.25, 1)");

    REQUIRE(imforge::ParseEaseSpec("cubic-bezier(.68,-0.55,.265,1.55)", e));
    CHECK(e.p[1] == doctest::Approx(-0.55));
    CHECK(imforge::EaseSpecToString(e) == "cubic-bezier(0.68, -0.55, 0.265, 1.55)");

    REQUIRE(imforge::ParseEaseSpec("steps( 4 )", e));
    CHECK(e.kind == imb::EaseKind::Steps);
    CHECK(e.p[0] == 4.0f);
    CHECK(imforge::EaseSpecToString(e) == "steps(4)");

    REQUIRE(imforge::ParseEaseSpec("spring(170,26)", e));
    CHECK(e.kind == imb::EaseKind::Spring);
    CHECK(e.p[0] == 170.0f);
    CHECK(e.p[1] == 26.0f);
    CHECK(imforge::EaseSpecToString(e) == "spring(170, 26)");

    imb::EaseSpec back;
    REQUIRE(imforge::ParseEaseSpec(imforge::EaseSpecToString(e), back));
    CHECK(SameEase(e, back));
}

TEST_CASE("bad eases are rejected with a reason") {
    const char* bad[] = {"Bouncy", "linear", "OutBack ", "cubic-bezier(1.5, 0, 0, 1)", "cubic-bezier(0, 9, 1, 1)",
                         "cubic-bezier(0, 0, 1)", "cubic-bezier(0,0,1,1", "steps(0)", "steps(2.5)", "steps(101)",
                         "steps(3)x", "spring(0, 10)", "spring(100)", "spring(100, -1)", "spring(1e5, 10)", "", "Spring",
                         "cubic-bezier(a, 0, 1, 1)", "steps(+3)"};
    for (const char* s : bad) {
        imb::EaseSpec e;
        e.named = imb::Ease::OutBack;
        std::string why;
        INFO(s);
        CHECK_FALSE(imforge::ParseEaseSpec(s, e, &why));
        CHECK_FALSE(why.empty());
        CHECK(e.named == imb::Ease::OutBack);
    }
}

TEST_CASE("animation json round trips") {
    Json src = Parse(kPopTint);
    imforge::AnimStorage a;
    imforge::Error err;
    REQUIRE_MESSAGE(imforge::AnimFromJson(src, a, &err), err.message);
    const imb::AnimDef& d = *a.Def();
    CHECK(std::string(d.name) == "PopTint");
    CHECK(std::string(d.category) == "text");
    CHECK(d.duration == doctest::Approx(0.6));
    CHECK(d.delay == doctest::Approx(0.1));
    CHECK(d.iterations == 2);
    CHECK(d.direction == imb::Direction::Alternate);
    CHECK(d.fill == imb::Fill::Forwards);
    CHECK(d.origin.y == 1.0f);
    CHECK(d.reveal_dir == imb::RevealDir::Center);
    CHECK(d.default_trigger == imb::Trigger::Change);
    REQUIRE(d.track_count == 4);
    CHECK(d.tracks[0].prop == imb::Prop::Scale);
    CHECK(d.tracks[1].prop == imb::Prop::Rotate);
    CHECK(d.tracks[2].prop == imb::Prop::Tint);
    CHECK(d.tracks[3].prop == imb::Prop::Glow);
    CHECK(d.tracks[0].keys[0].ease.named == imb::Ease::OutCubic);
    CHECK(d.tracks[0].keys[1].ease.named == imb::Ease::OutBack);
    CHECK(d.tracks[1].keys[0].ease.kind == imb::EaseKind::Steps);
    CHECK(d.tracks[2].keys[1].v[1] == doctest::Approx(128.0 / 255.0));
    CHECK(d.tracks[2].keys[1].ease.kind == imb::EaseKind::CubicBezier);
    CHECK(d.tracks[3].keys[0].ease.kind == imb::EaseKind::Spring);

    Json out = imforge::AnimToJson(a);
    CHECK(out["ease"].AsString() == "OutCubic");
    CHECK_FALSE(out["tracks"]["scale"][0].Has("ease"));
    CHECK(out["tracks"]["scale"][1]["ease"].AsString() == "OutBack");
    CHECK(out["tracks"]["tint"][1]["v"].AsString() == "#ff8040c0");
    CHECK(out["tags"].Size() == 2);
    CHECK(out["credit"].AsString() == "me");
    std::string text = imforge::DumpJson(out);
    CHECK(text.find("\"duration\": 0.6,") != std::string::npos);
    CHECK(text.find("0.60000") == std::string::npos);

    imforge::AnimStorage b;
    REQUIRE_MESSAGE(imforge::AnimFromJson(out, b, &err), err.message);
    CHECK(DiffDefs(*a.Def(), *b.Def()) == "");
    CHECK(imforge::DumpJson(imforge::AnimToJson(b)) == text);
    CHECK(b.data->description == "Test pop.");

    Json file = imforge::AnimToJson(b, true);
    CHECK(file["kind"].AsString() == "animation");
    CHECK(file["schemaVersion"].AsInt() == 1);
    imforge::AnimStorage c;
    REQUIRE(imforge::AnimFromJson(file, c, &err));
    CHECK(DiffDefs(*a.Def(), *c.Def()) == "");
}

TEST_CASE("defaults fill in what the json leaves out") {
    imforge::AnimStorage s;
    REQUIRE(imforge::AnimFromJson(Parse(kFadeUp), s));
    const imb::AnimDef& d = *s.Def();
    CHECK(d.delay == 0.0f);
    CHECK(d.iterations == 1);
    CHECK(d.direction == imb::Direction::Normal);
    CHECK(d.fill == imb::Fill::Both);
    CHECK(d.origin.x == 0.5f);
    CHECK(d.origin.y == 0.5f);
    CHECK(d.reveal_dir == imb::RevealDir::Left);
    CHECK(d.default_trigger == imb::Trigger::Enter);
    CHECK(d.tracks[0].keys[0].ease.kind == imb::EaseKind::Named);
    CHECK(d.tracks[0].keys[0].ease.named == imb::Ease::Linear);

    Json loop = Parse(R"({"name": "Bob", "category": "loop", "duration": 1,
        "tracks": {"translateY": [{"t": 0, "v": 0}, {"t": 0.5, "v": -4}, {"t": 1, "v": 0}]}})");
    REQUIRE(imforge::AnimFromJson(loop, s));
    CHECK(s.Def()->iterations == 0);
    CHECK(s.Def()->default_trigger == imb::Trigger::Loop);
    CHECK_FALSE(imforge::AnimToJson(s).Has("ease"));
}

TEST_CASE("storage keeps defs valid across moves and copies") {
    std::vector<imforge::AnimStorage> list;
    std::vector<const imb::AnimDef*> defs;
    Json src = Parse(kFadeUp);
    for (int i = 0; i < 40; ++i) {
        src.Set("name", Json("Fade" + std::to_string(i)));
        imforge::AnimStorage s;
        REQUIRE(imforge::AnimFromJson(src, s));
        defs.push_back(s.Def());
        list.push_back(std::move(s));
    }
    for (int i = 0; i < 40; ++i) {
        CHECK(list[i].Def() == defs[i]);
        CHECK(std::string(defs[i]->name) == "Fade" + std::to_string(i));
        CHECK(defs[i]->tracks[1].keys[0].v[0] == 12.0f);
    }

    imforge::AnimStorage copy = list[3];
    CHECK(copy.Def() != list[3].Def());
    CHECK(copy.Def()->tracks != list[3].Def()->tracks);
    CHECK(copy.Def()->name != list[3].Def()->name);
    CHECK(DiffDefs(*copy.Def(), *list[3].Def()) == "");
    copy.data->keys[0][1].v[0] = 0.5f;
    CHECK(list[3].Def()->tracks[0].keys[1].v[0] == 1.0f);

    copy = list[5];
    CHECK(std::string(copy.Def()->name) == "Fade5");
    imforge::AnimStorage moved(std::move(copy));
    CHECK(copy.Def() == nullptr);
    CHECK(std::string(moved.Def()->name) == "Fade5");
}

TEST_CASE("invalid animations are rejected with a clear message") {
    struct Case {
        const char* expect;
        std::function<void(Json&)> edit;
    };
    const Case cases[] = {
        {"PascalCase", [](Json& j) { j.Set("name", Json("fadeUp")); }},
        {"PascalCase", [](Json& j) { j.Set("name", Json("Fade-Up")); }},
        {"reserved", [](Json& j) { j.Set("name", Json("RegisterAll")); }},
        {"duration", [](Json& j) { j.Erase("duration"); }},
        {"duration", [](Json& j) { j.Set("duration", Json(0)); }},
        {"unknown key \"speed\"", [](Json& j) { j.Set("speed", Json(2)); }},
        {"schemaVersion must be 1", [](Json& j) { j.Set("kind", Json("animation")); j.Set("schemaVersion", Json(2)); }},
        {"kind must be", [](Json& j) { j.Set("kind", Json("animationSet")); }},
        {"unknown track \"blur\"", [](Json& j) { j.Find("tracks")->Set("blur", Json::Array()); }},
        {"unknown category", [](Json& j) { j.Set("category", Json("sideways")); }},
        {"category is missing", [](Json& j) { j.Erase("category"); }},
        {"t must increase", [](Json& j) { Track(j, "opacity")->arr.insert(Track(j, "opacity")->arr.begin() + 1, Parse(R"({"t": 0, "v": 0.5})")); }},
        {"first keyframe must have t = 0", [](Json& j) { Track(j, "opacity")->arr[0].Set("t", Json(0.1)); }},
        {"last keyframe must have t = 1", [](Json& j) { Track(j, "opacity")->arr[1].Set("t", Json(0.9)); }},
        {"t must be a number", [](Json& j) { Track(j, "opacity")->arr[1].Set("t", Json(1.5)); }},
        {"at least 2 keyframes", [](Json& j) { Track(j, "opacity")->arr.pop_back(); }},
        {"the limit is 64",
         [](Json& j) {
             Json arr = Json::Array();
             for (int i = 0; i < 65; ++i) arr.Push(Parse(("{\"t\": " + std::to_string(i / 64.0) + ", \"v\": 0}").c_str()));
             j.Find("tracks")->Set("glow", arr);
         }},
        {"outside 0..1", [](Json& j) { Track(j, "opacity")->arr[0].Set("v", Json(1.5)); }},
        {"outside 0..3", [](Json& j) { j.Find("tracks")->Set("scale", Parse(R"([{"t": 0, "v": 4}, {"t": 1, "v": 1}])")); }},
        {"outside -720..720", [](Json& j) { j.Find("tracks")->Set("rotate", Parse(R"([{"t": 0, "v": 800}, {"t": 1, "v": 0}])")); }},
        {"#rrggbbaa", [](Json& j) { j.Find("tracks")->Set("tint", Parse(R"([{"t": 0, "v": "#fff"}, {"t": 1, "v": "#ffffffff"}])")); }},
        {"#rrggbbaa", [](Json& j) { j.Find("tracks")->Set("tint", Parse(R"([{"t": 0, "v": 1}, {"t": 1, "v": "#ffffffff"}])")); }},
        {"v must be a number", [](Json& j) { Track(j, "opacity")->arr[0].Set("v", Json("0")); }},
        {"unknown key \"value\"", [](Json& j) { Track(j, "opacity")->arr[0].Set("value", Json(0)); }},
        {"unknown ease \"Bouncy\"", [](Json& j) { Track(j, "opacity")->arr[0].Set("ease", Json("Bouncy")); }},
        {"ease unknown ease", [](Json& j) { j.Set("ease", Json("Bouncy")); }},
        {"must end at identity: tracks.opacity ends at 0.5", [](Json& j) { Track(j, "opacity")->arr[1].Set("v", Json(0.5)); }},
        {"must end at identity", [](Json& j) { j.Set("direction", Json("reverse")); }},
        {"must end at identity", [](Json& j) { j.Set("direction", Json("alternate")); j.Set("iterations", Json(2)); }},
        {"can't repeat forever", [](Json& j) { j.Set("iterations", Json(0)); }},
        {"iterations must be an integer", [](Json& j) { j.Set("iterations", Json(1.5)); }},
        {"direction must be one of", [](Json& j) { j.Set("direction", Json("sideways")); }},
        {"fill must be one of", [](Json& j) { j.Set("fill", Json(true)); }},
        {"trigger must be one of", [](Json& j) { j.Set("trigger", Json("click")); }},
        {"revealDir must be one of", [](Json& j) { j.Set("revealDir", Json("north")); }},
        {"origin must be", [](Json& j) { j.Set("origin", Parse(R"({"x": 0.5})")); }},
        {"origin must be", [](Json& j) { j.Set("origin", Parse(R"({"x": 0.5, "y": 5})")); }},
        {"tags", [](Json& j) { j.Set("tags", Parse(R"(["a", "a"])")); }},
        {"tags", [](Json& j) { j.Set("tags", Parse(R"(["a", ""])")); }},
        {"description must be a string", [](Json& j) { j.Set("description", Json(3)); }},
        {"tracks must be an object", [](Json& j) { j.Set("tracks", Json::Object()); }},
    };
    for (const Case& c : cases) {
        Json j = Parse(kFadeUp);
        c.edit(j);
        std::string msg = FirstError(j);
        INFO(c.expect, " / got: ", msg);
        CHECK(msg.find(c.expect) != std::string::npos);
    }
    Json j = Parse(kFadeUp);
    Track(j, "opacity")->arr[1].Set("v", Json(0.5));
    CHECK(FirstError(j).rfind("animation \"FadeUp\": ", 0) == 0);

    CHECK(FirstError(Parse(R"({"name": "A", "name": "B", "category": "entrance", "duration": 1,
        "tracks": {"opacity": [{"t": 0, "v": 0}, {"t": 1, "v": 1}]}})")).find("duplicate key \"name\"") != std::string::npos);
    CHECK(FirstError(Parse(R"({"name": "A", "category": "entrance", "duration": 1, "tracks": {
        "opacity": [{"t": 0, "v": 0}, {"t": 1, "v": 1}], "opacity": [{"t": 0, "v": 0}, {"t": 1, "v": 1}]}})"))
              .find("duplicate track") != std::string::npos);

    imforge::AnimStorage keep;
    REQUIRE(imforge::AnimFromJson(Parse(kFadeUp), keep));
    Json bad = Parse(kFadeUp);
    bad.Set("name", Json("Other"));
    bad.Set("duration", Json(-1));
    CHECK_FALSE(imforge::AnimFromJson(bad, keep));
    CHECK(std::string(keep.Def()->name) == "FadeUp");
}

TEST_CASE("identity and loop rules follow direction and count full turns") {
    Json exit = Parse(R"({"name": "Out", "category": "exit", "duration": 0.3,
        "tracks": {"opacity": [{"t": 0, "v": 1}, {"t": 1, "v": 0}]}})");
    CHECK(FirstError(exit) == "");
    Track(exit, "opacity")->arr[0].Set("v", Json(0.5));
    CHECK(FirstError(exit).find("must start at identity: tracks.opacity starts at 0.5, expected 1") != std::string::npos);
    exit.Set("direction", Json("reverse"));
    Track(exit, "opacity")->arr[0].Set("v", Json(0));
    Track(exit, "opacity")->arr[1].Set("v", Json(1));
    CHECK(FirstError(exit) == "");

    Json in = Parse(kFadeUp);
    in.Set("direction", Json("alternate"));
    in.Set("iterations", Json(3));
    CHECK(FirstError(in) == "");
    in.Set("direction", Json("alternate-reverse"));
    CHECK(FirstError(in).find("must end at identity") != std::string::npos);

    Json loop = Parse(R"({"name": "Spinner", "category": "loop", "duration": 1,
        "tracks": {"rotate": [{"t": 0, "v": 0}, {"t": 1, "v": -360}]}})");
    CHECK(FirstError(loop) == "");
    Track(loop, "rotate")->arr[1].Set("v", Json(720));
    CHECK(FirstError(loop) == "");
    Track(loop, "rotate")->arr[1].Set("v", Json(180));
    CHECK(FirstError(loop).find("loops must end where they start: tracks.rotate starts at 0 and ends at 180") != std::string::npos);
    loop.Set("direction", Json("alternate"));
    CHECK(FirstError(loop) == "");

    Json pulse = Parse(R"({"name": "Glowy", "category": "loop", "duration": 1,
        "tracks": {"glow": [{"t": 0, "v": 0}, {"t": 1, "v": 0.5}]}})");
    CHECK(FirstError(pulse).find("end where they start") != std::string::npos);
    Json tint = Parse(R"({"name": "Flicker", "category": "loop", "duration": 1,
        "tracks": {"tint": [{"t": 0, "v": "#ffffffff"}, {"t": 0.5, "v": "#ff0000ff"}, {"t": 1, "v": "#FFFFFFFF"}]}})");
    CHECK(FirstError(tint) == "");
    Json bob = Parse(R"({"name": "Bob", "category": "loop", "duration": 1,
        "tracks": {"translateY": [{"t": 0, "v": -4}, {"t": 0.5, "v": 0}, {"t": 1, "v": -4}]}})");
    CHECK(FirstError(bob).find("loop animations must start at identity") != std::string::npos);

    Json lift = Parse(R"({"name": "Lift", "category": "hover", "duration": 0.2,
        "tracks": {"translateY": [{"t": 0, "v": 0}, {"t": 1, "v": -3}], "shadow": [{"t": 0, "v": 0}, {"t": 1, "v": 0.6}]}})");
    CHECK(FirstError(lift) == "");
    lift.Set("category", Json("press"));
    CHECK(FirstError(lift) == "");
    Track(lift, "shadow")->arr[0].Set("v", Json(0.2));
    CHECK(FirstError(lift).find("press animations must start at identity: tracks.shadow starts at 0.2, expected 0") !=
          std::string::npos);

    Json shake = Parse(R"({"name": "Shake", "category": "attention", "duration": 0.5,
        "tracks": {"translateX": [{"t": 0, "v": 0}, {"t": 0.5, "v": 6}, {"t": 1, "v": 0}]}})");
    CHECK(FirstError(shake) == "");
    Track(shake, "translateX")->arr[2].Set("v", Json(2));
    CHECK(FirstError(shake).find("attention animations must end at identity") != std::string::npos);
    shake.Set("category", Json("change"));
    CHECK(FirstError(shake).find("change animations must end at identity") != std::string::npos);
    shake.Set("category", Json("text"));
    CHECK(FirstError(shake) == "");
}

TEST_CASE("animation sets") {
    const char* set = R"({"schemaVersion": 1, "kind": "animationSet", "category": "entrance", "animations": [
        {"name": "A", "duration": 0.2, "tracks": {"opacity": [{"t": 0, "v": 0}, {"t": 1, "v": 1}]}},
        {"name": "B", "category": "entrance", "duration": 0.2, "tracks": {"scale": [{"t": 0, "v": 0}, {"t": 1, "v": 1}]}}]})";
    std::vector<imforge::AnimStorage> list;
    imforge::Error err;
    REQUIRE_MESSAGE(imforge::AnimSetFromJson(Parse(set), list, &err), err.message);
    REQUIRE(list.size() == 2);
    CHECK(std::string(list[0].Def()->category) == "entrance");

    Json dup = Parse(set);
    dup.Find("animations")->arr[1].Set("name", Json("A"));
    CHECK_FALSE(imforge::AnimSetFromJson(dup, list, &err));
    CHECK(err.message.find("duplicate name") != std::string::npos);
    CHECK(list.size() == 2);

    Json mismatch = Parse(set);
    mismatch.Find("animations")->arr[1].Set("category", Json("exit"));
    CHECK_FALSE(imforge::AnimSetFromJson(mismatch, list, &err));
    CHECK(err.message.find("animation \"B\": category \"exit\" does not match the set category \"entrance\"") != std::string::npos);

    Json kind = Parse(set);
    kind.Set("kind", Json("animation"));
    CHECK_FALSE(imforge::AnimSetFromJson(kind, list, &err));

    Json bad_item = Parse(set);
    bad_item.Find("animations")->arr[0].Erase("name");
    CHECK_FALSE(imforge::AnimSetFromJson(bad_item, list, &err));
    CHECK(err.message.rfind("animations[0]: ", 0) == 0);

    Json payload = Parse(R"({"animations": [{"name": "Nudge", "category": "attention", "duration": 0.3,
        "tracks": {"translateX": [{"t": 0, "v": 0}, {"t": 0.5, "v": 4}, {"t": 1, "v": 0}]}}]})");
    REQUIRE(imforge::AnimSetFromJson(payload, list, &err));
    REQUIRE(list.size() == 1);
    CHECK(list[0].Def()->default_trigger == imb::Trigger::Attention);
    payload.Find("animations")->arr[0].Erase("category");
    CHECK_FALSE(imforge::AnimSetFromJson(payload, list, &err));
    CHECK(err.message.find("category is missing") != std::string::npos);
}

TEST_CASE("presets are registered and listed in order") {
    int count = -1;
    const char* const* names = imb::presets::Names(count);
    REQUIRE(count >= 0);
    CHECK(imb::AnimCount() >= count);
    int last_cat = -1;
    std::string last_name;
    for (int i = 0; i < count; ++i) {
        const imb::AnimDef* d = imb::FindAnim(names[i]);
        INFO(names[i]);
        REQUIRE(d != nullptr);
        CHECK(std::strcmp(d->name, names[i]) == 0);
        int cat = imforge::MotionCategoryIndex(d->category);
        REQUIRE(cat >= 0);
        CHECK(cat >= last_cat);
        if (cat == last_cat) CHECK(last_name < std::string(names[i]));
        last_cat = cat;
        last_name = names[i];
    }
}

TEST_CASE("every preset samples cleanly and keeps its identity rules") {
    int count = 0;
    const char* const* names = imb::presets::Names(count);
    for (int i = 0; i < count; ++i) {
        const imb::AnimDef& d = *imb::FindAnim(names[i]);
        INFO(d.name);
        float span = d.delay + d.duration * static_cast<float>(d.iterations > 0 ? d.iterations : 2) + 0.5f;
        bool finite = true;
        for (int s = 0; s < 200; ++s) finite = finite && FinitePose(imb::SampleAnim(d, span * static_cast<float>(s) / 199.0f));
        CHECK(finite);
        std::string cat = d.category;
        if (cat == "entrance") {
            CHECK(d.iterations > 0);
            CHECK(IdentityPose(imb::SampleAnim(d, d.delay + d.duration * static_cast<float>(d.iterations) + 0.01f)));
            CHECK(imb::AnimFinished(d, d.delay + d.duration * static_cast<float>(d.iterations) + 0.01f));
        } else if (cat == "exit") {
            CHECK(IdentityPose(imb::SampleAnim(d, 0.0f)));
        } else if (cat == "loop" && (d.direction == imb::Direction::Normal || d.direction == imb::Direction::Reverse)) {
            for (int t = 0; t < d.track_count; ++t) {
                const imb::Track& tr = d.tracks[t];
                const imb::Keyframe &a = tr.keys[0], &b = tr.keys[tr.count - 1];
                INFO(imforge::MotionPropName(tr.prop));
                if (tr.prop == imb::Prop::Rotate) {
                    float turns = (b.v[0] - a.v[0]) / 360.0f;
                    CHECK(std::fabs(turns - std::round(turns)) < 1e-4f);
                } else {
                    for (int c = 0; c < 4; ++c) CHECK(a.v[c] == doctest::Approx(b.v[c]));
                }
            }
        }
        imforge::AnimStorage back;
        imforge::Error err;
        REQUIRE_MESSAGE(imforge::AnimFromJson(imforge::AnimToJson(d), back, &err), err.message);
        CHECK(DiffDefs(d, *back.Def()) == "");
    }
}

TEST_CASE("compiled presets match presets/animations") {
    int count = 0;
    imb::presets::Names(count);
    int seen = 0;
    for (const std::string& path : PresetFiles()) {
        std::ifstream f(path, std::ios::binary);
        REQUIRE(f.good());
        std::stringstream ss;
        ss << f.rdbuf();
        Json j;
        imforge::JsonError jerr;
        INFO(path);
        REQUIRE_MESSAGE(imforge::ParseJson(ss.str(), j, &jerr), jerr.message);
        std::vector<imforge::AnimStorage> list;
        imforge::Error err;
        REQUIRE_MESSAGE(imforge::AnimSetFromJson(j, list, &err), err.message);
        for (const imforge::AnimStorage& s : list) {
            const imb::AnimDef* compiled = imb::FindAnim(s.Name());
            INFO(s.Name(), " (run python tools/gen_motion_presets.py)");
            REQUIRE(compiled != nullptr);
            CHECK(DiffDefs(*s.Def(), *compiled) == "");
            CHECK_FALSE(s.data->description.empty());
            ++seen;
        }
    }
    CHECK(seen == count);
}
