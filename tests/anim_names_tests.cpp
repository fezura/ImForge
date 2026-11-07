#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "imforge/imforge_anim_names.h"

#include <cstring>
#include <set>
#include <string>

TEST_CASE("every imb::Ease round-trips through its name") {
    std::set<std::string> seen;
    for (int i = 0; i < imforge::kEaseCount; ++i) {
        imb::Ease e = static_cast<imb::Ease>(i);
        const char* name = imforge::EaseName(e);
        REQUIRE(name != nullptr);
        CHECK(std::strlen(name) > 0);
        CHECK(seen.insert(name).second);
        imb::Ease back = imb::Ease::Linear;
        CHECK(imforge::TryEaseFromName(name, &back));
        CHECK(back == e);
        CHECK(imforge::EaseFromName(name) == e);
    }
    CHECK(seen.size() == static_cast<size_t>(imforge::kEaseCount));
}

TEST_CASE("ease names keep their spelling and order") {
    const char* expected[] = {"Linear",  "InQuad",     "OutQuad", "InOutQuad", "InCubic",    "OutCubic",
                              "InOutCubic", "OutBack", "InBack",  "OutElastic", "OutBounce", "OutExpo"};
    for (int i = 0; i < imforge::kEaseCount; ++i) CHECK(std::string(imforge::EaseName(static_cast<imb::Ease>(i))) == expected[i]);
    CHECK(imforge::EaseName(imb::Ease::OutElastic) == std::string("OutElastic"));
}

TEST_CASE("every imb::Preset round-trips through its name") {
    std::set<std::string> seen;
    for (int i = 0; i < imforge::kPresetCount; ++i) {
        imb::Preset p = static_cast<imb::Preset>(i);
        const char* name = imforge::PresetName(p);
        REQUIRE(name != nullptr);
        CHECK(seen.insert(name).second);
        imb::Preset back = imb::Preset::Fade;
        CHECK(imforge::TryPresetFromName(name, &back));
        CHECK(back == p);
        CHECK(imforge::PresetFromName(name) == p);
    }
    CHECK(seen.size() == static_cast<size_t>(imforge::kPresetCount));
}

TEST_CASE("preset names match the schema enum") {
    const char* expected[] = {"None",        "Fade",          "SlideUp",  "SlideDown", "SlideLeft",
                              "SlideRight",  "FadeSlideUp",   "FadeSlideDown", "ScalePop", "Stagger"};
    for (int i = 0; i < imforge::kPresetCount; ++i)
        CHECK(std::string(imforge::PresetName(static_cast<imb::Preset>(i))) == expected[i]);
}

TEST_CASE("unknown names fall back to the defaults, strict parse rejects them") {
    imb::Ease e = imb::Ease::OutBounce;
    CHECK_FALSE(imforge::TryEaseFromName("Wobble", &e));
    CHECK(e == imb::Ease::OutBounce);
    CHECK_FALSE(imforge::TryEaseFromName(nullptr, &e));
    CHECK_FALSE(imforge::TryEaseFromName("Linear", nullptr));
    CHECK(imforge::EaseFromName("Wobble") == imb::Ease::OutCubic);
    CHECK(imforge::EaseFromName(nullptr) == imb::Ease::OutCubic);
    CHECK(imforge::EaseFromName("linear") == imb::Ease::OutCubic);
    CHECK(imforge::EaseFromName("Spring") == imb::Ease::OutCubic);
    CHECK(imforge::IsSpringEaseName("Spring"));
    CHECK_FALSE(imforge::IsSpringEaseName("OutCubic"));
    CHECK_FALSE(imforge::IsSpringEaseName(nullptr));

    imb::Preset p = imb::Preset::ScalePop;
    CHECK_FALSE(imforge::TryPresetFromName("Zoom", &p));
    CHECK(p == imb::Preset::ScalePop);
    CHECK(imforge::PresetFromName("Zoom") == imb::Preset::None);
    CHECK(imforge::PresetFromName("") == imb::Preset::None);
    CHECK(imforge::PresetFromName(nullptr) == imb::Preset::None);
}

TEST_CASE("transition trigger names round-trip") {
    const char* expected[] = {"hover", "active", "open", "visible", "focus", "disabled"};
    for (int i = 0; i < imforge::kTransitionTriggerCount; ++i) {
        CHECK(std::string(imforge::TransitionTriggerName(i)) == expected[i]);
        CHECK(imforge::TransitionTriggerIndex(expected[i]) == i);
    }
    CHECK(imforge::TransitionTriggerIndex("pressed") == -1);
    CHECK(imforge::TransitionTriggerIndex(nullptr) == -1);
}
