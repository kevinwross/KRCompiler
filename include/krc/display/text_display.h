#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "krc/display/display_abi.h"

namespace krc::display {

// The host text display behind the common display ABI. It owns a character grid and the input
// queues; a VM front end maps its hypercall surface onto DisplayService, and a front end
// renders the cells. The screen is host state: the guest never has screen memory and no
// command returns a cell.
//
// A cell is a 16-bit code plus XRGB8888 foreground and background; the high byte of a colour
// is stored and not interpreted. Cell codes are 16-bit; guest strings are 8-bit ASCII today.
class TextDisplay {
public:
    // A display with the default geometry and colours.
    TextDisplay();
    // rows and cols must be 1-32767 and the screen may not exceed 4,000,000 cells; either
    // violation throws std::invalid_argument. Colours are stored verbatim.
    TextDisplay(std::uint16_t rows, std::uint16_t cols,
                std::uint32_t foreground = kDefaultForeground,
                std::uint32_t background = kDefaultBackground);

    // ---- geometry and cells ---------------------------------------------------------
    [[nodiscard]] std::uint16_t rows() const noexcept { return rows_; }
    [[nodiscard]] std::uint16_t cols() const noexcept { return cols_; }
    [[nodiscard]] Info info() const noexcept;
    // row and col must be inside the screen. The reference stays valid for the object's life.
    [[nodiscard]] const Cell& cell(std::uint16_t row, std::uint16_t col) const;

    // ---- screen and colours ---------------------------------------------------------
    // Fills every cell with the current colours and code 0. The cursor and the input queues
    // are left alone; reset() is the full device reset.
    void clear();
    // Clears the screen back to the configured default colours, homes and shows the cursor,
    // and drops the input queues, the line editor and any completed line.
    void reset();

    // The colours used for new text and for clear()/scrolling. The high byte is stored but
    // not interpreted.
    void set_color(std::uint32_t foreground, std::uint32_t background) noexcept;
    [[nodiscard]] std::uint32_t foreground() const noexcept { return foreground_; }
    [[nodiscard]] std::uint32_t background() const noexcept { return background_; }

    // Writes one cell code at the cursor and advances it. 0x08, 0x09, 0x0A and 0x0D are
    // control codes (backspace, tab, newline, carriage return) and are not stored. A write
    // past the last column wraps; a write past the last row scrolls the screen up one row.
    void put(std::uint16_t code);
    // Writes each byte as a 16-bit cell code. Bytes are 8-bit ASCII today.
    void put_string(std::string_view text);

    // ---- cursor ---------------------------------------------------------------------
    // Requests outside the screen are clamped to its last row or column.
    void set_cursor(std::uint16_t row, std::uint16_t col) noexcept;
    void set_cursor_visible(bool visible) noexcept { cursor_visible_ = visible; }
    [[nodiscard]] std::uint16_t cursor_row() const noexcept { return cursor_row_; }
    [[nodiscard]] std::uint16_t cursor_col() const noexcept { return cursor_col_; }
    [[nodiscard]] bool cursor_visible() const noexcept { return cursor_visible_; }

    // ---- host input -----------------------------------------------------------------
    // Routes a key to the line editor while one is active and to the raw key queue
    // otherwise. While a line editor is active the editor consumes every key, so a key the
    // editor ignores is not queued for GET_KEY.
    void push_key(std::uint16_t code);
    // Queues each byte as a key. Line feeds act as Enter and carriage returns are dropped;
    // the bytes are otherwise queued or edited exactly as push_key() would.
    void push_text(std::string_view text);
    // Queues a click event; row and col are cell coordinates and buttons is the mask above.
    void push_click(std::uint16_t row, std::uint16_t col, std::uint16_t buttons);

    // Pops the oldest queued key, or kStatusNotReady when the queue is empty.
    [[nodiscard]] int get_key();
    // Pops the oldest queued click; returns false when the queue is empty.
    bool take_click(Click& click);

    // ---- host line editor -----------------------------------------------------------
    // READ_LINE arms the editor: typing is echoed at the cursor position where the guest
    // first asked, Enter completes the line and Escape cancels it. The guest receives only
    // the completed line; the editor never writes guest memory. A smaller capacity on a
    // later poll does not truncate an existing line; the completed line is delivered only
    // when the caller's buffer holds it.
    void arm_line_editor(std::uint16_t capacity);
    [[nodiscard]] bool editor_active() const noexcept { return editor_active_; }
    [[nodiscard]] bool line_ready() const noexcept { return completed_line_.has_value(); }
    // The byte length of the completed line, or 0 when no line is ready.
    [[nodiscard]] std::size_t pending_line_length() const noexcept;
    // Returns the completed line and clears the ready state; empty optional when none is
    // ready.
    [[nodiscard]] std::optional<std::string> take_line();

private:
    [[nodiscard]] std::size_t index(std::uint16_t row, std::uint16_t col) const noexcept {
        return static_cast<std::size_t>(row) * cols_ + col;
    }
    void scroll();
    void line_feed();
    void advance();
    void fill_row(std::uint16_t row);
    void handle_editor_key(std::uint16_t code);
    void render_editor_line();
    void place_editor_cursor();
    void complete_editor_line();
    void cancel_editor_line();

    std::uint16_t rows_;
    std::uint16_t cols_;
    std::uint32_t default_foreground_;
    std::uint32_t default_background_;
    std::uint32_t foreground_;
    std::uint32_t background_;
    std::vector<Cell> cells_;

    std::uint16_t cursor_row_ = 0;
    std::uint16_t cursor_col_ = 0;
    bool cursor_visible_ = true;

    std::deque<std::uint16_t> keys_;
    std::deque<Click> clicks_;

    bool editor_active_ = false;
    std::string line_;
    std::size_t caret_ = 0;
    std::uint16_t editor_row_ = 0;
    std::uint16_t editor_col_ = 0;
    std::size_t editor_limit_ = 0;
    std::size_t echoed_ = 0;
    std::optional<std::string> completed_line_;
};

}  // namespace krc::display
