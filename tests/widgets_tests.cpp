#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <cstring>
#include <set>
#include <string>
#include <vector>

#include "imgui.h"
#include "imforge/imforge_widgets.h"

using namespace imforge;

#include "widgets_tests_tables.h"

TEST_CASE("stock catalog matches the expected table") {
    size_t n = 0;
    const WidgetDef* stock = StockWidgets(n);
    REQUIRE(stock != nullptr);
    const std::vector<EW>& exp = Expected();
    CHECK(n == exp.size());
    for (const EW& e : exp) {
        INFO("widget ", std::string(e.type));
        const WidgetDef* w = FindWidget(e.type);
        REQUIRE(w != nullptr);
        CHECK(CStrEq(w->type, e.type));
        CHECK(w->container == e.container);
        CHECK(w->sizable == e.sizable);
        CHECK(CStrEq(w->child_types, e.child_types));
        CHECK(w->min_version == e.min_version);
        REQUIRE(w->prop_count == static_cast<int>(e.props.size()));
        for (size_t i = 0; i < e.props.size(); ++i) {
            INFO("prop ", std::string(e.props[i].name));
            const PropDef& p = w->props[i];
            CHECK(CStrEq(p.name, e.props[i].name));
            CHECK(p.type == e.props[i].type);
            CHECK(CStrEq(p.enum_values, e.props[i].ev));
            REQUIRE(p.def_json != nullptr);
            CHECK(J(p.def_json) == J(e.props[i].def));
            CHECK(FindProp(*w, e.props[i].name) == &p);
        }
        REQUIRE(w->bind_count == static_cast<int>(e.binds.size()));
        for (size_t i = 0; i < e.binds.size(); ++i) {
            CHECK(CStrEq(w->binds[i].name, e.binds[i].name));
            CHECK(CStrEq(w->binds[i].types, e.binds[i].types));
            CHECK(w->binds[i].required == e.binds[i].required);
            CHECK(FindBind(*w, e.binds[i].name) == &w->binds[i]);
        }
        REQUIRE(w->event_count == static_cast<int>(e.events.size()));
        for (size_t i = 0; i < e.events.size(); ++i) {
            CHECK(CStrEq(w->events[i], e.events[i]));
            CHECK(WidgetHasEvent(*w, e.events[i]));
        }
        CHECK_FALSE(WidgetHasEvent(*w, "onNothing"));
    }
    CHECK(FindWidget("Nope") == nullptr);
    CHECK(FindWidget(nullptr) == nullptr);
}

TEST_CASE("stock catalog internal consistency") {
    size_t n = 0;
    const WidgetDef* stock = StockWidgets(n);
    std::set<std::string> types;
    for (size_t i = 0; i < n; ++i) {
        const WidgetDef& w = stock[i];
        INFO("widget ", std::string(w.type));
        CHECK(types.insert(w.type).second);
        CHECK(w.category != nullptr);
        REQUIRE(w.icon != nullptr);
        CHECK(std::strncmp(w.icon, "fa-", 3) == 0);
        CHECK(w.min_version >= 18500);
        const PropDef* size = FindProp(w, "size");
        CHECK((size != nullptr) == w.sizable);
        if (size) CHECK(size->type == PropType::Size);
        for (int k = 0; k < w.prop_count; ++k) {
            const PropDef& p = w.props[k];
            INFO("prop ", std::string(p.name));
            Json def;
            CHECK(ParseJson(p.def_json, std::strlen(p.def_json), def, nullptr));
            CHECK(p.min_version >= w.min_version);
            CHECK(p.min <= p.max);
            if (p.type == PropType::Flags) {
                size_t fc = 0;
                CHECK(FlagsFor(p.enum_values, fc) != nullptr);
                CHECK(PropDefaultJson(p).IsArray());
            } else if (p.type == PropType::Enum) {
                std::vector<std::string> vals = EnumValues(p);
                CHECK(vals.size() >= 2);
                CHECK(IsValidEnumValue(p, PropDefaultJson(p).AsString().c_str()));
            } else {
                CHECK(p.enum_values == nullptr);
                CHECK(EnumValues(p).empty());
            }
            if (p.type == PropType::Bool && !def.IsNull()) CHECK(def.IsBool());
            if ((p.type == PropType::Int || p.type == PropType::Float) && !def.IsNull())
                CHECK(def.IsNumber());
            if (p.type == PropType::Size) CHECK(def.IsObject());
        }
        for (int k = 0; k < w.event_count; ++k) CHECK(std::strncmp(w.events[k], "on", 2) == 0);
        for (int k = 0; k < w.bind_count; ++k) CHECK(std::strlen(w.binds[k].types) > 0);
        if (!w.container) CHECK(w.child_types == nullptr);
    }
    CHECK(FindProp(*FindWidget("Child"), "childFlags")->min_version == 19000);
    CHECK(FindWidget("SeparatorText")->min_version == 18950);
}

TEST_CASE("WidgetAcceptsChild") {
    const WidgetDef& tabbar = *FindWidget("TabBar");
    const WidgetDef& vstack = *FindWidget("VStack");
    const WidgetDef& menu = *FindWidget("Menu");
    const WidgetDef& window = *FindWidget("Window");
    const WidgetDef& button = *FindWidget("Button");
    CHECK(WidgetAcceptsChild(tabbar, "Tab"));
    CHECK_FALSE(WidgetAcceptsChild(tabbar, "Button"));
    CHECK_FALSE(WidgetAcceptsChild(vstack, "Tab"));
    CHECK(WidgetAcceptsChild(vstack, "Button"));
    CHECK(WidgetAcceptsChild(vstack, "VStack"));
    CHECK(WidgetAcceptsChild(menu, "MenuItem"));
    CHECK(WidgetAcceptsChild(menu, "Menu"));
    CHECK(WidgetAcceptsChild(menu, "Separator"));
    CHECK_FALSE(WidgetAcceptsChild(menu, "Button"));
    CHECK(WidgetAcceptsChild(window, "MenuBar"));
    CHECK_FALSE(WidgetAcceptsChild(vstack, "MenuBar"));
    CHECK_FALSE(WidgetAcceptsChild(vstack, "Window"));
    CHECK_FALSE(WidgetAcceptsChild(button, "Text"));
    CHECK_FALSE(WidgetAcceptsChild(*FindWidget("Slot"), "Text"));
    CHECK_FALSE(WidgetAcceptsChild(*FindWidget("Code"), "Text"));
    CHECK_FALSE(WidgetAcceptsChild(vstack, nullptr));
}

TEST_CASE("PropDefaultJson and prop type names") {
    PropDef p{"x", PropType::Vec2, R"({"x":1,"y":2})", nullptr, 0, 0, 18500, nullptr};
    Json j = PropDefaultJson(p);
    CHECK(j.IsObject());
    CHECK(j["x"].AsDouble() == 1.0);
    CHECK(j["y"].AsDouble() == 2.0);
    PropDef bad{"x", PropType::Int, "{oops", nullptr, 0, 0, 18500, nullptr};
    CHECK(PropDefaultJson(bad).IsNull());
    PropDef none{"x", PropType::Int, nullptr, nullptr, 0, 0, 18500, nullptr};
    CHECK(PropDefaultJson(none).IsNull());
    PropType t;
    CHECK(ParsePropType("string", t));
    CHECK(t == PropType::String);
    CHECK(ParsePropType("Size", t));
    CHECK(t == PropType::Size);
    CHECK(ParsePropType("list<string>", t));
    CHECK(t == PropType::StringList);
    CHECK(ParsePropType("stringList", t));
    CHECK(t == PropType::StringList);
    CHECK(ParsePropType("list<SizeSpec>", t));
    CHECK(t == PropType::SizeList);
    CHECK_FALSE(ParsePropType("matrix", t));
    CHECK_FALSE(ParsePropType(nullptr, t));
    for (int i = 0; i <= static_cast<int>(PropType::Code); ++i) {
        PropType back;
        REQUIRE(ParsePropType(PropTypeName(static_cast<PropType>(i)), back));
        CHECK(back == static_cast<PropType>(i));
    }
}

TEST_CASE("FlagsFor tables match the running ImGui") {
    size_t enum_count = 0;
    const char* const* enums = FlagEnumNames(enum_count);
    CHECK(enum_count == 14);
    for (size_t e = 0; e < enum_count; ++e) {
        size_t n = 0;
        CHECK(FlagsFor(enums[e], n) != nullptr);
        CHECK(n > 0);
    }
    for (const RealTable& rt : kRealTables) {
        INFO("enum ", std::string(rt.enum_name));
        size_t n = 0;
        const FlagDef* f = FlagsFor(rt.enum_name, n);
        REQUIRE(f != nullptr);
        std::set<std::string> names;
        for (size_t i = 0; i < n; ++i) {
            INFO("flag ", std::string(f[i].name));
            CHECK(names.insert(f[i].name).second);
            CHECK(f[i].min_version >= 18500);
            CHECK(FindFlag(rt.enum_name, f[i].name) == &f[i]);
            if (f[i].min_version > IMGUI_VERSION_NUM) continue;
            const RealFlag* real = nullptr;
            for (size_t k = 0; k < rt.count; ++k)
                if (std::strcmp(rt.flags[k].name, f[i].name) == 0) real = &rt.flags[k];
            REQUIRE(real != nullptr);
            // entries older than 1.91.9 hold 1.91.9 values (some enums were renumbered before 1.91.8),
            // anything newer follows the running ImGui
#if IMGUI_VERSION_NUM >= 19180
            CHECK(real->value == f[i].value);
#endif
        }
        for (size_t k = 0; k < rt.count; ++k) {
            INFO("real flag ", std::string(rt.flags[k].name));
            CHECK(names.count(rt.flags[k].name) == 1);
        }
    }
    size_t n = 0;
    CHECK(FlagsFor("ImGuiWindowFlags", n) == FlagsFor("WindowFlags", n));
    CHECK(FlagsFor("Bogus", n) == nullptr);
    CHECK(n == 0);
    CHECK(FlagsFor(nullptr, n) == nullptr);
    CHECK(FindFlag("WindowFlags", "Nope") == nullptr);
    CHECK(FindFlag("WindowFlags", "NoTitleBar")->value == 1);
    size_t cc = 0;
    const FlagDef* child = FlagsFor("ChildFlags", cc);
    for (size_t i = 0; i < cc; ++i) CHECK(child[i].min_version >= 19000);
    CHECK(FindFlag("InputTextFlags", "ElideLeft")->min_version == 19160);
    CHECK(FindFlag("TableFlags", "HighlightHoveredColumn")->min_version == 19000);
    CHECK(FindFlag("TableFlags", "SizingStretchSame")->value == (4 << 13));
    REQUIRE(FindFlagRename("ImGuiTreeNodeFlags", "NavLeftJumpsBackHere") != nullptr);
    CHECK(FindFlagRename("TreeNodeFlags", "NavLeftJumpsToParent")->since == 19200);
    REQUIRE(FindFlagRename("TabBarFlags", "FittingPolicyShrink") != nullptr);
    CHECK(CStrEq(FindFlagRename("TabBarFlags", "FittingPolicyShrink")->old_name, "FittingPolicyResizeDown"));
    CHECK(FindFlagRename("TabBarFlags", "FittingPolicyScroll") == nullptr);
    CHECK(FindFlagRename(nullptr, "FittingPolicyShrink") == nullptr);
#if IMGUI_VERSION_NUM >= 19200
    CHECK(FindFlag("TreeNodeFlags", "NavLeftJumpsBackHere") == nullptr);
    REQUIRE(FindFlag("TreeNodeFlags", "DrawLinesFull") != nullptr);
    CHECK(FindFlag("TreeNodeFlags", "DrawLinesFull")->min_version == 19200);
#else
    CHECK(FindFlag("TreeNodeFlags", "NavLeftJumpsToParent") == nullptr);
#endif
#if IMGUI_VERSION_NUM >= 19220
    CHECK(FindFlag("TabBarFlags", "FittingPolicyResizeDown") == nullptr);
    REQUIRE(FindFlag("TabBarFlags", "FittingPolicyMixed") != nullptr);
    CHECK(FindFlag("TabBarFlags", "FittingPolicyMixed")->min_version == 19220);
#else
    CHECK(FindFlag("TabBarFlags", "FittingPolicyShrink") == nullptr);
#endif
#if IMGUI_VERSION_NUM >= 19290
    CHECK(FindFlag("ColorEditFlags", "PickerNoRotate")->min_version == 19290);
#else
    CHECK(FindFlag("ColorEditFlags", "PickerNoRotate") == nullptr);
#endif
}

TEST_CASE("ImGuiCond and ImGuiDir names") {
    size_t n = 0;
    const char* const* cond = ImGuiCondNames(n);
    REQUIRE(n == 4);
    CHECK(CStrEq(cond[0], "Always"));
    CHECK(CStrEq(cond[3], "Appearing"));
    const char* const* dir = ImGuiDirNames(n);
    REQUIRE(n == 4);
    CHECK(CStrEq(dir[0], "Left"));
    CHECK(CStrEq(dir[3], "Down"));
    CHECK(FindFlag("Dir", "Down")->value == static_cast<int>(ImGuiDir_Down));
    CHECK(FindFlag("Cond", "Appearing")->value == static_cast<int>(ImGuiCond_Appearing));
}

TEST_CASE("ImGuiColNames matches ImGuiCol_") {
    size_t n = 0;
    const char* const* names = ImGuiColNames(n);
    REQUIRE(names != nullptr);
    size_t dn = 0;
    const char* const* dock = ImGuiColNamesDocking(dn);
    CHECK(dn == 2);
    CHECK(CStrEq(dock[0], "DockingPreview"));
#ifdef IMGUI_HAS_DOCK
    const size_t kExpectedCount = static_cast<size_t>(ImGuiCol_COUNT) - dn;
#else
    const size_t kExpectedCount = static_cast<size_t>(ImGuiCol_COUNT);
#endif
#if IMGUI_VERSION_NUM >= 19190
    CHECK(n == kExpectedCount);
#else
    CHECK(n >= 49);
    (void)kExpectedCount;
#endif
    std::set<std::string> seen;
    const size_t real_n = sizeof(kRealCols) / sizeof(kRealCols[0]);
    for (size_t i = 0; i < n; ++i) {
        INFO("col ", std::string(names[i]));
        CHECK(seen.insert(names[i]).second);
        CHECK(ImGuiColIndex(names[i]) == static_cast<int>(i));
        int mv = ImGuiColMinVersion(names[i]);
        CHECK(mv >= 18500);
        if (mv > IMGUI_VERSION_NUM) continue;
        const RealCol* real = FindReal(kRealCols, real_n, names[i]);
        REQUIRE(real != nullptr);
#if IMGUI_VERSION_NUM >= 19190
        const int shifted = static_cast<int>(i + dn);
        CHECK((real->index == static_cast<int>(i) || real->index == shifted));
#endif
    }
    for (size_t k = 0; k < real_n; ++k) CHECK(seen.count(kRealCols[k].name) == 1);
    CHECK(ImGuiColMinVersion("TabSelected") == 19090);
    CHECK(ImGuiColMinVersion("TextLink") == 19100);
    CHECK(ImGuiColMinVersion("NavCursor") == 19140);
    CHECK(ImGuiColMinVersion("Text") == 18500);
    CHECK(ImGuiColMinVersion("DockingPreview") == 18500);
#if IMGUI_VERSION_NUM >= 19280
    CHECK(ImGuiColMinVersion("TreeLines") == 19200);
    CHECK(ImGuiColMinVersion("CheckboxSelectedBg") == 19280);
#else
    CHECK(ImGuiColMinVersion("CheckboxSelectedBg") == 0);
#endif
    CHECK(ImGuiColMinVersion("Bogus") == 0);
    CHECK(CStrEq(ImGuiColCanonicalName("TabActive"), "TabSelected"));
    CHECK(CStrEq(ImGuiColCanonicalName("TabUnfocusedActive"), "TabDimmedSelected"));
    CHECK(CStrEq(ImGuiColCanonicalName("NavHighlight"), "NavCursor"));
    CHECK(CStrEq(ImGuiColCanonicalName("Button"), "Button"));
    CHECK(ImGuiColIndex("TabActive") == ImGuiColIndex("TabSelected"));
    CHECK(ImGuiColIndex("Bogus") == -1);
    CHECK(ImGuiColIndex(nullptr) == -1);
}

TEST_CASE("ImGuiStyleVarNames matches ImGuiStyleVar_") {
    size_t n = 0;
    const char* const* names = ImGuiStyleVarNames(n);
    REQUIRE(names != nullptr);
    size_t dn = 0;
    const char* const* dock = ImGuiStyleVarNamesDocking(dn);
    CHECK(dn == 1);
    CHECK(CStrEq(dock[0], "DockingSeparatorSize"));
    CHECK_FALSE(StyleVarIsVec2("DockingSeparatorSize"));
    CHECK(ImGuiStyleVarMinVersion("DockingSeparatorSize") == 19000);
#ifdef IMGUI_HAS_DOCK
    const size_t kExpectedCount = static_cast<size_t>(ImGuiStyleVar_COUNT) - dn;
#else
    const size_t kExpectedCount = static_cast<size_t>(ImGuiStyleVar_COUNT);
#endif
#if IMGUI_VERSION_NUM >= 19190
    CHECK(n == kExpectedCount);
#else
    CHECK(n >= 25);
    (void)kExpectedCount;
#endif
    std::set<std::string> seen;
    const size_t real_n = sizeof(kRealStyleVars) / sizeof(kRealStyleVars[0]);
    for (size_t i = 0; i < n; ++i) {
        INFO("var ", std::string(names[i]));
        CHECK(seen.insert(names[i]).second);
        CHECK(ImGuiStyleVarIndex(names[i]) == static_cast<int>(i));
        int mv = ImGuiStyleVarMinVersion(names[i]);
        CHECK(mv >= 18500);
        if (mv > IMGUI_VERSION_NUM) continue;
        const RealCol* real = FindReal(kRealStyleVars, real_n, names[i]);
        REQUIRE(real != nullptr);
        CHECK(real->vec2 == StyleVarIsVec2(names[i]));
#if IMGUI_VERSION_NUM >= 19190
        const int shifted = static_cast<int>(i + dn);
        CHECK((real->index == static_cast<int>(i) || real->index == shifted));
#endif
    }
    for (size_t k = 0; k < real_n; ++k) CHECK(seen.count(kRealStyleVars[k].name) == 1);
    CHECK(StyleVarIsVec2("WindowPadding"));
    CHECK(StyleVarIsVec2("SeparatorTextAlign"));
    CHECK_FALSE(StyleVarIsVec2("Alpha"));
    CHECK_FALSE(StyleVarIsVec2("Bogus"));
    CHECK_FALSE(StyleVarIsVec2(nullptr));
    CHECK(ImGuiStyleVarMinVersion("SeparatorTextPadding") == 18950);
    CHECK(ImGuiStyleVarMinVersion("ImageBorderSize") == 19190);
#if IMGUI_VERSION_NUM >= 19290
    CHECK(ImGuiStyleVarMinVersion("TreeLinesSize") == 19200);
    CHECK(ImGuiStyleVarMinVersion("SelectableRounding") == 19290);
#else
    CHECK(ImGuiStyleVarMinVersion("SelectableRounding") == 0);
#endif
    CHECK(ImGuiStyleVarMinVersion("Bogus") == 0);
    CHECK(ImGuiStyleVarIndex("Bogus") == -1);
}

TEST_CASE("WidgetCatalog::AddManifest with the Toggle example") {
    WidgetCatalog cat;
    Error err;
    REQUIRE(cat.AddManifest(J(kToggleManifest), &err));
    REQUIRE(cat.custom.size() == 1);
    const CustomWidgetDef* c = cat.FindCustom("Toggle");
    REQUIRE(c != nullptr);
    CHECK(c->type == "Toggle");
    CHECK(c->category == "Essentials");
    CHECK(c->include == "imb_essentials.h");
    CHECK(c->call == "imb::ess::Toggle({label}, {value}, {size}, {onColor})");
    CHECK(c->returns == "changed");
    CHECK(c->min_version == 18500);
    CHECK_FALSE(c->container);
    CHECK(c->sizable);
    CHECK(c->manifest["preview"].Size() == 2);
    REQUIRE(c->props.size() == 3);
    CHECK(c->props[0].type == PropType::String);
    CHECK(CStrEq(c->props[0].name, "label"));
    CHECK(J(c->props[0].def_json) == Json("Toggle"));
    CHECK(c->props[1].type == PropType::Size);
    CHECK(J(c->props[1].def_json) == J(kAuto));
    CHECK(c->props[2].type == PropType::Color);
    CHECK(J(c->props[2].def_json) == Json("#2ecc71ff"));
    REQUIRE(c->binds.size() == 1);
    CHECK(CStrEq(c->binds[0].name, "value"));
    CHECK(CStrEq(c->binds[0].types, "bool"));
    CHECK(c->binds[0].required);
    REQUIRE(c->events.size() == 1);
    CHECK(c->events[0] == "onChange");

    const WidgetDef& d = c->def;
    CHECK(AsWidgetDef(*c) == &d);
    CHECK(CStrEq(d.type, "Toggle"));
    CHECK(CStrEq(d.category, "Essentials"));
    CHECK(d.prop_count == 3);
    CHECK(d.bind_count == 1);
    CHECK(d.event_count == 1);
    CHECK(CStrEq(d.events[0], "onChange"));
    CHECK_FALSE(d.container);
    CHECK(d.sizable);
    CHECK(d.child_types == nullptr);
    CHECK(d.min_version == 18500);
    CHECK(CStrEq(d.icon, "fa-puzzle-piece"));
    CHECK(FindProp(d, "onColor") == &d.props[2]);
    CHECK(FindBind(d, "value") == &d.binds[0]);
    CHECK(WidgetHasEvent(d, "onChange"));
    CHECK(PropDefaultJson(d.props[1])["w"].AsString() == "auto");

    CHECK(cat.Find("Toggle") == &d);
    CHECK(cat.Find("Button") == FindWidget("Button"));
    CHECK(cat.Find("Nope") == nullptr);
    CHECK(cat.FindCustom("Button") == nullptr);
    size_t n = 0;
    StockWidgets(n);
    CHECK(cat.All().size() == n + 1);
    CHECK(cat.All().back() == &d);
}

TEST_CASE("WidgetCatalog pointers stay valid across growth and copies") {
    WidgetCatalog cat;
    for (int i = 0; i < 40; ++i) {
        Json m = J(kToggleManifest);
        m.Set("name", Json("Toggle" + std::to_string(i)));
        REQUIRE(cat.AddManifest(m, nullptr));
    }
    REQUIRE(cat.custom.size() == 40);
    for (int i = 0; i < 40; ++i) {
        const WidgetDef& d = cat.custom[static_cast<size_t>(i)].def;
        CHECK(std::string(d.type) == "Toggle" + std::to_string(i));
        CHECK(CStrEq(d.props[2].name, "onColor"));
        CHECK(CStrEq(d.binds[0].types, "bool"));
        CHECK(CStrEq(d.events[0], "onChange"));
    }
    WidgetCatalog copy = cat;
    cat.custom.clear();
    const WidgetDef* d = copy.Find("Toggle7");
    REQUIRE(d != nullptr);
    CHECK(CStrEq(d->type, "Toggle7"));
    CHECK(CStrEq(d->props[0].name, "label"));
    CHECK(J(d->props[0].def_json) == Json("Toggle"));

    Json m = J(kToggleManifest);
    m.Set("name", Json("Toggle7"));
    m.Set("category", Json("Replaced"));
    REQUIRE(copy.AddManifest(m, nullptr));
    CHECK(copy.custom.size() == 40);
    CHECK(copy.FindCustom("Toggle7")->category == "Replaced");
    CHECK(copy.RemoveCustom("Toggle7"));
    CHECK_FALSE(copy.RemoveCustom("Toggle7"));
    CHECK(copy.custom.size() == 39);
}

TEST_CASE("WidgetCatalog::AddManifest defaults, enums and flags") {
    WidgetCatalog cat;
    Error err;
    Json m = J(R"json({"kind":"widget","name":"Knob","call":"Knob({value})",
        "props":[{"name":"mode","type":"enum","values":["Linear","Log"]},
                 {"name":"flags","type":"flags","flags":"ImGuiSliderFlags","minImGuiVersion":18700},
                 {"name":"speed","type":"float","min":0.1,"max":10,"hint":"Drag speed"},
                 {"name":"items","type":"list<string>"},
                 {"name":"tint","type":"color"},
                 {"name":"size","type":"size"}],
        "childTypes":["Text","Button"],"container":true,"minImGuiVersion":18600})json");
    REQUIRE(cat.AddManifest(m, &err));
    const CustomWidgetDef* c = cat.FindCustom("Knob");
    REQUIRE(c != nullptr);
    CHECK(c->returns == "void");
    CHECK(c->category == "Custom");
    CHECK(c->container);
    CHECK(c->sizable);
    CHECK(CStrEq(c->def.child_types, "Text|Button"));
    CHECK(WidgetAcceptsChild(c->def, "Text"));
    CHECK_FALSE(WidgetAcceptsChild(c->def, "Child"));
    REQUIRE(c->props.size() == 6);
    CHECK(CStrEq(c->props[0].enum_values, "Linear|Log"));
    CHECK(J(c->props[0].def_json) == Json("Linear"));
    CHECK(c->props[0].min_version == 18600);
    CHECK(CStrEq(c->props[1].enum_values, "SliderFlags"));
    CHECK(PropDefaultJson(c->props[1]).IsArray());
    CHECK(c->props[1].min_version == 18700);
    CHECK(c->props[2].min == doctest::Approx(0.1f));
    CHECK(c->props[2].max == doctest::Approx(10.0f));
    CHECK(CStrEq(c->props[2].hint, "Drag speed"));
    CHECK(PropDefaultJson(c->props[2]).AsDouble() == 0.0);
    CHECK(c->props[3].type == PropType::StringList);
    CHECK(PropDefaultJson(c->props[3]).IsArray());
    CHECK(PropDefaultJson(c->props[4]).AsString() == "#ffffffff");
    CHECK(PropDefaultJson(c->props[5]) == J(kAuto));
    CHECK(c->props[5].hint == nullptr);
}

TEST_CASE("WidgetCatalog::AddManifest rejects invalid manifests") {
    WidgetCatalog cat;
    auto rejects = [&](const char* text, const char* needle) {
        Error err;
        bool ok = cat.AddManifest(J(text), &err);
        INFO(err.message);
        CHECK_FALSE(ok);
        CHECK(err.message.find(needle) != std::string::npos);
        CHECK(cat.custom.empty());
    };
    const std::string head = R"j({"kind":"widget","name":"X","call":"f()",)j";
    rejects(R"j({"kind":"component","name":"X","call":"f()"})j", "kind");
    rejects(R"j({"kind":"widget","call":"f()"})j", "name");
    rejects(R"j({"kind":"widget","name":"Button","call":"f()"})j", "stock");
    rejects(R"j({"kind":"widget","name":"X"})j", "call");
    rejects((head + R"j("returns":"maybe"})j").c_str(), "returns");
    rejects((head + R"j("props":{}})j").c_str(), "props");
    rejects((head + R"j("props":[{"type":"int"}]})j").c_str(), "name");
    rejects((head + R"j("props":[{"name":"a","type":"matrix"}]})j").c_str(), "unknown type");
    rejects((head + R"j("props":[{"name":"a","type":"enum"}]})j").c_str(), "values");
    rejects((head + R"j("props":[{"name":"a","type":"flags","flags":"Nope"}]})j").c_str(),
            "flags");
    rejects((head + R"j("props":[{"name":"a","type":"int"},{"name":"a","type":"int"}]})j").c_str(),
            "duplicated");
    rejects((head + R"j("bindings":[{"name":"v"}]})j").c_str(), "types");
    rejects((head + R"j("bindings":[{"types":["bool"]}]})j").c_str(), "name");
    rejects((head + R"j("events":[1]})j").c_str(), "events");
    rejects((head + R"j("childTypes":[1]})j").c_str(), "childTypes");
    Error err;
    CHECK_FALSE(cat.AddManifest(Json(), &err));
    CHECK_FALSE(cat.AddManifest(Json("str"), nullptr));
}

TEST_CASE("pipe list helpers") {
    std::vector<std::string> v = SplitPipe("a|bb|ccc");
    REQUIRE(v.size() == 3);
    CHECK(v[1] == "bb");
    CHECK(SplitPipe(nullptr).empty());
    CHECK(SplitPipe("").empty());
    CHECK(SplitPipe("|a||b|").size() == 2);
    CHECK(PipeListContains("Tab|MenuItem", "Tab"));
    CHECK(PipeListContains("Tab|MenuItem", "MenuItem"));
    CHECK_FALSE(PipeListContains("Tab|MenuItem", "Menu"));
    CHECK_FALSE(PipeListContains("Tab|MenuItem", "Ta"));
    CHECK_FALSE(PipeListContains(nullptr, "Tab"));
    CHECK_FALSE(PipeListContains("Tab", nullptr));
}
