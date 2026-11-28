#include <stdexcept>

#include "doctest.h"
#include "krc/display/text_display.h"

using krc::display::Cell;
using krc::display::kDefaultBackground;
using krc::display::kDefaultCols;
using krc::display::kDefaultForeground;
using krc::display::kDefaultRows;
using krc::display::kKeyBackspace;
using krc::display::kKeyEscape;
using krc::display::kKeyLeft;
using krc::display::kKeyRight;
using krc::display::kStatusNotReady;
using krc::display::TextDisplay;

TEST_CASE("the default display is 80x30 white on black") {
    TextDisplay display;
    CHECK(display.rows() == kDefaultRows);
    CHECK(display.cols() == kDefaultCols);
    CHECK(display.foreground() == kDefaultForeground);
    CHECK(display.background() == kDefaultBackground);
    CHECK(display.cursor_row() == 0);
    CHECK(display.cursor_col() == 0);
    CHECK(display.cursor_visible());
    CHECK(display.cell(0, 0).code == 0);
    CHECK(display.cell(0, 0).foreground == kDefaultForeground);
    CHECK(display.cell(0, 0).background == kDefaultBackground);
    CHECK(display.info().abi_version == krc::display::kAbiVersion);
    CHECK(display.info().rows == kDefaultRows);
    CHECK(display.info().cols == kDefaultCols);
}

TEST_CASE("geometry is validated") {
    CHECK_THROWS_AS(TextDisplay(0, 80), std::invalid_argument);
    CHECK_THROWS_AS(TextDisplay(30, 0), std::invalid_argument);
    CHECK_THROWS_AS(TextDisplay(40000, 1), std::invalid_argument);
    CHECK_THROWS_AS(TextDisplay(3000, 3000), std::invalid_argument);
}

TEST_CASE("clear paints the current colours and keeps the cursor") {
    TextDisplay display;
    display.put_string("hi");
    display.set_color(0x11223344u, 0x55667788u);
    display.set_cursor(3, 4);
    display.clear();
    CHECK(display.cell(0, 0).code == 0);
    CHECK(display.cell(29, 79).code == 0);
    CHECK(display.cell(12, 34).foreground == 0x11223344u);
    CHECK(display.cell(12, 34).background == 0x55667788u);
    CHECK(display.cursor_row() == 3);
    CHECK(display.cursor_col() == 4);
}

TEST_CASE("put writes a cell, uses the current colours and advances") {
    TextDisplay display;
    display.set_color(0x00112233u, 0x00445566u);
    display.put(0x41);
    CHECK(display.cell(0, 0).code == 0x41);
    CHECK(display.cell(0, 0).foreground == 0x00112233u);
    CHECK(display.cell(0, 0).background == 0x00445566u);
    CHECK(display.cursor_row() == 0);
    CHECK(display.cursor_col() == 1);
}

TEST_CASE("put wraps at the last column and scrolls at the last row") {
    TextDisplay display;
    display.set_cursor(0, 79);
    display.put(0x5A);
    CHECK(display.cell(0, 79).code == 0x5A);
    CHECK(display.cursor_row() == 1);
    CHECK(display.cursor_col() == 0);

    display.set_cursor(29, 0);
    display.put(0x58);
    display.put(0x0A);
    CHECK(display.cell(28, 0).code == 0x58);
    CHECK(display.cell(29, 0).code == 0);
    CHECK(display.cursor_row() == 29);
    CHECK(display.cursor_col() == 0);
}

TEST_CASE("control codes act instead of storing a cell") {
    TextDisplay display;
    display.set_cursor(2, 5);
    display.put(0x0A);
    CHECK(display.cursor_row() == 3);
    CHECK(display.cursor_col() == 0);
    CHECK(display.cell(2, 5).code == 0);

    display.set_cursor(4, 7);
    display.put(0x0D);
    CHECK(display.cursor_row() == 4);
    CHECK(display.cursor_col() == 0);

    display.set_cursor(4, 3);
    display.put(0x08);
    CHECK(display.cursor_col() == 2);

    display.set_cursor(5, 0);
    display.put(0x09);
    CHECK(display.cursor_col() == 8);
}

TEST_CASE("put_string writes bytes and handles newlines") {
    TextDisplay display;
    display.set_cursor(0, 78);
    display.put_string("ABCD");
    CHECK(display.cell(0, 78).code == 'A');
    CHECK(display.cell(0, 79).code == 'B');
    CHECK(display.cell(1, 0).code == 'C');
    CHECK(display.cell(1, 1).code == 'D');
    CHECK(display.cursor_row() == 1);
    CHECK(display.cursor_col() == 2);

    display.set_cursor(5, 0);
    display.put_string("X\nY");
    CHECK(display.cell(5, 0).code == 'X');
    CHECK(display.cell(6, 0).code == 'Y');
}

TEST_CASE("the cursor is clamped and can be hidden") {
    TextDisplay display;
    display.set_cursor(100, 200);
    CHECK(display.cursor_row() == 29);
    CHECK(display.cursor_col() == 79);
    display.set_cursor_visible(false);
    CHECK_FALSE(display.cursor_visible());
}

TEST_CASE("keys and clicks queue and drain") {
    TextDisplay display;
    CHECK(display.get_key() == kStatusNotReady);
    display.push_key(0x41);
    display.push_key(krc::display::kKeyLeft);
    CHECK(display.get_key() == 0x41);
    CHECK(display.get_key() == krc::display::kKeyLeft);
    CHECK(display.get_key() == kStatusNotReady);

    krc::display::Click click;
    CHECK_FALSE(display.take_click(click));
    display.push_click(3, 4, krc::display::kButtonLeft);
    CHECK(display.take_click(click));
    CHECK(click.row == 3);
    CHECK(click.col == 4);
    CHECK(click.buttons == krc::display::kButtonLeft);
    CHECK_FALSE(display.take_click(click));
}

TEST_CASE("the line editor echoes, inserts and completes") {
    TextDisplay display;
    CHECK(display.get_key() == kStatusNotReady);
    display.arm_line_editor(16);
    CHECK(display.editor_active());

    display.push_text("ab");
    CHECK(display.cell(0, 0).code == 'a');
    CHECK(display.cell(0, 1).code == 'b');
    CHECK(display.cursor_col() == 2);

    display.push_key(kKeyLeft);
    CHECK(display.cursor_col() == 1);
    display.push_text("X");
    CHECK(display.cell(0, 0).code == 'a');
    CHECK(display.cell(0, 1).code == 'X');
    CHECK(display.cell(0, 2).code == 'b');
    display.push_key(kKeyRight);
    CHECK(display.cursor_col() == 3);
    display.push_key(kKeyRight);
    CHECK(display.cursor_col() == 3);
    display.push_key(kKeyBackspace);
    CHECK(display.cell(0, 0).code == 'a');
    CHECK(display.cell(0, 1).code == 'X');
    CHECK(display.cell(0, 2).code == 0);

    CHECK_FALSE(display.line_ready());
    display.push_text("\n");
    CHECK_FALSE(display.editor_active());
    CHECK(display.line_ready());
    CHECK(display.pending_line_length() == 2);
    const auto line = display.take_line();
    REQUIRE(line.has_value());
    CHECK(*line == "aX");
    CHECK_FALSE(display.line_ready());
}

TEST_CASE("escape cancels the line and removes its echo") {
    TextDisplay display;
    display.arm_line_editor(8);
    display.push_text("zz");
    CHECK(display.cell(0, 0).code == 'z');
    display.push_key(kKeyEscape);
    CHECK_FALSE(display.editor_active());
    CHECK_FALSE(display.line_ready());
    CHECK(display.cell(0, 0).code == 0);
    CHECK(display.cell(0, 1).code == 0);
}

TEST_CASE("the editor caps the line and keeps it when a poll shrinks the capacity") {
    TextDisplay display;
    display.arm_line_editor(4);
    display.push_text("abcd");
    CHECK(display.cell(0, 2).code == 'c');
    CHECK(display.cell(0, 3).code == 0);
    display.push_text("\n");
    CHECK(display.pending_line_length() == 3);

    TextDisplay shrinking;
    shrinking.arm_line_editor(16);
    shrinking.push_text("abcdef");
    shrinking.arm_line_editor(3);
    CHECK(shrinking.pending_line_length() == 0);
    shrinking.push_text("\n");
    CHECK(shrinking.pending_line_length() == 6);
}

TEST_CASE("an empty line is a completed line") {
    TextDisplay display;
    display.arm_line_editor(4);
    display.push_text("\n");
    CHECK(display.line_ready());
    CHECK(display.pending_line_length() == 0);
    const auto line = display.take_line();
    REQUIRE(line.has_value());
    CHECK(line->empty());
}

TEST_CASE("reset restores the construction state and drops input") {
    TextDisplay display(10, 20, 0x00112233u, 0x00445566u);
    display.put_string("abc");
    display.push_key(0x41);
    display.push_click(1, 2, krc::display::kButtonLeft);
    display.arm_line_editor(16);
    display.push_text("draft");
    display.reset();
    CHECK(display.rows() == 10);
    CHECK(display.cols() == 20);
    CHECK(display.foreground() == 0x00112233u);
    CHECK(display.background() == 0x00445566u);
    CHECK(display.cell(0, 0).code == 0);
    CHECK(display.cell(9, 19).code == 0);
    CHECK(display.cell(4, 4).foreground == 0x00112233u);
    CHECK(display.cursor_row() == 0);
    CHECK(display.cursor_col() == 0);
    CHECK(display.cursor_visible());
    CHECK_FALSE(display.editor_active());
    CHECK_FALSE(display.line_ready());
    CHECK(display.get_key() == kStatusNotReady);
    krc::display::Click click;
    CHECK_FALSE(display.take_click(click));
}

TEST_CASE("the editor echo starts at the arming cursor") {
    TextDisplay display;
    display.set_cursor(2, 5);
    display.arm_line_editor(8);
    display.push_text("q");
    CHECK(display.cell(2, 5).code == 'q');
    CHECK(display.cursor_row() == 2);
    CHECK(display.cursor_col() == 6);
}
