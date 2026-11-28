#include "krc/display/text_display.h"

#include <algorithm>
#include <cassert>
#include <stdexcept>
#include <utility>

namespace krc::display {
namespace {

constexpr std::uint16_t kCellBackspace = 0x08;
constexpr std::uint16_t kCellTab = 0x09;
constexpr std::uint16_t kCellLineFeed = 0x0A;
constexpr std::uint16_t kCellCarriageReturn = 0x0D;

constexpr std::uint16_t kTabWidth = 8;

// A display larger than this is a configuration mistake rather than a screen: the cells are
// host memory and nothing in the ABI promises a bound beyond the 16-bit geometry fields.
constexpr std::uint32_t kMaximumCells = 4'000'000u;

}  // namespace

TextDisplay::TextDisplay()
    : TextDisplay(kDefaultRows, kDefaultCols, kDefaultForeground, kDefaultBackground) {}

TextDisplay::TextDisplay(std::uint16_t rows, std::uint16_t cols, std::uint32_t foreground,
                         std::uint32_t background)
    : rows_(rows),
      cols_(cols),
      default_foreground_(foreground),
      default_background_(background),
      foreground_(foreground),
      background_(background) {
    if (rows == 0 || cols == 0) {
        throw std::invalid_argument("TextDisplay: rows and cols must be non-zero");
    }
    if (rows > 32767 || cols > 32767) {
        throw std::invalid_argument("TextDisplay: rows and cols must fit a 16-bit signed result");
    }
    if (static_cast<std::uint32_t>(rows) * static_cast<std::uint32_t>(cols) > kMaximumCells) {
        throw std::invalid_argument("TextDisplay: the screen exceeds 4,000,000 cells");
    }
    cells_.resize(static_cast<std::size_t>(rows) * cols);
    for (Cell& cell : cells_) {
        cell = Cell{0, foreground_, background_};
    }
}

Info TextDisplay::info() const noexcept {
    return Info{kAbiVersion, rows_, cols_};
}

const Cell& TextDisplay::cell(std::uint16_t row, std::uint16_t col) const {
    assert(row < rows_ && col < cols_);
    return cells_[index(row, col)];
}

void TextDisplay::clear() {
    for (Cell& cell : cells_) {
        cell = Cell{0, foreground_, background_};
    }
}

void TextDisplay::reset() {
    foreground_ = default_foreground_;
    background_ = default_background_;
    cursor_row_ = 0;
    cursor_col_ = 0;
    cursor_visible_ = true;
    keys_.clear();
    clicks_.clear();
    editor_active_ = false;
    line_.clear();
    caret_ = 0;
    editor_row_ = 0;
    editor_col_ = 0;
    editor_limit_ = 0;
    echoed_ = 0;
    completed_line_.reset();
    input_closed_ = false;
    clear();
}

void TextDisplay::set_color(std::uint32_t foreground, std::uint32_t background) noexcept {
    foreground_ = foreground;
    background_ = background;
}

void TextDisplay::scroll() {
    for (std::uint16_t row = 1; row < rows_; ++row) {
        const auto source = cells_.begin() + static_cast<std::ptrdiff_t>(index(row, 0));
        const auto destination = cells_.begin() + static_cast<std::ptrdiff_t>(index(row - 1, 0));
        std::copy_n(source, cols_, destination);
    }
    fill_row(static_cast<std::uint16_t>(rows_ - 1));
}

void TextDisplay::fill_row(std::uint16_t row) {
    for (std::uint16_t col = 0; col < cols_; ++col) {
        cells_[index(row, col)] = Cell{0, foreground_, background_};
    }
}

void TextDisplay::line_feed() {
    ++cursor_row_;
    if (cursor_row_ >= rows_) {
        scroll();
        cursor_row_ = static_cast<std::uint16_t>(rows_ - 1);
    }
}

void TextDisplay::advance() {
    ++cursor_col_;
    if (cursor_col_ >= cols_) {
        cursor_col_ = 0;
        line_feed();
    }
}

void TextDisplay::put(std::uint16_t code) {
    switch (code) {
    case kCellBackspace:
        if (cursor_col_ > 0) --cursor_col_;
        return;
    case kCellTab: {
        const auto next = static_cast<std::uint16_t>((cursor_col_ / kTabWidth + 1) * kTabWidth);
        if (next >= cols_) {
            cursor_col_ = 0;
            line_feed();
        } else {
            cursor_col_ = next;
        }
        return;
    }
    case kCellLineFeed:
        cursor_col_ = 0;
        line_feed();
        return;
    case kCellCarriageReturn:
        cursor_col_ = 0;
        return;
    default:
        cells_[index(cursor_row_, cursor_col_)] = Cell{code, foreground_, background_};
        advance();
        return;
    }
}

void TextDisplay::put_string(std::string_view text) {
    for (const char byte : text) {
        put(static_cast<std::uint16_t>(static_cast<unsigned char>(byte)));
    }
}

void TextDisplay::set_cursor(std::uint16_t row, std::uint16_t col) noexcept {
    cursor_row_ = row < rows_ ? row : static_cast<std::uint16_t>(rows_ - 1);
    cursor_col_ = col < cols_ ? col : static_cast<std::uint16_t>(cols_ - 1);
}

void TextDisplay::push_key(std::uint16_t code) {
    if (input_closed_) return;
    if (editor_active_) {
        handle_editor_key(code);
        return;
    }
    keys_.push_back(code);
}

void TextDisplay::push_text(std::string_view text) {
    for (const char byte : text) {
        const auto value = static_cast<std::uint16_t>(static_cast<unsigned char>(byte));
        if (value == static_cast<std::uint16_t>('\r')) continue;
        if (value == static_cast<std::uint16_t>('\n')) {
            push_key(kKeyEnter);
            continue;
        }
        push_key(value);
    }
}

void TextDisplay::push_click(std::uint16_t row, std::uint16_t col, std::uint16_t buttons) {
    clicks_.push_back(Click{row, col, buttons});
}

void TextDisplay::close_input() noexcept {
    if (input_closed_) return;
    input_closed_ = true;
    if (!editor_active_) return;
    if (line_.empty()) {
        cancel_editor_line();
    } else {
        // End of input with a partial line: deliver it, exactly as a terminal delivers the
        // last unterminated line at end of file. The echo stays and the cursor moves on.
        complete_editor_line();
    }
}

int TextDisplay::get_key() {
    if (keys_.empty()) return kStatusNotReady;
    const int code = keys_.front();
    keys_.pop_front();
    return code;
}

bool TextDisplay::take_click(Click& click) {
    if (clicks_.empty()) return false;
    click = clicks_.front();
    clicks_.pop_front();
    return true;
}

void TextDisplay::arm_line_editor(std::uint16_t capacity) {
    if (!editor_active_) {
        editor_active_ = true;
        editor_row_ = cursor_row_;
        editor_col_ = cursor_col_;
        line_.clear();
        caret_ = 0;
        editor_limit_ = 0;
        echoed_ = 0;
    }
    // One byte is reserved for the NUL the guest wrapper appends when there is room. A
    // smaller capacity on a later poll does not truncate the line the guest already has: the
    // line is delivered only when the caller's buffer can hold it (docs/display-abi.md).
    const std::size_t from_capacity = capacity > 0 ? capacity - 1u : 0u;
    const std::size_t on_row = static_cast<std::size_t>(cols_ - editor_col_);
    editor_limit_ = std::min(from_capacity, on_row);
    if (caret_ > line_.size()) caret_ = line_.size();
    render_editor_line();
    place_editor_cursor();
}

std::size_t TextDisplay::pending_line_length() const noexcept {
    return completed_line_.has_value() ? completed_line_->size() : 0u;
}

std::optional<std::string> TextDisplay::take_line() {
    if (!completed_line_.has_value()) return std::nullopt;
    std::optional<std::string> line = std::move(completed_line_);
    completed_line_.reset();
    return line;
}

void TextDisplay::handle_editor_key(std::uint16_t code) {
    switch (code) {
    case kKeyEnter:
        complete_editor_line();
        return;
    case kKeyEscape:
        cancel_editor_line();
        return;
    case kKeyBackspace:
        if (caret_ > 0) {
            line_.erase(caret_ - 1, 1);
            --caret_;
            render_editor_line();
            place_editor_cursor();
        }
        return;
    case kKeyLeft:
        if (caret_ > 0) --caret_;
        place_editor_cursor();
        return;
    case kKeyRight:
        if (caret_ < line_.size()) ++caret_;
        place_editor_cursor();
        return;
    default:
        // 8-bit ASCII printable codes are inserted; every other key is ignored by the
        // editor and is not queued for GET_KEY.
        if (code >= 0x20 && code < 0x7F && line_.size() < editor_limit_) {
            line_.insert(caret_, 1, static_cast<char>(code));
            ++caret_;
            render_editor_line();
            place_editor_cursor();
        }
        return;
    }
}

void TextDisplay::render_editor_line() {
    const std::size_t span = std::max(echoed_, line_.size());
    for (std::size_t offset = 0; offset < span; ++offset) {
        const auto col = static_cast<std::uint16_t>(editor_col_ + offset);
        if (col >= cols_) break;
        const auto code = offset < line_.size()
                              ? static_cast<std::uint16_t>(
                                    static_cast<unsigned char>(line_[offset]))
                              : static_cast<std::uint16_t>(0);
        cells_[index(editor_row_, col)] = Cell{code, foreground_, background_};
    }
    echoed_ = line_.size();
}

void TextDisplay::place_editor_cursor() {
    cursor_row_ = editor_row_;
    const auto col = static_cast<std::size_t>(editor_col_) + caret_;
    cursor_col_ = col < cols_ ? static_cast<std::uint16_t>(col)
                              : static_cast<std::uint16_t>(cols_ - 1);
}

void TextDisplay::complete_editor_line() {
    completed_line_ = line_;
    editor_active_ = false;
    line_.clear();
    caret_ = 0;
    echoed_ = 0;
    editor_limit_ = 0;
    // The typed line stays on screen and the cursor moves to the start of the next row.
    ++editor_row_;
    if (editor_row_ >= rows_) {
        scroll();
        editor_row_ = static_cast<std::uint16_t>(rows_ - 1);
    }
    cursor_row_ = editor_row_;
    cursor_col_ = 0;
}

void TextDisplay::cancel_editor_line() {
    for (std::size_t offset = 0; offset < echoed_; ++offset) {
        const auto col = static_cast<std::uint16_t>(editor_col_ + offset);
        if (col >= cols_) break;
        cells_[index(editor_row_, col)] = Cell{0, foreground_, background_};
    }
    editor_active_ = false;
    line_.clear();
    caret_ = 0;
    editor_limit_ = 0;
    echoed_ = 0;
    cursor_row_ = editor_row_;
    cursor_col_ = editor_col_;
}

}  // namespace krc::display
