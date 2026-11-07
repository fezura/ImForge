#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "../src/builder/undo.h"

using namespace imforge;
using namespace imforge::builder;

namespace {

Json J(int v) { return Json(v); }

void PushN(UndoStack& s, int count, double t0 = 0.0) {
    for (int i = 0; i < count; ++i) s.Push("Edit " + std::to_string(i), DocKind::Panel, "Main", J(i), J(i + 1), t0 + i);
}

}  // namespace

TEST_CASE("empty stack") {
    UndoStack s;
    CHECK_FALSE(s.CanUndo());
    CHECK_FALSE(s.CanRedo());
    CHECK(s.Undo() == nullptr);
    CHECK(s.Redo() == nullptr);
    CHECK(s.PeekUndo() == nullptr);
    CHECK(s.PeekRedo() == nullptr);
    CHECK(s.Size() == 0);
    CHECK(s.Cursor() == 0);
}

TEST_CASE("Push records the entry and advances the cursor") {
    UndoStack s;
    s.Push("Move", DocKind::Panel, "Main", J(1), J(2), 4.5);
    REQUIRE(s.Size() == 1);
    CHECK(s.Cursor() == 1);
    CHECK(s.CanUndo());
    CHECK_FALSE(s.CanRedo());
    const UndoEntry* e = s.PeekUndo();
    REQUIRE(e);
    CHECK(e->label == "Move");
    CHECK(e->kind == DocKind::Panel);
    CHECK(e->name == "Main");
    CHECK(e->before == J(1));
    CHECK(e->after == J(2));
    CHECK(e->time == 4.5);
}

TEST_CASE("no-op pushes are dropped and do not disturb the redo list") {
    UndoStack s;
    s.Push("Edit", DocKind::Panel, "Main", J(1), J(1), 0.0);
    CHECK(s.Size() == 0);
    PushN(s, 2);
    REQUIRE(s.Undo());
    CHECK(s.CanRedo());
    s.Push("Other", DocKind::Component, "Card", Json("x"), Json("x"), 100.0);
    CHECK(s.Size() == 2);
    CHECK(s.Cursor() == 1);
    CHECK(s.CanRedo());
}

TEST_CASE("Undo and Redo walk the cursor without losing entries") {
    UndoStack s;
    PushN(s, 3);
    const UndoEntry* e = s.Undo();
    REQUIRE(e);
    CHECK(e->label == "Edit 2");
    CHECK(s.Cursor() == 2);
    CHECK(s.PeekRedo() == e);
    CHECK(s.PeekUndo()->label == "Edit 1");
    e = s.Undo();
    REQUIRE(e);
    CHECK(e->label == "Edit 1");
    e = s.Redo();
    REQUIRE(e);
    CHECK(e->label == "Edit 1");
    CHECK(s.Cursor() == 2);
    e = s.Redo();
    REQUIRE(e);
    CHECK(e->label == "Edit 2");
    CHECK(s.Redo() == nullptr);
    CHECK(s.Size() == 3);
    while (s.Undo()) {
    }
    CHECK(s.Cursor() == 0);
    CHECK(s.Size() == 3);
    CHECK(s.Undo() == nullptr);
}

TEST_CASE("Push after Undo truncates the redo entries") {
    UndoStack s;
    PushN(s, 3);
    s.Undo();
    s.Undo();
    CHECK(s.Cursor() == 1);
    s.Push("Fresh", DocKind::Panel, "Main", J(1), J(9), 50.0);
    CHECK(s.Size() == 2);
    CHECK(s.Cursor() == 2);
    CHECK_FALSE(s.CanRedo());
    CHECK(s.Entries()[0].label == "Edit 0");
    CHECK(s.Entries()[1].label == "Fresh");
}

TEST_CASE("pushes with the same label, kind and name within 300 ms coalesce") {
    UndoStack s;
    s.Push("Drag", DocKind::Panel, "Main", J(0), J(1), 10.0);
    s.Push("Drag", DocKind::Panel, "Main", J(1), J(2), 10.2);
    REQUIRE(s.Size() == 1);
    CHECK(s.Entries()[0].before == J(0));
    CHECK(s.Entries()[0].after == J(2));
    CHECK(s.Entries()[0].time == 10.2);
    SUBCASE("the window slides with the last coalesced push") {
        s.Push("Drag", DocKind::Panel, "Main", J(2), J(3), 10.45);
        CHECK(s.Size() == 1);
        CHECK(s.Entries()[0].after == J(3));
        s.Push("Drag", DocKind::Panel, "Main", J(3), J(4), 10.8);
        CHECK(s.Size() == 2);
        CHECK(s.Entries()[1].before == J(3));
    }
    SUBCASE("exactly kCoalesceWindow still coalesces") {
        UndoStack b;
        b.Push("Drag", DocKind::Panel, "Main", J(0), J(1), 0.0);
        b.Push("Drag", DocKind::Panel, "Main", J(1), J(2), UndoStack::kCoalesceWindow);
        CHECK(b.Size() == 1);
    }
    SUBCASE("just past the window starts a new entry") {
        UndoStack b;
        b.Push("Drag", DocKind::Panel, "Main", J(0), J(1), 0.0);
        b.Push("Drag", DocKind::Panel, "Main", J(1), J(2), UndoStack::kCoalesceWindow + 0.001);
        CHECK(b.Size() == 2);
    }
}

TEST_CASE("coalescing is keyed on label, kind and document name") {
    UndoStack s;
    s.Push("Drag", DocKind::Panel, "Main", J(0), J(1), 0.0);
    s.Push("Resize", DocKind::Panel, "Main", J(1), J(2), 0.05);
    CHECK(s.Size() == 2);
    s.Push("Resize", DocKind::Panel, "Other", J(2), J(3), 0.1);
    CHECK(s.Size() == 3);
    s.Push("Resize", DocKind::Component, "Other", J(3), J(4), 0.15);
    CHECK(s.Size() == 4);
    s.Push("Resize", DocKind::Component, "Other", J(4), J(5), 0.2);
    CHECK(s.Size() == 4);
    CHECK(s.Entries()[3].after == J(5));
}

TEST_CASE("coalescing back to the original value removes the entry") {
    UndoStack s;
    PushN(s, 1, 0.0);
    s.Push("Toggle", DocKind::Panel, "Main", J(0), J(1), 5.0);
    CHECK(s.Size() == 2);
    s.Push("Toggle", DocKind::Panel, "Main", J(1), J(0), 5.1);
    CHECK(s.Size() == 1);
    CHECK(s.Cursor() == 1);
    CHECK(s.PeekUndo()->label == "Edit 0");
    s.Push("Toggle", DocKind::Panel, "Main", J(0), J(1), 5.2);
    s.Push("Toggle", DocKind::Panel, "Main", J(1), J(0), 5.3);
    CHECK(s.Size() == 1);
    CHECK(s.CanUndo());
}

TEST_CASE("coalescing never merges into an undone entry") {
    UndoStack s;
    s.Push("Drag", DocKind::Panel, "Main", J(0), J(1), 0.0);
    s.Push("Drag", DocKind::Panel, "Main", J(1), J(2), 1.0);
    REQUIRE(s.Size() == 2);
    s.Undo();
    s.Push("Drag", DocKind::Panel, "Main", J(1), J(7), 1.1);
    REQUIRE(s.Size() == 2);
    CHECK(s.Entries()[0].after == J(1));
    CHECK(s.Entries()[1].before == J(1));
    CHECK(s.Entries()[1].after == J(7));
    CHECK(s.Cursor() == 2);
}

TEST_CASE("the stack keeps at most kMaxEntries, dropping the oldest") {
    UndoStack s;
    const int extra = 5;
    PushN(s, (int)UndoStack::kMaxEntries + extra);
    CHECK(s.Size() == UndoStack::kMaxEntries);
    CHECK(s.Cursor() == UndoStack::kMaxEntries);
    CHECK(s.Entries().front().before == J(extra));
    CHECK(s.Entries().back().after == J((int)UndoStack::kMaxEntries + extra));
    int undone = 0;
    while (s.Undo()) ++undone;
    CHECK(undone == (int)UndoStack::kMaxEntries);
}

TEST_CASE("Clear empties everything") {
    UndoStack s;
    PushN(s, 4);
    s.Undo();
    s.Clear();
    CHECK(s.Size() == 0);
    CHECK(s.Cursor() == 0);
    CHECK_FALSE(s.CanUndo());
    CHECK_FALSE(s.CanRedo());
    PushN(s, 1);
    CHECK(s.Size() == 1);
}

TEST_CASE("entries keep structured JSON snapshots") {
    UndoStack s;
    Json before = Json::Object();
    before.Set("id", Json("a"));
    Json after = before;
    after.Set("id", Json("b"));
    s.Push("Rename", DocKind::Project, "", before, after, 0.0);
    REQUIRE(s.Size() == 1);
    CHECK(s.Entries()[0].before["id"].AsString() == "a");
    CHECK(s.Entries()[0].after["id"].AsString() == "b");
    CHECK(s.Entries()[0].kind == DocKind::Project);
    CHECK(s.Entries()[0].name.empty());
}
