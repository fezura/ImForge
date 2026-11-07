#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "../src/builder/ui/tree_ops.cpp"

using namespace imforge;
using namespace imforge::builder::tree;

namespace {

Node ParseRoot(const char* text) {
    Json j;
    JsonError je;
    REQUIRE_MESSAGE(ParseJson(text, j, &je), je.message);
    Node n;
    Error e;
    REQUIRE_MESSAGE(NodeFromJson(j, n, &e), e.message);
    return n;
}

const char* kTree = R"json({
  "id": "window", "type": "Window", "children": [
    { "id": "stack", "type": "VStack", "children": [
      { "id": "a", "type": "Button", "props": { "label": "A" },
        "events": { "onClick": { "actions": [ { "type": "openPopup", "target": "pop" },
                                              { "type": "toggle", "target": "cfg.flag" } ] } } },
      { "id": "b", "type": "Text" },
      { "id": "c", "type": "Checkbox" },
      { "id": "pop", "type": "Popup", "children": [ { "id": "pt", "type": "Text" } ] }
    ] },
    { "id": "tabs", "type": "TabBar", "children": [
      { "id": "tab1", "type": "Tab", "children": [
        { "id": "sw", "type": "Button",
          "events": { "onClick": { "actions": [ { "type": "setTab", "target": "tabs", "value": "tab2" } ] } } } ] },
      { "id": "tab2", "type": "Tab" }
    ] },
    { "id": "menu", "type": "Menu", "children": [ { "id": "mi", "type": "MenuItem" } ] }
  ]
})json";

std::vector<std::string> ChildIds(const Node& n) {
    std::vector<std::string> out;
    for (const Node& c : n.children) out.push_back(c.id);
    return out;
}

bool UniqueIds(const Node& root) {
    std::vector<std::string> ids;
    CollectIds(root, ids);
    std::sort(ids.begin(), ids.end());
    return std::adjacent_find(ids.begin(), ids.end()) == ids.end();
}

}  // namespace

TEST_CASE("LocateNode and PathToNode") {
    Node root = ParseRoot(kTree);
    NodeLocation loc;
    REQUIRE(LocateNode(root, "c", loc));
    CHECK(loc.parent->id == "stack");
    CHECK(loc.index == 2);
    CHECK(loc.list == &loc.parent->children);
    CHECK_FALSE(LocateNode(root, "window", loc));
    CHECK_FALSE(LocateNode(root, "nope", loc));

    CHECK(PathToNode(root, "sw") == std::vector<std::string>{"window", "tabs", "tab1", "sw"});
    CHECK(PathToNode(root, "window") == std::vector<std::string>{"window"});
    CHECK(PathToNode(root, "missing").empty());

    CHECK(IsAncestor(root, "stack", "pt"));
    CHECK_FALSE(IsAncestor(root, "pt", "stack"));
    CHECK_FALSE(IsAncestor(root, "a", "a"));
}

TEST_CASE("InsertChild index semantics") {
    Node root = ParseRoot(kTree);
    Node* stack = FindNode(root, "stack");
    Node n;
    n.id = "x";
    n.type = "Text";
    Node* ins = InsertChild(*stack, n, 1);
    CHECK(ins->id == "x");
    CHECK(ChildIds(*stack) == std::vector<std::string>{"a", "x", "b", "c", "pop"});
    n.id = "y";
    InsertChild(*stack, n, -1);
    CHECK(stack->children.back().id == "y");
    n.id = "z";
    InsertChild(*stack, n, 99);
    CHECK(stack->children.back().id == "z");
}

TEST_CASE("AcceptsChild and CanReparent") {
    Node root = ParseRoot(kTree);
    CHECK(AcceptsChild(nullptr, *FindNode(root, "stack"), "Button"));
    CHECK_FALSE(AcceptsChild(nullptr, *FindNode(root, "stack"), "Tab"));
    CHECK(AcceptsChild(nullptr, *FindNode(root, "tabs"), "Tab"));
    CHECK_FALSE(AcceptsChild(nullptr, *FindNode(root, "tabs"), "Button"));
    CHECK_FALSE(AcceptsChild(nullptr, *FindNode(root, "a"), "Text"));
    CHECK_FALSE(AcceptsChild(nullptr, *FindNode(root, "menu"), "Button"));
    CHECK(AcceptsChild(nullptr, *FindNode(root, "menu"), "MenuItem"));
    CHECK_FALSE(AcceptsChild(nullptr, root, "Window"));

    CHECK(CanReparent(root, "a", "tab1", nullptr));
    CHECK_FALSE(CanReparent(root, "a", "tabs", nullptr));
    CHECK_FALSE(CanReparent(root, "stack", "pop", nullptr));
    CHECK_FALSE(CanReparent(root, "stack", "stack", nullptr));
    CHECK_FALSE(CanReparent(root, "window", "stack", nullptr));
    CHECK(CanReparent(root, "a", "", nullptr));
}

TEST_CASE("MoveNode within a list and across parents") {
    Node root = ParseRoot(kTree);
    Node* stack = FindNode(root, "stack");
    SUBCASE("down past a sibling") {
        CHECK(MoveNode(root, "a", "stack", 3));
        CHECK(ChildIds(*stack) == std::vector<std::string>{"b", "c", "a", "pop"});
    }
    SUBCASE("up to front") {
        CHECK(MoveNode(root, "c", "stack", 0));
        CHECK(ChildIds(*stack) == std::vector<std::string>{"c", "a", "b", "pop"});
    }
    SUBCASE("append to another parent") {
        CHECK(MoveNode(root, "b", "tab1", -1));
        CHECK(ChildIds(*stack) == std::vector<std::string>{"a", "c", "pop"});
        CHECK(ChildIds(*FindNode(root, "tab1")) == std::vector<std::string>{"sw", "b"});
    }
    SUBCASE("rejects root and own subtree") {
        CHECK_FALSE(MoveNode(root, "window", "stack", 0));
        CHECK_FALSE(MoveNode(root, "stack", "pop", 0));
        CHECK_FALSE(MoveNode(root, "stack", "stack", 0));
        CHECK(UniqueIds(root));
    }
}

TEST_CASE("MoveNodes keeps order and resolves anchors after removal") {
    Node root = ParseRoot(kTree);
    Node* stack = FindNode(root, "stack");
    SUBCASE("multiple before an anchor in the same list") {
        CHECK(MoveNodes(root, {"a", "c"}, "stack", "pop", false));
        CHECK(ChildIds(*stack) == std::vector<std::string>{"b", "a", "c", "pop"});
    }
    SUBCASE("after an anchor in another parent") {
        CHECK(MoveNodes(root, {"c", "a"}, "tab1", "sw", true));
        CHECK(ChildIds(*FindNode(root, "tab1")) == std::vector<std::string>{"sw", "c", "a"});
        CHECK(ChildIds(*stack) == std::vector<std::string>{"b", "pop"});
    }
    SUBCASE("nested ids collapse to the top level") {
        CHECK(MoveNodes(root, {"pop", "pt"}, "tab2", "", false));
        CHECK(ChildIds(*FindNode(root, "tab2")) == std::vector<std::string>{"pop"});
        CHECK(FindNode(root, "pt") != nullptr);
    }
    SUBCASE("rejects anchor inside the moved set") {
        CHECK_FALSE(MoveNodes(root, {"stack"}, "window", "a", true));
        CHECK_FALSE(MoveNodes(root, {"stack"}, "pop", "", false));
    }
}

TEST_CASE("MoveSibling") {
    Node root = ParseRoot(kTree);
    Node* stack = FindNode(root, "stack");
    CHECK(MoveSibling(root, "b", 1));
    CHECK(ChildIds(*stack) == std::vector<std::string>{"a", "c", "b", "pop"});
    CHECK(MoveSibling(root, "b", -2));
    CHECK(ChildIds(*stack) == std::vector<std::string>{"b", "a", "c", "pop"});
    CHECK_FALSE(MoveSibling(root, "b", -1));
    CHECK_FALSE(MoveSibling(root, "pop", 1));
    CHECK_FALSE(MoveSibling(root, "window", 1));
}

TEST_CASE("RemoveNode") {
    Node root = ParseRoot(kTree);
    Node out;
    CHECK(RemoveNode(root, "pop", &out));
    CHECK(out.id == "pop");
    CHECK(out.children.size() == 1);
    CHECK(FindNode(root, "pop") == nullptr);
    CHECK(FindNode(root, "pt") == nullptr);
    CHECK_FALSE(RemoveNode(root, "window", nullptr));
    CHECK_FALSE(RemoveNode(root, "pop", nullptr));
    CHECK(RemoveNode(root, "mi", nullptr));
    CHECK(FindNode(root, "menu")->children.empty());
}

TEST_CASE("CloneWithFreshIds regenerates ids and remaps intra-subtree references") {
    Node root = ParseRoot(kTree);
    const Node& stack = *FindNode(root, "stack");
    Node clone = CloneWithFreshIds(stack, root);
    CHECK(clone.id == "vStack_1");
    CHECK(clone.children.size() == 4);
    std::vector<std::string> ids;
    CollectIds(clone, ids);
    for (const std::string& id : ids) CHECK(FindNode(root, id) == nullptr);
    Node merged = root;
    merged.children.push_back(clone);
    CHECK(UniqueIds(merged));

    const Node& a = clone.children[0];
    CHECK(a.type == "Button");
    CHECK(a.id == "button_1");
    CHECK(clone.children[3].id == "popup_1");
    CHECK(clone.children[3].children[0].id == "text_2");
    REQUIRE(a.events.size() == 1);
    const std::vector<Action>& acts = a.events[0].second.actions;
    REQUIRE(acts.size() == 2);
    CHECK(acts[0].target == clone.children[3].id);
    CHECK(acts[1].target == "cfg.flag");

    Node tab1 = CloneWithFreshIds(*FindNode(root, "tab1"), root);
    const Action& st = tab1.children[0].events[0].second.actions[0];
    CHECK(st.target == "tabs");
    CHECK(st.value.AsString() == "tab2");

    Node tabs = CloneWithFreshIds(*FindNode(root, "tabs"), root);
    const Action& st2 = tabs.children[0].children[0].events[0].second.actions[0];
    CHECK(st2.target == tabs.id);
    CHECK(st2.value.AsString() == tabs.children[1].id);
}

TEST_CASE("WrapNode and WrapNodes") {
    Node root = ParseRoot(kTree);
    SUBCASE("single node in a VStack") {
        CHECK(WrapNode(root, "b", "HStack"));
        Node* stack = FindNode(root, "stack");
        CHECK(stack->children[1].type == "HStack");
        CHECK(stack->children[1].id == "hStack_1");
        CHECK(ChildIds(stack->children[1]) == std::vector<std::string>{"b"});
        CHECK(UniqueIds(root));
    }
    SUBCASE("several siblings keep order and land at the first index") {
        std::string wid;
        CHECK(WrapNodes(root, {"c", "a"}, "Group", &wid));
        Node* stack = FindNode(root, "stack");
        CHECK(ChildIds(*stack) == std::vector<std::string>{wid, "b", "pop"});
        CHECK(ChildIds(*FindNode(root, wid)) == std::vector<std::string>{"a", "c"});
    }
    SUBCASE("TabBar wraps into TabBar + Tab") {
        std::string wid;
        CHECK(WrapNodes(root, {"a"}, "TabBar", &wid));
        Node* w = FindNode(root, wid);
        REQUIRE(w);
        CHECK(w->type == "TabBar");
        REQUIRE(w->children.size() == 1);
        CHECK(w->children[0].type == "Tab");
        CHECK(w->children[0].Prop("label")->raw.AsString() == "Tab");
        CHECK(ChildIds(w->children[0]) == std::vector<std::string>{"a"});
        CHECK(UniqueIds(root));
    }
    SUBCASE("rejects root, mixed parents and empty") {
        CHECK_FALSE(WrapNode(root, "window", "VStack"));
        CHECK_FALSE(WrapNodes(root, {"a", "sw"}, "VStack", nullptr));
        CHECK_FALSE(WrapNodes(root, {}, "VStack", nullptr));
    }
    SUBCASE("CanWrap validates container rules") {
        CHECK(CanWrap(root, {"a", "b"}, "HStack", nullptr));
        CHECK_FALSE(CanWrap(root, {"tab1"}, "VStack", nullptr));
        CHECK_FALSE(CanWrap(root, {"mi"}, "VStack", nullptr));
        CHECK(CanWrap(root, {"a"}, "TabBar", nullptr));
        CHECK_FALSE(CanWrap(root, {"a"}, "Tab", nullptr));
    }
}

TEST_CASE("UnwrapNode") {
    Node root = ParseRoot(kTree);
    SUBCASE("splices children in place") {
        CHECK(UnwrapNode(root, "stack"));
        CHECK(ChildIds(root) == std::vector<std::string>{"a", "b", "c", "pop", "tabs", "menu"});
        CHECK(UniqueIds(root));
    }
    SUBCASE("leaf cannot be unwrapped") {
        CHECK_FALSE(UnwrapNode(root, "b"));
        CHECK(FindNode(root, "b") != nullptr);
    }
    SUBCASE("catalog rules") {
        CHECK_FALSE(CanUnwrap(root, "tabs", nullptr));
        WidgetCatalog empty;
        CHECK_FALSE(UnwrapNode(root, "tabs", &empty));
        CHECK(CanUnwrap(root, "pop", nullptr));
        CHECK(CanUnwrap(root, "stack", nullptr));
        CHECK_FALSE(CanUnwrap(root, "window", nullptr));
    }
    SUBCASE("component instance slots are spliced") {
        Node* stack = FindNode(root, "stack");
        Node inst;
        inst.id = "inst";
        inst.type = "Component";
        inst.component = "Card";
        Node slotted;
        slotted.id = "slotted";
        slotted.type = "Text";
        inst.slots.emplace_back("content", std::vector<Node>{slotted});
        stack->children.push_back(inst);
        CHECK(UnwrapNode(root, "inst"));
        CHECK(stack->children.back().id == "slotted");
    }
}

TEST_CASE("MakeDefaultNode applies catalog defaults") {
    Node root = ParseRoot(kTree);
    Node b = MakeDefaultNode("Button", root, nullptr);
    CHECK(b.id == "button_1");
    CHECK(b.type == "Button");
    REQUIRE(b.Prop("label"));
    CHECK(b.Prop("label")->raw.AsString() == "Button");
    CHECK(b.Prop("size") == nullptr);

    Node t = MakeDefaultNode("Text", root, nullptr);
    CHECK(t.Prop("text")->raw.AsString() == "Text");
    Node w = MakeDefaultNode("Window", root, nullptr);
    CHECK(w.Prop("title")->raw.AsString() == "Window");
    CHECK(w.id == "window_1");
    Node combo = MakeDefaultNode("Combo", root, nullptr);
    CHECK(combo.Prop("items")->raw.Size() == 3);
    Node grid = MakeDefaultNode("Grid", root, nullptr);
    CHECK(grid.Prop("columns")->raw.AsInt() == 2);
    Node slot = MakeDefaultNode("Slot", root, nullptr);
    CHECK(slot.Prop("name")->raw.AsString() == "content");
    Node plot = MakeDefaultNode("PlotLines", root, nullptr);
    CHECK(plot.Prop("label")->raw.AsString() == "");
    Node sep = MakeDefaultNode("Separator", root, nullptr);
    CHECK(sep.props.empty());
    Node custom = MakeDefaultNode("Mystery", root, nullptr);
    CHECK(custom.props.empty());
    CHECK(custom.id == "mystery_1");

    WidgetCatalog cat;
    Json m;
    JsonError je;
    REQUIRE(ParseJson(R"json({"schemaVersion":1,"kind":"widget","name":"Toggle","category":"Essentials",
        "call":"imb::ess::Toggle({label}, {value})","returns":"changed",
        "props":[{"name":"label","type":"string","default":"Toggle"}],
        "bindings":[{"name":"value","types":["bool"],"required":true}]})json", m, &je));
    Error err;
    REQUIRE_MESSAGE(cat.AddManifest(m, &err), err.message);
    Node tg = MakeDefaultNode("Toggle", root, &cat);
    CHECK(tg.Prop("label")->raw.AsString() == "Toggle");
    CHECK(tg.id == "toggle_1");
}

TEST_CASE("MakeComponentInstance") {
    Node root = ParseRoot(kTree);
    Component def;
    def.name = "LabeledSlider";
    def.params.push_back(Param{"label", "string", Json("Value")});
    def.params.push_back(Param{"value", "binding<float>", Json()});
    def.params.push_back(Param{"min", "float", Json(0)});
    def.params.push_back(Param{"note", "string", Json()});
    def.slots.push_back("content");
    Node inst = MakeComponentInstance(def, root);
    CHECK(inst.type == "Component");
    CHECK(inst.component == "LabeledSlider");
    CHECK(inst.id == "labeledSlider_1");
    CHECK(inst.Prop("label")->raw.AsString() == "Value");
    CHECK(inst.Prop("min")->raw.AsDouble() == 0);
    CHECK(inst.Prop("value") == nullptr);
    CHECK(inst.Prop("note") == nullptr);
    REQUIRE(inst.slots.size() == 1);
    CHECK(inst.slots[0].first == "content");
    CHECK(inst.slots[0].second.empty());
    Json j = NodeToJson(inst);
    CHECK(j["component"].AsString() == "LabeledSlider");
}

TEST_CASE("TopLevelIds") {
    Node root = ParseRoot(kTree);
    CHECK(TopLevelIds(root, {"pt", "stack", "a", "tab2", "ghost", "stack"}) ==
          std::vector<std::string>{"stack", "tab2"});
    CHECK(TopLevelIds(root, {}).empty());
}

TEST_CASE("FindInsertPoint") {
    Node root = ParseRoot(kTree);
    std::string parent;
    int index = -2;
    SUBCASE("selected container appends") {
        CHECK(FindInsertPoint(root, "stack", "Button", nullptr, parent, index));
        CHECK(parent == "stack");
        CHECK(index == -1);
    }
    SUBCASE("selected leaf inserts after it") {
        CHECK(FindInsertPoint(root, "b", "Button", nullptr, parent, index));
        CHECK(parent == "stack");
        CHECK(index == 2);
    }
    SUBCASE("walks up to a parent that accepts the type") {
        CHECK(FindInsertPoint(root, "sw", "Tab", nullptr, parent, index));
        CHECK(parent == "tabs");
        CHECK(index == 1);
    }
    SUBCASE("nothing selected targets the root") {
        CHECK(FindInsertPoint(root, "", "Text", nullptr, parent, index));
        CHECK(parent == "window");
        CHECK(index == -1);
        CHECK_FALSE(FindInsertPoint(root, "", "Window", nullptr, parent, index));
    }
    SUBCASE("no valid place") {
        CHECK_FALSE(FindInsertPoint(root, "mi", "Tab", nullptr, parent, index));
    }
}
