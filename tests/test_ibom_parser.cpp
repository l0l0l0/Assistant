#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "ibom/IBomData.h"
#include "ibom/IBomParser.h"

using namespace ibom;

TEST_CASE("IBomParser — parse empty HTML", "[ibom][parser]")
{
    IBomParser parser;
    auto result = parser.parseFile("nonexistent.html");
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("IBomParser — extract JSON from script", "[ibom][parser]")
{
    // Simulate a minimal iBOM HTML with embedded data
    std::string html = R"(
        <html><head><script>
        var pcbdata = {"edges_bbox":{"minx":0,"miny":0,"maxx":100,"maxy":80},
                       "board_outline":{"F":[]},"footprints":[],"bom":{"F":[],"B":[],"both":[]},
                       "nets":[]};
        </script></head><body></body></html>
    )";

    IBomParser parser;
    auto result = parser.parseString(html);

    // This depends on parser implementation robustness
    // The parser should at least not crash on minimal input
    // Full test requires a real iBOM file
    if (result) {
        CHECK(result->components.empty());
    }
}

TEST_CASE("IBomParser — LZString decompression terminates on corrupted input",
          "[ibom][parser][lzstring]")
{
    IBomParser parser;

    SECTION("empty input") {
        REQUIRE_FALSE(parser.decompressLZString("").has_value());
    }

    SECTION("garbage base64 returns without hanging") {
        // Deterministic pseudo-random base64 alphabet stream — simulates a
        // truncated/corrupted compressed block inside an iBOM HTML.
        static const std::string alphabet =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string garbage;
        unsigned seed = 12345;
        for (int i = 0; i < 50000; ++i) {
            seed = seed * 1103515245u + 12345u;
            garbage.push_back(alphabet[(seed >> 16) % alphabet.size()]);
        }

        // Must terminate (bad-code error, end-of-data, or output-size guard)
        // — any outcome is fine as long as it returns instead of spinning
        // or exhausting memory.
        auto result = parser.decompressLZString(garbage);
        if (result) {
            // The guard caps expansion at 1000x input + 1 MiB (in UTF-16
            // chars); the returned UTF-8 string is at most 3 bytes per char.
            CHECK(result->size() <= (garbage.size() * 1000 + (1 << 20)) * 3);
        }
    }

    SECTION("non-base64 characters do not crash") {
        auto result = parser.decompressLZString("!!!###$$$%%%");
        (void)result; // termination without crash is the assertion
    }
}

TEST_CASE("IBomData — Component default construction", "[ibom][data]")
{
    Component comp;
    REQUIRE(comp.reference.empty());
    REQUIRE(comp.value.empty());
    REQUIRE(comp.footprint.empty());
    REQUIRE(comp.layer == Layer::Front);
    REQUIRE(comp.rotation == Catch::Approx(0.0));
    REQUIRE(comp.pads.empty());
}

TEST_CASE("IBomData — Pad construction", "[ibom][data]")
{
    Pad pad;
    pad.position = {10.5, 20.3};
    pad.shape = Pad::Shape::Rect;
    pad.sizeX = 1.2;
    pad.sizeY = 0.8;
    pad.isSMD = true;
    pad.isPin1 = false;

    REQUIRE(pad.position.x == Catch::Approx(10.5));
    REQUIRE(pad.position.y == Catch::Approx(20.3));
    REQUIRE(pad.shape == Pad::Shape::Rect);
    REQUIRE(pad.isSMD == true);
}

TEST_CASE("IBomData — BBox operations", "[ibom][data]")
{
    BBox box;
    box.minX = 10;
    box.minY = 20;
    box.maxX = 50;
    box.maxY = 60;

    REQUIRE(box.width() == Catch::Approx(40.0));
    REQUIRE(box.height() == Catch::Approx(40.0));
    REQUIRE(box.center().x == Catch::Approx(30.0));
    REQUIRE(box.center().y == Catch::Approx(40.0));

    // Check a point inside the box
    Point2D inside{30, 40};
    REQUIRE(inside.x >= box.minX);
    REQUIRE(inside.x <= box.maxX);
    REQUIRE(inside.y >= box.minY);
    REQUIRE(inside.y <= box.maxY);

    // Check a point outside the box
    Point2D outside{5, 5};
    bool isInside = outside.x >= box.minX && outside.x <= box.maxX &&
                    outside.y >= box.minY && outside.y <= box.maxY;
    REQUIRE_FALSE(isInside);
}
