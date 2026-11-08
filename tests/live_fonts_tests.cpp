#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#include "../src/builder/live_fonts.cpp"
#include "font_test_util.h"

using namespace imforge;
using namespace imforge::builder;
namespace fs = std::filesystem;

namespace {

struct TempTree {
    fs::path root;
    TempTree() {
        std::error_code ec;
        root = fs::temp_directory_path(ec) / ("imforge_live_fonts_" + std::to_string((unsigned long long)(uintptr_t)this) + "_" +
                                             std::to_string((long long)std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(root / "proj" / "fonts", ec);
        fs::create_directories(root / "builder_fonts", ec);
        fs::create_directories(root / "outside", ec);
        fs::create_directories(root / "proj2", ec);
    }
    ~TempTree() {
        std::error_code ec;
        fs::remove_all(root, ec);
    }
    std::string Write(const std::string& rel, const std::vector<unsigned char>& bytes) const {
        fs::path p = root / fs::u8path(rel);
        std::ofstream f(p, std::ios::binary);
        f.write((const char*)bytes.data(), (std::streamsize)bytes.size());
        return p.u8string();
    }
    std::string Path(const std::string& rel) const { return (root / fs::u8path(rel)).u8string(); }
    std::vector<std::string> Roots() const { return {Path("proj"), Path("builder_fonts")}; }
};

FontSpec Font(const char* name, const std::string& file, const char* merge = "none", float size = 16.0f) {
    FontSpec f;
    f.name = name;
    f.file = file;
    f.size = size;
    f.merge_icons = merge;
    f.ranges = "default";
    return f;
}

bool HasWarning(const LiveFontPlan& plan, const char* needle) {
    for (const std::string& w : plan.warnings)
        if (w.find(needle) != std::string::npos) return true;
    return false;
}

}  // namespace

TEST_CASE("only fonts inside the project and builder fonts folders may be sent") {
    TempTree t;
    std::vector<unsigned char> font = test::MakeTinyFont().bytes;
    std::string inside = t.Write("proj/fonts/a.ttf", font);
    std::string builder = t.Write("builder_fonts/fa.ttf", font);
    std::string outside = t.Write("outside/b.ttf", font);
    std::string sibling = t.Write("proj2/c.ttf", font);
    CHECK(FontPathAllowed(inside, t.Roots()));
    CHECK(FontPathAllowed(builder, t.Roots()));
    CHECK_FALSE(FontPathAllowed(outside, t.Roots()));
    CHECK_FALSE(FontPathAllowed(sibling, t.Roots()));
    CHECK_FALSE(FontPathAllowed(t.Path("proj/fonts/../../outside/b.ttf"), t.Roots()));
    CHECK_FALSE(FontPathAllowed(t.Path("proj/fonts/missing.ttf"), t.Roots()));
    CHECK_FALSE(FontPathAllowed(t.Path("proj"), t.Roots()));
    CHECK_FALSE(FontPathAllowed("\\\\server\\share\\x.ttf", t.Roots()));
    CHECK_FALSE(FontPathAllowed("//server/share/x.ttf", t.Roots()));
    CHECK_FALSE(FontPathAllowed(inside, {}));
#ifndef _WIN32
    std::error_code ec;
    fs::create_symlink(outside, t.Path("proj/fonts/link.ttf"), ec);
    if (!ec) CHECK_FALSE(FontPathAllowed(t.Path("proj/fonts/link.ttf"), t.Roots()));
#endif
}

TEST_CASE("the font plan sends each file once and explains what it skipped") {
    TempTree t;
    std::vector<unsigned char> font = test::MakeTinyFont().bytes;
    t.Write("proj/fonts/a.ttf", font);
    std::vector<unsigned char> icons = font;
    icons.push_back(0);
    t.Write("builder_fonts/fa-solid-900.ttf", icons);
    std::string outside = t.Write("outside/b.ttf", font);
    t.Write("proj/fonts/junk.ttf", std::vector<unsigned char>(300, 0x41));

    Project p;
    p.dir = t.Path("proj");
    p.fonts.push_back(Font("Default", "fonts/a.ttf", "fontawesome6"));
    p.fonts.push_back(Font("Title", "fonts/a.ttf", "fontawesome6", 900.0f));
    p.fonts.push_back(Font("Evil", outside));
    p.fonts.push_back(Font("Junk", "fonts/junk.ttf"));
    p.fonts.push_back(Font("Gone", "fonts/none.ttf"));
    p.fonts.push_back(Font("NoIcons", "fonts/a.ttf", "lucide"));

    std::string dir = p.dir;
    LiveFontResolver resolve = [&](const std::string& f) {
        fs::path q = fs::u8path(f);
        if (q.is_absolute()) return f;
        fs::path full = fs::u8path(dir) / q;
        std::error_code ec;
        return fs::exists(full, ec) ? full.u8string() : std::string();
    };
    LiveFontResolver resolve_icons = [&](const std::string& merge) {
        return merge == "fontawesome6" ? t.Path("builder_fonts/fa-solid-900.ttf") : std::string();
    };
    LiveFontCache cache;
    LiveFontPlan plan = cache.Plan(p, t.Roots(), resolve, resolve_icons);
    REQUIRE(plan.specs.size() == 3);
    CHECK(plan.specs[0].name == "Default");
    CHECK(plan.specs[1].name == "Title");
    CHECK(plan.specs[1].size == doctest::Approx(256.0f));
    CHECK(plan.specs[2].name == "NoIcons");
    CHECK(plan.specs[2].icons.empty());
    CHECK(plan.specs[0].file == plan.specs[1].file);
    CHECK(plan.specs[0].icons == plan.specs[1].icons);
    CHECK(plan.specs[0].icons != plan.specs[0].file);
    CHECK(plan.specs[0].icon_lo == 0xe005);
    CHECK(plan.specs[0].icon_hi == 0xf8ff);
    CHECK(plan.files.size() == 2);
    CHECK(HasWarning(plan, "'Evil' not sent to the app: outside"));
    CHECK(HasWarning(plan, "'Junk' not sent to the app"));
    CHECK(HasWarning(plan, "file not found: fonts/none.ttf"));
    CHECK(HasWarning(plan, "icons for font 'NoIcons'"));

    std::vector<FontSpecMsg> back;
    CHECK(ParseFonts(MsgFonts(plan.specs), back));

    std::string err;
    const LiveFontFile* again = cache.Load(t.Path("proj/fonts/a.ttf"), t.Roots(), err);
    CHECK(again == plan.files[0]);
}

TEST_CASE("font files go out in chunks that put the file back together") {
    TempTree t;
    std::vector<unsigned char> font = test::MakeTinyFont().bytes;
    font.resize(kProtocolFontChunk * 2 + 777, 0);
    std::string path = t.Write("proj/fonts/big.ttf", font);
    LiveFontCache cache;
    std::string err;
    const LiveFontFile* f = cache.Load(path, t.Roots(), err);
    REQUIRE_MESSAGE(f != nullptr, err);
    std::vector<Json> msgs = FontDataMessages(*f);
    REQUIRE(msgs.size() == 3);
    std::vector<unsigned char> joined;
    for (const Json& m : msgs) {
        CHECK(Frame(m).size() < (size_t(1) << 20) - 64);
        FontDataMsg d;
        REQUIRE(ParseFontData(m, d));
        CHECK(d.offset == joined.size());
        CHECK(d.hash == f->hash);
        joined.insert(joined.end(), d.bytes.begin(), d.bytes.end());
    }
    CHECK(joined == font);

    std::vector<unsigned char> huge(kProtocolMaxFontBytes + 1, 0);
    std::string big = t.Write("proj/fonts/huge.ttf", huge);
    CHECK(cache.Load(big, t.Roots(), err) == nullptr);
    CHECK(err.find("8 MB") != std::string::npos);
}

namespace {

LiveFontResolver DirResolver(const std::string& dir) {
    return [dir](const std::string& f) {
        fs::path full = fs::u8path(dir) / fs::u8path(f);
        std::error_code ec;
        return fs::exists(full, ec) ? full.u8string() : std::string();
    };
}

LiveFontResolver NoIcons() {
    return [](const std::string&) { return std::string(); };
}

}  // namespace

TEST_CASE("the font cache forgets files the project no longer uses") {
    TempTree t;
    std::vector<unsigned char> font = test::MakeTinyFont().bytes;
    LiveFontCache cache;
    std::string err;
    for (int i = 0; i < 5; ++i) {
        std::vector<unsigned char> f = font;
        f.resize(f.size() + (size_t)i, 0);
        REQUIRE(cache.Load(t.Write("proj/fonts/f" + std::to_string(i) + ".ttf", f), t.Roots(), err) != nullptr);
    }
    CHECK(cache.CachedFiles() == 5);
    Project p;
    p.dir = t.Path("proj");
    p.fonts.push_back(Font("One", "fonts/f2.ttf"));
    LiveFontPlan plan = cache.Plan(p, t.Roots(), DirResolver(p.dir), NoIcons());
    REQUIRE(plan.specs.size() == 1);
    CHECK(cache.CachedFiles() == 1);
    CHECK(plan.files[0]->hash == plan.specs[0].file);
}

TEST_CASE("a project can't make the builder look at an endless list of fonts") {
    TempTree t;
    std::vector<unsigned char> font = test::MakeTinyFont().bytes;
    Project p;
    p.dir = t.Path("proj");
    for (int i = 0; i < 10; ++i) p.fonts.push_back(Font("Gone", "fonts/none" + std::to_string(i) + ".ttf"));
    t.Write("proj/fonts/real.ttf", font);
    for (int i = 0; i < 40; ++i) p.fonts.push_back(Font("Real", "fonts/real.ttf"));
    LiveFontCache cache;
    LiveFontPlan plan = cache.Plan(p, t.Roots(), DirResolver(p.dir), NoIcons());
    CHECK(plan.specs.size() == kProtocolMaxFonts - 10);
    CHECK(HasWarning(plan, "only the first 32 fonts"));
    CHECK(cache.CachedFiles() == plan.files.size());
}

TEST_CASE("the builder stops reading font files once they add up past the total cap") {
    TempTree t;
    std::vector<unsigned char> font = test::MakeTinyFont().bytes;
    font.resize(size_t(6) << 20, 0);
    Project p;
    p.dir = t.Path("proj");
    for (int i = 0; i < 8; ++i) {
        std::string name = "fonts/big" + std::to_string(i) + ".ttf";
        t.Write("proj/" + name, font);
        p.fonts.push_back(Font("Big", name));
    }
    LiveFontCache cache;
    LiveFontPlan plan = cache.Plan(p, t.Roots(), DirResolver(p.dir), NoIcons());
    CHECK(plan.specs.size() == 5);
    CHECK(HasWarning(plan, "over 32 MB"));
    CHECK(cache.CachedFiles() == 5);
}
