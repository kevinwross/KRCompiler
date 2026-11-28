#pragma once

#include <cstdint>
#include <vector>

#include "krc/display/display_abi.h"
#include "krc/display/text_display.h"

namespace krc::display {

class Surface;

// A glyph source for text-overlay composition. The package draws no font of its own: the
// consumer supplies one (a debugger's mono font, a test's synthetic glyphs), so the package
// stays free of font data and the renderer stays with the front end.
class GlyphSource {
public:
    virtual ~GlyphSource() = default;
    [[nodiscard]] virtual std::uint32_t cell_width() const noexcept = 0;
    [[nodiscard]] virtual std::uint32_t cell_height() const noexcept = 0;
    // Sets the glyph's pixels through surface.put_pixel(); x and y are the cell's top-left
    // corner and may be outside the surface, which clips.
    virtual void draw(Surface& surface, std::uint16_t code, std::int32_t x, std::int32_t y,
                      std::uint32_t foreground) const = 0;
};

// The framebuffer core for 32-bit-or-larger VMs. It owns XRGB8888 storage, a back buffer the
// VM draws into and a front buffer a presenter reads; flip() commits. Nothing adopts it yet:
// the 16-bit machines are text-only, so this is the API and its tests.
//
// set_mode() validates the mode, allocates both buffers and clears them to black. Drawing
// calls clip; the high byte of a colour is not interpreted and is stored as zero.
class Surface {
public:
    static constexpr std::uint32_t kMaxDimension = 4096;
    static constexpr std::uint64_t kMaxPixels = 16'000'000;

    Surface() = default;

    [[nodiscard]] static bool mode_valid(std::uint32_t width, std::uint32_t height) noexcept;

    // Sets the mode. A rejected mode returns false and leaves the surface unchanged (the
    // default surface has no mode).
    bool set_mode(std::uint32_t width, std::uint32_t height);
    [[nodiscard]] std::uint32_t width() const noexcept { return width_; }
    [[nodiscard]] std::uint32_t height() const noexcept { return height_; }
    // Pixels per row; the bytes per row are four times this.
    [[nodiscard]] std::uint32_t stride() const noexcept { return width_; }
    [[nodiscard]] bool has_mode() const noexcept { return width_ != 0 && height_ != 0; }

    // The buffer the VM draws into.
    [[nodiscard]] std::uint32_t* back() noexcept { return back_.data(); }
    [[nodiscard]] const std::uint32_t* back() const noexcept { return back_.data(); }
    // The committed buffer a presenter reads.
    [[nodiscard]] const std::uint32_t* front() const noexcept { return front_.data(); }

    // Drawing on the back buffer. A no-mode surface ignores every drawing call.
    void clear(std::uint32_t color);
    void put_pixel(std::int32_t x, std::int32_t y, std::uint32_t color);
    void fill_rect(std::int32_t x, std::int32_t y, std::uint32_t width, std::uint32_t height,
                   std::uint32_t color);

    // Commits the back buffer to the front buffer.
    void flip();

    // A stable hash of the committed frame: geometry and every front pixel, byte by byte, so
    // the answer does not depend on host endianness. Two surfaces with the same committed
    // pixels have the same hash.
    [[nodiscard]] std::uint64_t frame_hash() const;

    // Composes a text display into the back buffer at cell width/height from the glyph source;
    // each cell's background fills its rectangle and the glyph draws on top. Clipped.
    void compose_text(const TextDisplay& display, const GlyphSource& glyphs,
                      std::int32_t origin_x = 0, std::int32_t origin_y = 0);

private:
    [[nodiscard]] bool inside(std::int32_t x, std::int32_t y) const noexcept {
        return x >= 0 && y >= 0 && static_cast<std::uint32_t>(x) < width_ &&
               static_cast<std::uint32_t>(y) < height_;
    }

    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    std::vector<std::uint32_t> front_;
    std::vector<std::uint32_t> back_;
};

}  // namespace krc::display
