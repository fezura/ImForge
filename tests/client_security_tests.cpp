#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#ifndef IMFORGE_LIVE
#define IMFORGE_LIVE
#endif
#include "imforge/imforge_client.h"
#include "font_test_util.h"
#include "imgui_headless.h"

#include <cstdint>
#include <cstring>
#include <random>
#include <string>
#include <vector>

using namespace imforge;
namespace live = imforge::live;

namespace {

Json ParseOrFail(const std::string& text) {
    Json j;
    JsonError err;
    bool ok = ParseJson(text, j, &err);
    REQUIRE_MESSAGE(ok, err.message);
    return j;
}

std::string NestedNodes(int levels) {
    std::string open, close;
    for (int i = 0; i < levels; ++i) {
        open += R"({"id":"n)" + std::to_string(i) + R"(","type":"VStack","children":[)";
        close += "]}";
    }
    return open + R"({"id":"leaf","type":"Text","props":{"text":"x"}})" + close;
}

std::string PanelJson(const std::string& name, const std::string& root) {
    return R"({"schemaVersion":1,"kind":"panel","name":")" + name + R"(","root":)" + root + "}";
}

Json PanelMsg(const std::string& name, const std::string& root) {
    return MsgPanel(ParseOrFail(PanelJson(name, root)));
}

bool HasDiag(const Interpreter& in, const char* needle) {
    for (const Diagnostic& d : in.last_diagnostics)
        if (d.message.find(needle) != std::string::npos) return true;
    return false;
}

}  // namespace

TEST_CASE("client rejects panels that are too deeply nested or too large") {
    live::client_detail::Client& c = live::client_detail::Get();
    c.panels.clear();
    live::client_detail::Dispatch(c, PanelMsg("Deep", NestedNodes(80)));
    CHECK(live::DebugPanelCount() == 0);
    CHECK(c.last_error.find("too large or too deeply nested") != std::string::npos);

    live::client_detail::Dispatch(c, PanelMsg("Fine", NestedNodes(10)));
    CHECK(live::DebugPanelCount() == 1);

    std::string wide = R"({"id":"root","type":"VStack","children":[)";
    for (size_t i = 0; i <= live::client_detail::kMaxNodesPerDoc; ++i) wide += R"({"id":"t","type":"Text"},)";
    wide += R"({"id":"last","type":"Text"}]})";
    live::client_detail::Dispatch(c, PanelMsg("Wide", wide));
    CHECK(live::DebugPanelCount() == 1);
    c.panels.clear();
}

TEST_CASE("client caps the number of panels") {
    live::client_detail::Client& c = live::client_detail::Get();
    c.panels.clear();
    for (size_t i = 0; i < live::client_detail::kMaxPanels + 20; ++i)
        live::client_detail::Dispatch(c, PanelMsg("P" + std::to_string(i), NestedNodes(1)));
    CHECK(live::DebugPanelCount() == live::client_detail::kMaxPanels);
    live::client_detail::Dispatch(c, PanelMsg("P0", NestedNodes(2)));
    CHECK(live::DebugPanelCount() == live::client_detail::kMaxPanels);
    c.panels.clear();
}

TEST_CASE("unknown message types are not echoed unbounded") {
    live::client_detail::Client& c = live::client_detail::Get();
    static std::string last;
    live::SetLogCallback([](int, const char* m) { last = m; });
    Json m = Json::Object();
    m.Set("t", std::string(100000, 'q'));
    live::client_detail::Dispatch(c, m);
    CHECK(last.size() < 200);
    live::SetLogCallback(nullptr);
}

TEST_CASE("before pairing the client applies nothing and rejects a builder that cannot prove the code") {
    namespace cd = live::client_detail;
    cd::Client& c = cd::Get();
    c.panels.clear();
    c.link = cd::Link::Pairing;
    REQUIRE(c.handshake.Begin(pairing::GenerateCode()));
    c.ignored_unpaired = 0;
    cd::HandlePairing(c, PanelMsg("Early", NestedNodes(1)), 0);
    cd::HandlePairing(c, MsgMode(true, "x", "Early"), 0);
    cd::HandlePairing(c, MsgPing(), 0);
    CHECK(live::DebugPanelCount() == 0);
    CHECK_FALSE(c.mode.picker);
    CHECK(c.ignored_unpaired == 3);
    CHECK(c.link == cd::Link::Pairing);
    CHECK_FALSE(live::IsConnected());
    CHECK_FALSE(live::IsPaired());

    cd::HandlePairing(c, MsgPairChallenge(pairing::RandomHex(16), pairing::RandomHex(16), pairing::RandomHex(32)), 0);
    CHECK(c.link == cd::Link::WaitingForCode);
    CHECK(c.pair_error.find("could not prove") != std::string::npos);
    CHECK_FALSE(c.handshake.Active());
    c.link = cd::Link::Off;
    live::Disconnect();
}

TEST_CASE("interpreter stops at a maximum node depth instead of recursing without bound") {
    imforge::test::HeadlessImGui ui;
    Interpreter interp;
    Panel panel;
    Error err;
    REQUIRE(PanelFromJson(ParseOrFail(PanelJson("Deep", NestedNodes(120))), panel, &err));
    PanelInstance inst;
    InterpOptions opts;
    ui.BeginFrame();
    ImGui::Begin("deep");
    interp.DrawPanel(panel, inst, {}, opts);
    ImGui::End();
    ui.EndFrame();
    CHECK(HasDiag(interp, "nested deeper"));
    CHECK(interp.draw_depth == 0);
}

TEST_CASE("builder supplied text is never used as a printf format") {
    CHECK(std::string(interp_widgets::SafeFormat("%s", true, "%.3f")) == "%.3f");
    CHECK(std::string(interp_widgets::SafeFormat("%n", true, "%.3f")) == "%.3f");
    CHECK(std::string(interp_widgets::SafeFormat("%d", true, "%.3f")) == "%.3f");
    CHECK(std::string(interp_widgets::SafeFormat("%f%f", true, "%.3f")) == "%.3f");
    CHECK(std::string(interp_widgets::SafeFormat("%*f", true, "%.3f")) == "%.3f");
    CHECK(std::string(interp_widgets::SafeFormat("%lf", true, "%.3f")) == "%.3f");
    CHECK(std::string(interp_widgets::SafeFormat("%s", false, "%d")) == "%d");
    CHECK(std::string(interp_widgets::SafeFormat("%.2f units", true, "%.3f")) == "%.2f units");

    imforge::test::HeadlessImGui ui;
    Interpreter interp;
    Panel panel;
    Error err;
    const char* root = R"({"id":"root","type":"VStack","children":[
        {"id":"t1","type":"Text","props":{"text":"%s%s%s%n%x"}},
        {"id":"t2","type":"Text","props":{"text":"%s%n","wrapped":true}},
        {"id":"b1","type":"Button","props":{"label":"%s%n"},"tooltip":"%s%s%n"},
        {"id":"s1","type":"SliderFloat","props":{"label":"v","format":"%s%s%n"}},
        {"id":"d1","type":"DragInt","props":{"label":"i","format":"%n"}}]})";
    REQUIRE(PanelFromJson(ParseOrFail(PanelJson("Fmt", root)), panel, &err));
    PanelInstance inst;
    InterpOptions opts;
    for (int i = 0; i < 3; ++i) {
        ui.BeginFrame();
        ImGui::Begin("fmt");
        interp.DrawPanel(panel, inst, {}, opts);
        ImGui::End();
        ui.EndFrame();
    }
    CHECK(interp.draw_depth == 0);
}

namespace {

namespace cd = live::client_detail;
using imforge::test::MakeTinyFont;
using imforge::test::Set16;

bool FontOk(const std::vector<unsigned char>& v, std::string* why = nullptr) {
    return fontcheck::ValidateFontData(v.data(), v.size(), why);
}

std::string HashOf(const std::vector<unsigned char>& v) { return cd::FontHash(v.data(), v.size()); }

void SendFont(cd::FontState& s, const std::vector<unsigned char>& v, cd::FontLogs& logs, size_t chunk = 100) {
    std::string h = HashOf(v);
    for (size_t off = 0; off < v.size(); off += chunk) {
        size_t n = v.size() - off < chunk ? v.size() - off : chunk;
        cd::OnFontData(s, MsgFontData(h, v.size(), off, v.data() + off, n), 0, logs);
    }
}

bool AnyLog(const cd::FontLogs& logs, const char* needle) {
    for (const auto& l : logs)
        if (l.second.find(needle) != std::string::npos) return true;
    return false;
}

FontSpecMsg Spec(const char* name, float size, const std::vector<unsigned char>& file) {
    FontSpecMsg f;
    f.name = name;
    f.size = size;
    f.ranges = "default";
    f.file = HashOf(file);
    return f;
}

int g_rebuilds = 0;
void CountRebuild() {
    ++g_rebuilds;
    unsigned char* px = nullptr;
    int w = 0, h = 0;
    ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&px, &w, &h);
}

std::string g_last_log;
void KeepLog(int, const char* m) { g_last_log = m; }

}  // namespace

TEST_CASE("font bytes are checked before stb_truetype sees them") {
    imforge::test::TinyFont tf = MakeTinyFont();
    std::string why;
    REQUIRE_MESSAGE(FontOk(tf.bytes, &why), why);

    for (size_t n : {size_t(0), size_t(4), size_t(11), size_t(12 + 16 * 3), tf.cmap + 10, tf.bytes.size() - 20})
        CHECK_FALSE(FontOk(std::vector<unsigned char>(tf.bytes.begin(), tf.bytes.begin() + (long)n)));

    std::vector<unsigned char> v = tf.bytes;
    std::memcpy(v.data(), "ttcf", 4);
    CHECK_FALSE(FontOk(v, &why));
    CHECK(why.find("collections") != std::string::npos);
    v = tf.bytes;
    std::memcpy(v.data(), "OTTO", 4);
    CHECK_FALSE(FontOk(v, &why));
    CHECK(why.find("CFF") != std::string::npos);

    v = tf.bytes;
    Set16(v, tf.loca + 6, 0x7FFF);
    CHECK_FALSE(FontOk(v, &why));
    CHECK(why.find("loca") != std::string::npos);

    v = tf.bytes;
    Set16(v, tf.composite_child, 2);
    CHECK_FALSE(FontOk(v, &why));
    CHECK(why.find("itself") != std::string::npos);
    Set16(v, tf.composite_child, 9);
    CHECK_FALSE(FontOk(v));

    v = tf.bytes;
    Set16(v, tf.cmap + 12 + 14 + 4 + 2 + 4, 0x0040);
    CHECK_FALSE(FontOk(v, &why));
    CHECK(why.find("cmap") != std::string::npos);

    v = tf.bytes;
    Set16(v, tf.hhea + 34, 7);
    CHECK_FALSE(FontOk(v));

    v = tf.bytes;
    Set16(v, tf.dir + 8 + 2, 0xFFFF);
    CHECK_FALSE(FontOk(v));
}

TEST_CASE("a lone off-curve point is made on-curve instead of letting stb read past its buffer") {
    imforge::test::TinyFont tf = MakeTinyFont();
    std::vector<unsigned char> v = tf.bytes;
    Set16(v, tf.glyf + 10, 0);
    v[tf.glyf + 14] = 0x00;
    std::vector<size_t> fixes;
    std::string why;
    REQUIRE_MESSAGE(fontcheck::ValidateFontData(v.data(), v.size(), &why, nullptr, &fixes), why);
    REQUIRE(fixes.size() == 1);
    CHECK(fixes[0] == tf.glyf + 14);
    REQUIRE(fontcheck::PrepareFontData(v));
    CHECK(v[tf.glyf + 14] == 0x01);
    CHECK(FontOk(tf.bytes));
    fixes.clear();
    fontcheck::ValidateFontData(tf.bytes.data(), tf.bytes.size(), nullptr, nullptr, &fixes);
    CHECK(fixes.empty());
}

TEST_CASE("fonts that pass the check build in a real atlas, even after random corruption") {
    imforge::test::TinyFont tf = MakeTinyFont();
    std::mt19937 rng(1234);
    int accepted = 0;
    for (int iter = 0; iter < 3000; ++iter) {
        std::vector<unsigned char> v = tf.bytes;
        int flips = 1 + (int)(rng() % 4);
        for (int k = 0; k < flips; ++k) v[rng() % v.size()] = (unsigned char)rng();
        ImFontAtlas atlas;
        if (!fontcheck::PrepareFontData(v) || !fontcheck::MapsAnyGlyph(v.data(), v.size(), atlas.GetGlyphRangesDefault())) continue;
        ++accepted;
        ImFontConfig cfg;
        cfg.FontDataOwnedByAtlas = false;
        atlas.AddFontFromMemoryTTF(v.data(), (int)v.size(), 16.0f, &cfg);
        atlas.Build();
    }
    CHECK(accepted > 0);
}

TEST_CASE("font chunks are joined in order, hashed and capped") {
    cd::FontState s;
    cd::FontLogs logs;
    std::vector<unsigned char> font = MakeTinyFont().bytes;
    SendFont(s, font, logs);
    CHECK(s.files.count(HashOf(font)) == 1);
    CHECK(s.file_bytes == font.size());
    CHECK(logs.empty());

    std::vector<unsigned char> other = font;
    other.push_back(1);
    std::string h = HashOf(other);
    cd::OnFontData(s, MsgFontData(h, other.size(), 0, other.data(), 50), 0, logs);
    cd::OnFontData(s, MsgFontData(h, other.size(), 100, other.data() + 100, 50), 0, logs);
    CHECK(AnyLog(logs, "out of order"));
    CHECK(s.files.count(h) == 0);
    CHECK(s.part.empty());

    logs.clear();
    cd::OnFontData(s, MsgFontData(std::string(64, 'e'), other.size(), 0, other.data(), other.size()), 0, logs);
    CHECK(AnyLog(logs, "hash"));
    CHECK(s.files.size() == 1);

    logs.clear();
    std::vector<unsigned char> junk(400, 0x41);
    SendFont(s, junk, logs);
    CHECK(AnyLog(logs, "rejected"));
    CHECK(s.files.size() == 1);

    logs.clear();
    for (size_t i = 0; i < kProtocolMaxFontFiles + 3; ++i) {
        std::vector<unsigned char> f = font;
        for (size_t k = 0; k <= i; ++k) f.push_back(0);
        SendFont(s, f, logs);
    }
    CHECK(s.files.size() == kProtocolMaxFontFiles);
    CHECK(AnyLog(logs, "maximum number of font files"));
}

TEST_CASE("PreNewFrame loads builder fonts, keeps old ImFont pointers and lets RegisterFont win") {
    imforge::test::HeadlessImGui ui;
    cd::Client& c = cd::Get();
    c.font_state = cd::FontState();
    g_rebuilds = 0;
    std::vector<unsigned char> font = MakeTinyFont().bytes;
    cd::FontLogs logs;
    SendFont(c.font_state, font, logs);
    FontSpecMsg body = Spec("Body", 18.0f, font);
    FontSpecMsg icons = Spec("Icons", 16.0f, font);
    icons.icons = body.file;
    icons.icon_lo = 0x41;
    icons.icon_hi = 0x42;
    cd::OnFontList(c.font_state, MsgFonts({body, icons}), 0, logs);
    REQUIRE(logs.empty());
    ImFontAtlas* atlas = ImGui::GetIO().Fonts;
    const int before = atlas->Fonts.Size;

    live::SetLogCallback(&KeepLog);
    live::PreNewFrame();
    CHECK(g_last_log.find("SetFontTextureRebuild") != std::string::npos);
    CHECK(atlas->Fonts.Size == before);

    live::SetFontTextureRebuild(&CountRebuild);
    live::PreNewFrame();
    CHECK(g_rebuilds == 1);
    REQUIRE(atlas->Fonts.Size == before + 2);
    CHECK(atlas->IsBuilt());
    ImFont* f = c.interp.hooks.font("Body");
    REQUIRE(f != nullptr);
    CHECK(f == atlas->Fonts[before]);
    CHECK(c.interp.hooks.font("Icons") == atlas->Fonts[before + 1]);
    CHECK(f->FindGlyphNoFallback((ImWchar)'A') != nullptr);
    ui.Frame([&] {
        ImGui::Begin("fonts");
#if IMGUI_VERSION_NUM >= 19200
        ImGui::PushFont(f, 0.0f);
#else
        ImGui::PushFont(f);
#endif
        ImGui::TextUnformatted("AB");
        ImGui::PopFont();
        ImGui::End();
    });

    cd::OnFontList(c.font_state, MsgFonts({body, icons}), 0, logs);
    live::PreNewFrame();
    CHECK(g_rebuilds == 1);
    CHECK(atlas->Fonts.Size == before + 2);

    body.size = 22.0f;
    cd::OnFontList(c.font_state, MsgFonts({body}), 0, logs);
    live::PreNewFrame();
    CHECK(g_rebuilds == 2);
    CHECK(atlas->Fonts.Size == before + 3);
    CHECK(atlas->Fonts[before] == f);
    CHECK(c.interp.hooks.font("Body") == atlas->Fonts[before + 2]);
    CHECK(c.font_state.specs.size() == 1);

    ImFont* mine = atlas->Fonts[0];
    live::RegisterFont("Body", mine);
    CHECK(c.interp.hooks.font("Body") == mine);
    c.fonts.clear();

    CHECK(cd::FontFileHashes(c.font_state) == std::vector<std::string>{body.file});
    live::SetLogCallback(nullptr);
    live::SetFontTextureRebuild(nullptr);
    c.font_state = cd::FontState();
}

TEST_CASE("an app that never calls PreNewFrame gets one hint") {
    cd::FontState s;
    cd::FontLogs logs;
    std::vector<unsigned char> font = MakeTinyFont().bytes;
    SendFont(s, font, logs);
    cd::OnFontList(s, MsgFonts({Spec("Body", 16.0f, font)}), 1000, logs);
    cd::CheckFontIntegration(s, 1500, logs);
    CHECK(logs.empty());
    cd::CheckFontIntegration(s, 1000 + cd::kPreNewFrameGraceMs + 1, logs);
    CHECK(AnyLog(logs, "PreNewFrame"));
    logs.clear();
    cd::CheckFontIntegration(s, 99999, logs);
    CHECK(logs.empty());
}

TEST_CASE("icon names from the builder feed {icon:...} text") {
    cd::Client& c = cd::Get();
    c.font_state = cd::FontState();
    CHECK(c.interp.hooks.icon("fa-gear") == nullptr);
    cd::Dispatch(c, MsgIcons({{"fa-gear", 0xf013}, {"fa-face-smile", 0x1f600}}));
    REQUIRE(c.interp.hooks.icon("fa-gear") != nullptr);
    CHECK(std::string(c.interp.hooks.icon("fa-gear")) == "\xef\x80\x93");
    CHECK(std::string(c.interp.hooks.icon("fa-face-smile")) == "\xf0\x9f\x98\x80");
    Json bad = MsgIcons({{"fa-gear", 0xf013}});
    Json map = Json::Object();
    map.Set("Fa Gear", 0xf013);
    bad.Set("icons", map);
    cd::Dispatch(c, bad);
    CHECK(c.interp.hooks.icon("fa-gear") != nullptr);
    c.font_state = cd::FontState();
}

TEST_CASE("composite glyphs may not nest deeper than stb can safely recurse") {
    // glyph 1 is the box, glyph k a composite of glyph k-1. In index order every child is already
    // checked when its parent comes up, so the depth has to carry over
    auto make = [](int levels) {
        imforge::test::TinyFont tf = MakeTinyFont();
        std::vector<unsigned char> v = tf.bytes;
        std::vector<unsigned char> glyf(v.begin() + (long)tf.glyf, v.begin() + (long)(tf.glyf + 34));
        std::vector<uint32_t> loca = {0, 0, 34};
        for (int k = 2; k <= levels + 1; ++k) {
            unsigned char comp[18] = {0xFF, 0xFF, 0, 0, 0, 0, 0x02, 0x58, 0x02, 0xBC, 0x00, 0x03, 0, 0, 0, 100, 0, 0};
            comp[13] = (unsigned char)(k - 1);
            glyf.insert(glyf.end(), comp, comp + 18);
            loca.push_back((uint32_t)glyf.size());
        }
        return std::make_pair(glyf, loca);
    };
    for (int levels : {3, 12}) {
        std::pair<std::vector<unsigned char>, std::vector<uint32_t>> g = make(levels);
        uint32_t glyphs = (uint32_t)g.second.size() - 1;
        std::vector<unsigned char> head(54, 0), hhea(36, 0), maxp(6, 0), hmtx, loca, cmap;
        head[1] = 1;
        head[12] = 0x5F; head[13] = 0x0F; head[14] = 0x3C; head[15] = 0xF5;
        head[18] = 0x03; head[19] = 0xE8;
        head[51] = 1;
        hhea[1] = 1;
        hhea[4] = 0x03; hhea[5] = 0x20;
        hhea[6] = 0xFF; hhea[7] = 0x38;
        hhea[34] = (unsigned char)(glyphs >> 8); hhea[35] = (unsigned char)glyphs;
        maxp[2] = 0x50;
        maxp[4] = (unsigned char)(glyphs >> 8); maxp[5] = (unsigned char)glyphs;
        for (uint32_t i = 0; i < glyphs; ++i) { imforge::test::Put16(hmtx, 600); imforge::test::Put16(hmtx, 0); }
        for (uint32_t o : g.second) imforge::test::Put32(loca, o);
        const uint32_t cmap_words[] = {0, 1, 3, 1, 0, 12, 4, 32, 0, 4, 4, 1, 0, 0x42, 0xFFFF, 0, 0x41, 0xFFFF,
                                       (1u - 0x41u) & 0xFFFF, 1, 0, 0};
        for (size_t i = 0; i < sizeof(cmap_words) / sizeof(cmap_words[0]); ++i) imforge::test::Put16(cmap, cmap_words[i]);
        struct T { const char* tag; std::vector<unsigned char>* d; } tables[] = {
            {"cmap", &cmap}, {"glyf", &g.first}, {"head", &head}, {"hhea", &hhea}, {"hmtx", &hmtx}, {"loca", &loca}, {"maxp", &maxp}};
        std::vector<unsigned char> font;
        imforge::test::Put32(font, 0x00010000);
        imforge::test::Put16(font, 7);
        imforge::test::Put16(font, 0);
        imforge::test::Put16(font, 0);
        imforge::test::Put16(font, 0);
        size_t off = 12 + 16 * 7;
        for (const T& t : tables) {
            font.insert(font.end(), t.tag, t.tag + 4);
            imforge::test::Put32(font, 0);
            imforge::test::Put32(font, (uint32_t)off);
            imforge::test::Put32(font, (uint32_t)t.d->size());
            off += (t.d->size() + 3) & ~size_t(3);
        }
        for (const T& t : tables) {
            font.insert(font.end(), t.d->begin(), t.d->end());
            while (font.size() % 4) font.push_back(0);
        }
        std::string why;
        bool ok = FontOk(font, &why);
        if (levels == 3) CHECK_MESSAGE(ok, why);
        else CHECK(why.find("nest too deep") != std::string::npos);
    }
}

TEST_CASE("a format 6 cmap may not reach past the BMP, where the check never looks") {
    imforge::test::TinyFont tf = MakeTinyFont();
    std::vector<unsigned char> v = tf.bytes;
    Set16(v, tf.cmap + 12, 6);
    Set16(v, tf.cmap + 12 + 6, 0x41);
    Set16(v, tf.cmap + 12 + 8, 2);
    std::string why;
    CHECK_MESSAGE(FontOk(v, &why), why);
    Set16(v, tf.cmap + 12 + 6, 0xFFFF);
    Set16(v, tf.cmap + 12 + 8, 0xFFFF);
    CHECK_FALSE(FontOk(v, &why));
    CHECK(why.find("cmap") != std::string::npos);
}

TEST_CASE("composite glyphs are limited by what they expand to, not just by depth") {
    using imforge::test::BoxGlyph;
    using imforge::test::BuildFont;
    using imforge::test::CompositeOf;
    auto font = [&](size_t outer) {
        std::vector<unsigned char> glyf = BoxGlyph();
        std::vector<uint32_t> loca = {0, 0, (uint32_t)glyf.size()};
        for (const std::vector<unsigned char>& g : {CompositeOf(1, 200), CompositeOf(2, outer)}) {
            glyf.insert(glyf.end(), g.begin(), g.end());
            loca.push_back((uint32_t)glyf.size());
        }
        return BuildFont(glyf, loca);
    };
    std::string why;
    CHECK_MESSAGE(FontOk(font(20), &why), why);
    CHECK_FALSE(FontOk(font(200), &why));
    CHECK(why.find("too complex") != std::string::npos);
}

TEST_CASE("a font full of busy composites is refused as a whole") {
    using imforge::test::BoxGlyph;
    using imforge::test::BuildFont;
    using imforge::test::CompositeOf;
    auto font = [&](size_t copies) {
        std::vector<unsigned char> glyf = BoxGlyph();
        std::vector<uint32_t> loca = {0, 0, (uint32_t)glyf.size()};
        std::vector<unsigned char> base = CompositeOf(1, 200);
        glyf.insert(glyf.end(), base.begin(), base.end());
        loca.push_back((uint32_t)glyf.size());
        std::vector<unsigned char> busy = CompositeOf(2, 27);
        for (size_t i = 0; i < copies; ++i) {
            glyf.insert(glyf.end(), busy.begin(), busy.end());
            loca.push_back((uint32_t)glyf.size());
        }
        return BuildFont(glyf, loca);
    };
    std::string why;
    CHECK_MESSAGE(FontOk(font(300), &why), why);
    CHECK_FALSE(FontOk(font(1100), &why));
    CHECK(why.find("outlines are too complex") != std::string::npos);
}

#if IMGUI_VERSION_NUM < 19200
namespace {
std::string g_log_text;
void AppendLog(int, const char* m) {
    g_log_text += m;
    g_log_text += '\n';
}
}  // namespace

TEST_CASE("fonts that would blow up the atlas texture are skipped") {
    imforge::test::HeadlessImGui ui;
    cd::Client& c = cd::Get();
    c.font_state = cd::FontState();
    g_log_text.clear();
    imforge::test::TinyFont tf = MakeTinyFont();
    std::vector<unsigned char> tall = tf.bytes;
    Set16(tall, tf.glyf + 6, 4000);
    Set16(tall, tf.glyf + 8, 16000);
    cd::FontLogs logs;
    SendFont(c.font_state, tall, logs);
    REQUIRE(logs.empty());
    std::vector<FontSpecMsg> specs;
    for (const char* name : {"H1", "H2", "H3", "H4"}) specs.push_back(Spec(name, 256.0f, tall));
    cd::OnFontList(c.font_state, MsgFonts(specs), 0, logs);
    live::SetLogCallback(&AppendLog);
    live::SetFontTextureRebuild(&CountRebuild);
    ImFontAtlas* atlas = ImGui::GetIO().Fonts;
    const int before = atlas->Fonts.Size;
    live::PreNewFrame();
    CHECK(atlas->Fonts.Size == before + 3);
    CHECK(cd::FindLiveFont(c.font_state, "H3") != nullptr);
    CHECK(cd::FindLiveFont(c.font_state, "H4") == nullptr);
    CHECK(g_log_text.find("'H4' would make the font atlas too large") != std::string::npos);
    live::SetLogCallback(nullptr);
    live::SetFontTextureRebuild(nullptr);
    c.font_state = cd::FontState();
}
#endif
