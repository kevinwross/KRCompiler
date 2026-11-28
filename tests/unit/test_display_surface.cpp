#include <cstdint>
#include <vector>

#include "doctest.h"
#include "krc/display/surface.h"
#include "krc/display/text_display.h"

using krc::display::kDefaultBackground;
using krc::display::kDefaultForeground;
using krc::display::Surface;
using krc::display::TextDisplay;

namespace {

// A two-pixel-wide synthetic glyph: code 1 draws its top-left pixel, code 2 the bottom-right,
// and everything else is blank. Enough to prove composition and clipping without font data.
class TestGlyphs final : public krc::display::GlyphSource {
public:
    [[nodiscard]] std::uint32_t cell_width() const noexcept override { return 2; }
    [[nodiscard]] std::uint32_t cell_height() const noexcept override { return 2; }
    void draw(Surface& surface, std::uint16_t code, std::int32_t x, std::int32_t y,
              std::uint32_t foreground) const override {
        if (code == 1) surface.put_pixel(x, y, foreground);
        if (code == 2) surface.put_pixel(x + 1, y + 1, foreground);
    }
};

constexpr std::uint32_t kRed = 0x00FF0000u;
constexpr std::uint32_t kBlue = 0x000000FFu;

}  // namespace

TEST_CASE("a surface starts without a mode") {
    Surface surface;
    CHECK_FALSE(surface.has_mode());
    CHECK(surface.width() == 0);
    CHECK(surface.height() == 0);
    // Drawing without a mode is a no-op rather than a crash.
    surface.put_pixel(0, 0, kRed);
    surface.clear(kBlue);
    surface.flip();
    CHECK(surface.frame_hash() != 0);
}

TEST_CASE("mode validation rejects impossible modes and keeps the old one") {
    CHECK_FALSE(Surface::mode_valid(0, 10));
    CHECK_FALSE(Surface::mode_valid(10, 0));
    CHECK_FALSE(Surface::mode_valid(Surface::kMaxDimension + 1, 1));
    CHECK_FALSE(Surface::mode_valid(4001, 4000));
    CHECK(Surface::mode_valid(4000, 4000));
    CHECK(Surface::mode_valid(640, 480));

    Surface surface;
    REQUIRE(surface.set_mode(4, 3));
    CHECK(surface.width() == 4);
    CHECK(surface.height() == 3);
    CHECK(surface.stride() == 4);
    CHECK_FALSE(surface.set_mode(0, 0));
    CHECK(surface.width() == 4);
    CHECK(surface.height() == 3);
}

TEST_CASE("a set mode starts black in both buffers") {
    Surface surface;
    REQUIRE(surface.set_mode(4, 3));
    CHECK(surface.back()[0] == 0);
    CHECK(surface.front()[0] == 0);
    for (std::uint32_t index = 0; index < 12; ++index) {
        CHECK(surface.front()[index] == 0);
    }
}

TEST_CASE("drawing writes the back buffer and ignores the high colour byte") {
    Surface surface;
    REQUIRE(surface.set_mode(4, 4));
    surface.put_pixel(1, 1, 0xABFF0000u);
    CHECK(surface.back()[1 * 4 + 1] == kRed);
    surface.put_pixel(-1, 0, kRed);
    surface.put_pixel(4, 0, kRed);
    surface.put_pixel(0, -1, kRed);
    surface.put_pixel(0, 4, kRed);
    std::size_t painted = 0;
    for (std::uint32_t index = 0; index < 16; ++index) {
        if (surface.back()[index] != 0) ++painted;
    }
    CHECK(painted == 1);
}

TEST_CASE("fill_rect clips to the surface") {
    Surface surface;
    REQUIRE(surface.set_mode(4, 4));
    surface.fill_rect(-2, -2, 4, 4, kRed);
    CHECK(surface.back()[0] == kRed);
    CHECK(surface.back()[1 * 4 + 1] == kRed);
    CHECK(surface.back()[2 * 4 + 2] == 0);
    surface.fill_rect(2, 2, 100, 100, kBlue);
    CHECK(surface.back()[2 * 4 + 2] == kBlue);
    CHECK(surface.back()[3 * 4 + 3] == kBlue);
    CHECK(surface.back()[3 * 4 + 1] == 0);
}

TEST_CASE("flip commits the back buffer and the frame hash covers the front buffer") {
    Surface surface;
    REQUIRE(surface.set_mode(4, 4));
    const std::uint64_t initial = surface.frame_hash();
    surface.put_pixel(0, 0, kRed);
    CHECK(surface.frame_hash() == initial);  // not committed yet
    surface.flip();
    const std::uint64_t committed = surface.frame_hash();
    CHECK(committed != initial);
    CHECK(surface.front()[0] == kRed);

    surface.put_pixel(1, 1, kBlue);
    CHECK(surface.frame_hash() == committed);
    surface.flip();
    CHECK(surface.frame_hash() != committed);
}

TEST_CASE("the frame hash is stable for equal committed frames") {
    Surface first;
    Surface second;
    REQUIRE(first.set_mode(8, 2));
    REQUIRE(second.set_mode(8, 2));
    CHECK(first.frame_hash() == second.frame_hash());
    first.put_pixel(3, 1, kRed);
    first.flip();
    second.put_pixel(3, 1, kRed);
    second.flip();
    CHECK(first.frame_hash() == second.frame_hash());

    // Geometry participates: the same pixels at a different size hash differently.
    Surface third;
    REQUIRE(third.set_mode(2, 8));
    CHECK(third.frame_hash() != first.frame_hash());
}

TEST_CASE("compose_text lays cells out at the glyph size") {
    Surface surface;
    REQUIRE(surface.set_mode(4, 4));
    TextDisplay display(2, 2, 0x00FFFFFFu, 0x000000FFu);
    display.put(1);  // cell (0, 0)
    display.put(2);  // cell (0, 1)
    TestGlyphs glyphs;
    surface.compose_text(display, glyphs);
    CHECK(surface.back()[0 * 4 + 0] == 0x00FFFFFFu);  // code 1 at (0, 0)
    CHECK(surface.back()[0 * 4 + 1] == 0x000000FFu);  // rest of the cell is background
    CHECK(surface.back()[0 * 4 + 2] == 0x000000FFu);  // cell (0, 1) background
    CHECK(surface.back()[1 * 4 + 3] == 0x00FFFFFFu);  // code 2 bottom-right at (3, 1)
    CHECK(surface.back()[2 * 4 + 0] == 0x000000FFu);  // second row is all background
}

TEST_CASE("compose_text clips at the surface edge") {
    Surface surface;
    REQUIRE(surface.set_mode(3, 3));
    TextDisplay display(10, 10);
    display.put(1);
    TestGlyphs glyphs;
    surface.compose_text(display, glyphs, 2, 2);
    CHECK(surface.back()[2 * 3 + 2] == 0x00FFFFFFu);  // glyph at (2, 2), inside
    CHECK(surface.frame_hash() != 0);                 // no crash on the clipped rest
}
