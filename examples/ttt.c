/*
 * ttt.c - tic-tac-toe drawn on the shared text display.
 *
 * This is the display-terminal version of examples/tictactoe.cpp: stdin and
 * stdout are the host text display, so the board is drawn with the cursor and
 * the host line editor provides echoed input. The console is not used.
 *
 * Cells are numbered 1..9:
 *
 *   1 | 2 | 3
 *  ---+---+---
 *   4 | 5 | 6
 *  ---+---+---
 *   7 | 8 | 9
 *
 * The human plays X and moves first. Input is one move per line; "y"/"n"
 * answers the replay prompt. End of display input exits cleanly.
 *
 * Build for kr216 (from the KRCompiler root, with the kr216 runtime):
 *   krc -c --target=kr216 examples/ttt.c -o build/ttt.kro
 *   krc -c --target=kr216 targets/kr216/runtime/crt0.s -o build/crt0.kro
 *   krc -c --target=kr216 targets/kr216/runtime/syscalls.s -o build/syscalls.kro
 *   krc -c --target=kr216 targets/kr216/runtime/eh.s -o build/eh.kro
 *   krc -c --target=kr216 targets/kr216/runtime/libc.c -o build/libc.kro
 *   krc -c --target=kr216 targets/kr216/runtime/display.c -o build/display.kro
 *   krld build/crt0.kro build/syscalls.kro build/eh.kro build/libc.kro \
 *        build/display.kro build/ttt.kro -o build/ttt.kr216
 *
 * It uses only the common guest display API in <krc/display.h>; there is no
 * libc stdio path in the source.
 */

#include <krc/display.h>
#include <stdint.h>

#define COLOR_BG      0xFF101828UL
#define COLOR_TEXT    0xFFE0E8F0UL
#define COLOR_TITLE   0xFFFFD75FUL
#define COLOR_SUBTITLE 0xFF88B8FFUL
#define COLOR_BORDER  0xFF4C5A68UL
#define COLOR_EMPTY   0xFF6C7A8AUL
#define COLOR_X       0xFF55FFFFUL
#define COLOR_O       0xFFFF6B6BUL

static char board[9];
static int human_wins;
static int computer_wins;
static int draws;

static int screen_rows;
static int screen_cols;
static int board_left;
static int board_top;
static int title_row;
static int subtitle_row;
static int score_row;
static int message_row;
static int prompt_row;

/* The eight winning lines, as three parallel arrays. */
static const int line_a[8] = {0, 3, 6, 0, 1, 2, 0, 2};
static const int line_b[8] = {1, 4, 7, 3, 4, 5, 4, 4};
static const int line_c[8] = {2, 5, 8, 6, 7, 8, 8, 6};

static int text_length(const char *text) {
    int length = 0;
    while (text[length] != 0) {
        ++length;
    }
    return length;
}

static void write_at(int row, int col, const char *text, uint32_t colour) {
    if (row < 0 || row >= screen_rows || col < 0 || col >= screen_cols) {
        return;
    }
    display_cursor(row, col, 0);
    display_set_color(colour, COLOR_BG);
    display_puts(text);
}

static void write_centered(int row, const char *text, uint32_t colour) {
    const int length = text_length(text);
    int col = (screen_cols - length) / 2;
    if (col < 0) {
        col = 0;
    }
    write_at(row, col, text, colour);
}

static void clear_row(int row) {
    if (row < 0 || row >= screen_rows) {
        return;
    }
    display_cursor(row, 0, 0);
    display_set_color(COLOR_BG, COLOR_BG);
    for (int col = 0; col < screen_cols; ++col) {
        display_putc(' ');
    }
}

static void append_decimal(char *buffer, int *length, int value) {
    char digits[12];
    int count = 0;
    if (value < 0) {
        buffer[(*length)++] = '-';
        value = -value;
    }
    if (value == 0) {
        digits[count++] = '0';
    }
    while (value > 0) {
        digits[count++] = (char)('0' + (value % 10));
        value /= 10;
    }
    while (count > 0) {
        buffer[(*length)++] = digits[--count];
    }
    buffer[*length] = 0;
}

static void draw_header(void) {
    write_centered(title_row, "TIC-TAC-TOE", COLOR_TITLE);
    write_centered(subtitle_row, "You are X, the computer is O", COLOR_SUBTITLE);
}

static void draw_score(void) {
    char line[64];
    int length = 0;
    line[length++] = 'S';
    line[length++] = 'c';
    line[length++] = 'o';
    line[length++] = 'r';
    line[length++] = 'e';
    line[length++] = ':';
    line[length++] = ' ';
    line[length++] = 'y';
    line[length++] = 'o';
    line[length++] = 'u';
    line[length++] = ' ';
    append_decimal(line, &length, human_wins);
    line[length++] = ' ';
    line[length++] = ' ';
    line[length++] = 'c';
    line[length++] = 'o';
    line[length++] = 'm';
    line[length++] = 'p';
    line[length++] = 'u';
    line[length++] = 't';
    line[length++] = 'e';
    line[length++] = 'r';
    line[length++] = ' ';
    append_decimal(line, &length, computer_wins);
    line[length++] = ' ';
    line[length++] = ' ';
    line[length++] = 'd';
    line[length++] = 'r';
    line[length++] = 'a';
    line[length++] = 'w';
    line[length++] = 's';
    line[length++] = ' ';
    append_decimal(line, &length, draws);
    write_centered(score_row, line, COLOR_TEXT);
}

static void draw_board(void) {
    static const char *border = "+---+---+---+";
    static const char *empty_border = "|   |   |   |";
    for (int row = 0; row < 7; ++row) {
        const int screen_row = board_top + row;
        if ((row & 1) == 0) {
            write_at(screen_row, board_left, border, COLOR_BORDER);
            continue;
        }
        write_at(screen_row, board_left, empty_border, COLOR_BORDER);
        for (int column = 0; column < 3; ++column) {
            const int cell = (row / 2) * 3 + column;
            const int cell_col = board_left + 2 + column * 4;
            char mark[2];
            uint32_t colour;
            if (board[cell] == 'X') {
                mark[0] = 'X';
                colour = COLOR_X;
            } else if (board[cell] == 'O') {
                mark[0] = 'O';
                colour = COLOR_O;
            } else {
                mark[0] = (char)('1' + cell);
                colour = COLOR_EMPTY;
            }
            mark[1] = 0;
            write_at(screen_row, cell_col, mark, colour);
        }
    }
}

static void draw_message(const char *message) {
    clear_row(message_row);
    if (message != 0 && message[0] != 0) {
        write_centered(message_row, message, COLOR_TEXT);
    }
}

static void draw_prompt(void) {
    clear_row(prompt_row);
    write_at(prompt_row, board_left, "Your move (1-9): ", COLOR_TEXT);
    display_cursor(prompt_row, board_left + 17, 1);
}

static void clear_board(void) {
    for (int index = 0; index < 9; ++index) {
        board[index] = ' ';
    }
}

static char winner(void) {
    for (int index = 0; index < 8; ++index) {
        const char mark = board[line_a[index]];
        if (mark != ' ' && mark == board[line_b[index]] &&
            mark == board[line_c[index]]) {
            return mark;
        }
    }
    for (int index = 0; index < 9; ++index) {
        if (board[index] == ' ') {
            return ' ';
        }
    }
    return 'D';
}

/* Reads one host-edited line. Returns its length, or -1 at end of input. */
static int read_line(char *buffer, int capacity) {
    for (;;) {
        const int length = display_read_line(buffer, capacity);
        if (length == DISPLAY_NOT_READY) {
            continue;
        }
        if (length < 0) {
            return -1;
        }
        return length;
    }
}

/* Parses exactly one digit 1..9 with optional surrounding spaces. */
static int parse_cell(const char *text, int length) {
    int position = 0;
    while (position < length && (text[position] == ' ' || text[position] == '\t')) {
        ++position;
    }
    if (position >= length || text[position] < '1' || text[position] > '9') {
        return -1;
    }
    const int cell = text[position] - '1';
    ++position;
    while (position < length && (text[position] == ' ' || text[position] == '\t')) {
        ++position;
    }
    if (position != length) {
        return -1;
    }
    return cell;
}

static int winning_move(char mark) {
    for (int index = 0; index < 9; ++index) {
        if (board[index] != ' ') {
            continue;
        }
        board[index] = mark;
        const int won = winner() == mark;
        board[index] = ' ';
        if (won) {
            return index;
        }
    }
    return -1;
}

static int computer_move(void) {
    int move = winning_move('O');
    if (move >= 0) {
        return move;
    }
    move = winning_move('X');
    if (move >= 0) {
        return move;
    }
    if (board[4] == ' ') {
        return 4;
    }
    static const int corners[4] = {0, 2, 6, 8};
    static const int opposite[4] = {8, 6, 2, 0};
    for (int index = 0; index < 4; ++index) {
        if (board[corners[index]] == 'X' && board[opposite[index]] == ' ') {
            return opposite[index];
        }
    }
    for (int index = 0; index < 4; ++index) {
        if (board[corners[index]] == ' ') {
            return corners[index];
        }
    }
    static const int sides[4] = {1, 3, 5, 7};
    for (int index = 0; index < 4; ++index) {
        if (board[sides[index]] == ' ') {
            return sides[index];
        }
    }
    for (int index = 0; index < 9; ++index) {
        if (board[index] == ' ') {
            return index;
        }
    }
    return -1;
}

static void report_result(char result) {
    if (result == 'X') {
        ++human_wins;
        draw_message("You win!");
    } else if (result == 'O') {
        ++computer_wins;
        draw_message("The computer wins.");
    } else {
        ++draws;
        draw_message("It is a draw.");
    }
}

/* Plays one round. Returns 1 when the round ended, 0 at end of input. */
static int play_round(void) {
    char line[32];
    clear_board();
    draw_header();
    draw_board();
    draw_score();
    draw_message("You move first; pick a numbered square.");

    for (;;) {
        draw_prompt();
        const int length = read_line(line, (int)sizeof(line));
        if (length < 0) {
            return 0;
        }
        clear_row(prompt_row);

        const int cell = parse_cell(line, length);
        if (cell < 0 || board[cell] != ' ') {
            draw_message("Choose an empty square from 1 to 9.");
            continue;
        }
        board[cell] = 'X';
        draw_board();

        char result = winner();
        if (result != ' ') {
            report_result(result);
            draw_score();
            return 1;
        }

        const int move = computer_move();
        if (move < 0) {
            draw_board();
            ++draws;
            draw_message("It is a draw.");
            draw_score();
            return 1;
        }
        board[move] = 'O';
        draw_board();
        draw_message("The computer replies.");
    }
}

int main(void) {
    screen_rows = display_rows();
    screen_cols = display_cols();
    if (screen_rows < 20 || screen_cols < 30) {
        return 1;
    }

    display_set_color(COLOR_TEXT, COLOR_BG);
    display_clear();

    title_row = (screen_rows - 14) / 2;
    if (title_row < 0) {
        title_row = 0;
    }
    subtitle_row = title_row + 1;
    board_top = title_row + 3;
    board_left = (screen_cols - 13) / 2;
    score_row = board_top + 9;
    message_row = board_top + 10;
    prompt_row = board_top + 11;

    for (;;) {
        if (!play_round()) {
            display_cursor(message_row, 0, 0);
            draw_message("Input ended. Thanks for playing.");
            break;
        }
        draw_prompt();
        clear_row(prompt_row);
        write_at(prompt_row, board_left, "Play again? (y/n): ", COLOR_TEXT);
        display_cursor(prompt_row, board_left + 19, 1);
        char answer[32];
        const int length = read_line(answer, (int)sizeof(answer));
        clear_row(prompt_row);
        if (length < 0) {
            draw_message("Input ended. Thanks for playing.");
            break;
        }
        if (length > 0 && (answer[0] == 'n' || answer[0] == 'N')) {
            break;
        }
        if (length > 0 && (answer[0] != 'y' && answer[0] != 'Y')) {
            draw_message("Assuming yes.");
        }
    }
    display_cursor(0, 0, 0);
    return 0;
}
