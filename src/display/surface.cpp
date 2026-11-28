#include "krc/display/surface.h"

#include <algorithm>
#include <cstddef>

namespace krc::display {
namespace {

constexpr std::uint32_t kColorMask = 0x00FFFFFFu;
constexpr std::uint64_t kFnvOffset = 14695981039346656037ull;
constexpr std::uint64_t kFnvPrime = 1099511628211ull;

void hash_byte(std::uint64_t& hash, std::uint8_t byte) {
    hash ^= byte;
    hash *= kFnvPrime;
}

void hash_word(std::uint64_t& hash, std::uint32_t word) {
    hash_byte(hash, static_cast<std::uint8_t>(word & 0xFFu));
    hash_byte(hash, static_cast<std::uint8_t>((word >> 8) & 0xFFu));
    hash_byte(hash, static_cast<std::uint8_t>((word >> 16) & 0xFFu));
    hash_byte(hash, static_cast<std::uint8_t>((word >> 24) & 0xFFu));
}

}  // namespace

bool Surface::mode_valid(std::uint32_t width, std::uint32_t height) noexcept {
    if (width == 0 || height == 0) return false;
    if (width > kMaxDimension || height > kMaxDimension) return false;
    return static_cast<std::uint64_t>(width) * height <= kMaxPixels;
}

bool Surface::set_mode(std::uint32_t width, std::uint32_t height) {
    if (!mode_valid(width, height)) return false;
    width_ = width;
    height_ = height;
    const auto pixels = static_cast<std::size_t>(width) * height;
    constexpr std::uint32_t kBlack = 0x00000000u;
    front_.assign(pixels, kBlack);
    back_.assign(pixels, kBlack);
    return true;
}

void Surface::clear(std::uint32_t color) {
    if (!has_mode()) return;
    std::fill(back_.begin(), back_.end(), color & kColorMask);
}

void Surface::put_pixel(std::int32_t x, std::int32_t y, std::uint32_t color) {
    if (!inside(x, y)) return;
    back_[static_cast<std::size_t>(y) * width_ + static_cast<std::uint32_t>(x)] =
        color & kColorMask;
}

void Surface::fill_rect(std::int32_t x, std::int32_t y, std::uint32_t width,
                        std::uint32_t height, std::uint32_t color) {
    if (!has_mode() || width == 0 || height == 0) return;
    const std::int64_t left = std::max<std::int64_t>(0, x);
    const std::int64_t top = std::max<std::int64_t>(0, y);
    const std::int64_t right =
        std::min<std::int64_t>(width_, static_cast<std::int64_t>(x) + width);
    const std::int64_t bottom =
        std::min<std::int64_t>(height_, static_cast<std::int64_t>(y) + height);
    if (left >= right || top >= bottom) return;
    const std::uint32_t pixel = color & kColorMask;
    for (std::int64_t row = top; row < bottom; ++row) {
        auto* line = back_.data() + static_cast<std::size_t>(row) * width_;
        std::fill(line + left, line + right, pixel);
    }
}

void Surface::flip() { front_ = back_; }

std::uint64_t Surface::frame_hash() const {
    std::uint64_t hash = kFnvOffset;
    hash_word(hash, width_);
    hash_word(hash, height_);
    for (const std::uint32_t pixel : front_) {
        hash_word(hash, pixel);
    }
    return hash;
}

void Surface::compose_text(const TextDisplay& display, const GlyphSource& glyphs,
                           std::int32_t origin_x, std::int32_t origin_y) {
    if (!has_mode()) return;
    const std::uint32_t cell_width = glyphs.cell_width();
    const std::uint32_t cell_height = glyphs.cell_height();
    if (cell_width == 0 || cell_height == 0) return;
    for (std::uint16_t row = 0; row < display.rows(); ++row) {
        for (std::uint16_t col = 0; col < display.cols(); ++col) {
            const Cell& cell = display.cell(row, col);
            const auto x = static_cast<std::int32_t>(
                static_cast<std::int64_t>(origin_x) + static_cast<std::int64_t>(col) * cell_width);
            const auto y = static_cast<std::int32_t>(
                static_cast<std::int64_t>(origin_y) + static_cast<std::int64_t>(row) * cell_height);
            fill_rect(x, y, cell_width, cell_height, cell.background);
            glyphs.draw(*this, cell.code, x, y, cell.foreground);
        }
    }
}

}  // namespace krc::display
