/*
 * tictactoe.cpp - a text-mode tic-tac-toe game for krc.
 *
 * The human plays X and moves first. The computer plays O with the standard
 * win/block/centre/opposite-corner/corner/side strategy, so a draw is the best
 * a correct human player can force. Cells are numbered 1..9:
 *
 *   1 | 2 | 3
 *  ---+---+---
 *   4 | 5 | 6
 *  ---+---+---
 *   7 | 8 | 9
 *
 * Input is line-oriented: one move, "y", or "n" per line. Blank lines are
 * ignored. The program exits cleanly when the input stream ends, which lets a
 * scripted `--input` session drive a whole game.
 *
 * Build for KR32 (from the repository root):
 *   krc -c examples/tictactoe.cpp -o build/tictactoe.kro
 *   krld build/tictactoe.kro build/windows-msvc-debug/stage/sysroot/kr32/lib/libkrcrt.kro \
 *        targets/kr32/runtime/crt0.s targets/kr32/runtime/fileio.s \
 *        targets/kr32/runtime/eh.s -o build/tictactoe.kr32
 *
 * Run:
 *   ..\kr32\build\windows-msvc\Debug\kr32.exe vm build/tictactoe.kr32 \
 *       --max-steps 20000000 --input "5`n1`n9`n"
 *
 * The `--input` text is consumed one byte at a time, so every move needs its
 * own line when the input comes from a terminal as well.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char board[9];
static int human_wins;
static int computer_wins;
static int draws;

/* One array per column of the eight winning lines. Three parallel arrays keep
 * the element stride a power of two, which the KR232 backend requires. */
static const int line_a[8] = {0, 3, 6, 0, 1, 2, 0, 2};
static const int line_b[8] = {1, 4, 7, 3, 4, 5, 4, 4};
static const int line_c[8] = {2, 5, 8, 6, 7, 8, 8, 6};

static void clear_board(void) {
    for (int i = 0; i < 9; ++i) {
        board[i] = ' ';
    }
}

/* Reads one line into buffer. Returns 1 on a line, 0 when the input ended
 * before a line started. A final line without a newline still counts. */
static int read_line(char *buffer, int capacity) {
    int count = 0;
    int c = getchar();
    if (c < 0) {
        buffer[0] = 0;
        return 0;
    }
    while (c >= 0 && c != '\n' && c != '\r') {
        if (count + 1 < capacity) {
            buffer[count++] = (char)c;
        }
        c = getchar();
    }
    buffer[count] = 0;
    return 1;
}

/* Reads one non-whitespace byte. Returns -1 at end of input. */
static int read_key(void) {
    int c = getchar();
    while (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
        c = getchar();
    }
    return c;
}

static char winner(void) {
    for (int i = 0; i < 8; ++i) {
        char a = board[line_a[i]];
        if (a != ' ' && a == board[line_b[i]] && a == board[line_c[i]]) {
            return a;
        }
    }
    for (int i = 0; i < 9; ++i) {
        if (board[i] == ' ') {
            return ' ';
        }
    }
    return 'D';
}

static void draw_board(void) {
    printf("\n");
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            int index = row * 3 + column;
            char mark = board[index];
            if (mark == ' ') {
                mark = (char)('1' + index);
            }
            printf(" %c ", mark);
            if (column < 2) {
                printf("|");
            }
        }
        printf("\n");
        if (row < 2) {
            printf("---+---+---\n");
        }
    }
    printf("\n");
}

/* Parses exactly one digit 1..9 with optional surrounding spaces. Returns the
 * zero-based cell, or -1 when the line is not a single cell number. */
static int parse_cell(const char *text) {
    while (*text == ' ' || *text == '\t') {
        ++text;
    }
    if (*text < '1' || *text > '9') {
        return -1;
    }
    int cell = *text - '1';
    ++text;
    while (*text == ' ' || *text == '\t') {
        ++text;
    }
    if (*text != 0) {
        return -1;
    }
    return cell;
}

static int winning_move(char mark) {
    for (int i = 0; i < 9; ++i) {
        if (board[i] != ' ') {
            continue;
        }
        board[i] = mark;
        int won = winner() == mark;
        board[i] = ' ';
        if (won) {
            return i;
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
    for (int i = 0; i < 4; ++i) {
        if (board[corners[i]] == 'X' && board[opposite[i]] == ' ') {
            return opposite[i];
        }
    }
    for (int i = 0; i < 4; ++i) {
        if (board[corners[i]] == ' ') {
            return corners[i];
        }
    }
    static const int sides[4] = {1, 3, 5, 7};
    for (int i = 0; i < 4; ++i) {
        if (board[sides[i]] == ' ') {
            return sides[i];
        }
    }
    for (int i = 0; i < 9; ++i) {
        if (board[i] == ' ') {
            return i;
        }
    }
    return -1;
}

static void report_result(char result) {
    if (result == 'X') {
        ++human_wins;
        printf("You win!\n");
    } else if (result == 'O') {
        ++computer_wins;
        printf("The computer wins.\n");
    } else {
        ++draws;
        printf("It is a draw.\n");
    }
}

/* Plays one round. Returns 1 when the round finished, 0 when input ended. */
static int play_round(void) {
    clear_board();
    for (;;) {
        draw_board();
        char result = winner();
        if (result != ' ') {
            report_result(result);
            return 1;
        }

        char line[32];
        printf("Your move (1-9): ");
        if (!read_line(line, sizeof(line))) {
            return 0;
        }
        int cell = parse_cell(line);
        if (cell < 0 || board[cell] != ' ') {
            printf("Choose an empty square from 1 to 9.\n");
            continue;
        }
        board[cell] = 'X';

        result = winner();
        if (result != ' ') {
            draw_board();
            report_result(result);
            return 1;
        }

        int move = computer_move();
        if (move < 0) {
            draw_board();
            printf("It is a draw.\n");
            ++draws;
            return 1;
        }
        board[move] = 'O';
        printf("The computer plays %d.\n", move + 1);
    }
}

int main(void) {
    /* A fixed seed keeps a scripted game reproducible. Change it for variety. */
    srand(1);

    printf("Tic-tac-toe\n");
    printf("You are X and move first. The computer is O.\n");

    for (;;) {
        if (!play_round()) {
            printf("\nInput ended.\n");
            break;
        }
        printf("Score: you %d, computer %d, draws %d\n",
               human_wins, computer_wins, draws);
        printf("Play again? (y/n): ");
        int key = read_key();
        if (key < 0) {
            printf("\nInput ended.\n");
            break;
        }
        if (key == 'n' || key == 'N') {
            break;
        }
        if (key != 'y' && key != 'Y') {
            printf("Assuming yes.\n");
        }
    }

    printf("Thanks for playing.\n");
    return 0;
}
