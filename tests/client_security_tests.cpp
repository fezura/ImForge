#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#ifndef IMFORGE_LIVE
#define IMFORGE_LIVE
#endif
#include "imforge/imforge_client.h"
#include "imgui_headless.h"

#include <cstring>
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
