#ifndef KRC_DISPLAY_H
#define KRC_DISPLAY_H

/*
 * The common guest text-display API (docs/display-abi.md in the KRCompiler tree).
 *
 * A VM that provides the display family implements these wrappers over its own hypercall: the
 * argument-block layout and the hypercall number are the VM's, the command set and statuses
 * are common. The host owns the screen; a program never reads cells back. Guest strings are
 * 8-bit ASCII today and cell codes are 16-bit. Colours are XRGB8888 and the high byte is not
 * interpreted.
 *
 * A negative result is a status (READ_LINE and GET_KEY also return a non-negative result):
 *
 *   DISPLAY_BAD_ARGUMENT     -1  a bad guest pointer, length or argument
 *   DISPLAY_NOT_READY        -2  no key/click is queued, or a line is still being typed
 *   DISPLAY_UNKNOWN_COMMAND  -3  the VM does not know the command
 *   DISPLAY_END_OF_INPUT     -4  READ_LINE/GET_KEY only: the host closed its input source
 *
 * A VM without the display leaves the hypercall reserved, so a program that cares calls
 * display_rows()/display_cols() first and treats a negative answer as "no display".
 */

#include <stdint.h>

#define DISPLAY_ABI_VERSION 2

#define DISPLAY_CMD_INFO 0
#define DISPLAY_CMD_CLEAR 1
#define DISPLAY_CMD_SET_COLOR 2
#define DISPLAY_CMD_GET_COLOR 3
#define DISPLAY_CMD_PUT_CHAR 4
#define DISPLAY_CMD_PUT_STRING 5
#define DISPLAY_CMD_CURSOR 6
#define DISPLAY_CMD_READ_LINE 7
#define DISPLAY_CMD_GET_KEY 8
#define DISPLAY_CMD_GET_CLICK 9

#define DISPLAY_OK 0
#define DISPLAY_BAD_ARGUMENT (-1)
#define DISPLAY_NOT_READY (-2)
#define DISPLAY_UNKNOWN_COMMAND (-3)
#define DISPLAY_END_OF_INPUT (-4)

/* Key codes. Printable keys carry their ASCII code; the extended keys sit above it. */
#define DISPLAY_KEY_BACKSPACE 8
#define DISPLAY_KEY_TAB 9
#define DISPLAY_KEY_ENTER 13
#define DISPLAY_KEY_ESCAPE 27
#define DISPLAY_KEY_LEFT 0x0102
#define DISPLAY_KEY_RIGHT 0x0103

/* Clicks report a button mask; the wheel is not part of the ABI. */
#define DISPLAY_BUTTON_LEFT 1
#define DISPLAY_BUTTON_RIGHT 2
#define DISPLAY_BUTTON_MIDDLE 4

/* Fills the screen with the current colours. */
int display_clear(void);

/* The screen geometry from INFO, or a negative status when the VM has no display. */
int display_rows(void);
int display_cols(void);

/* The colours used for new text. The high byte is stored but not interpreted. */
int display_set_color(uint32_t foreground, uint32_t background);
int display_get_color(uint32_t *foreground, uint32_t *background);

/* Writes text at the cursor, wrapping and scrolling the host screen. 0x08, 0x09, 0x0A and
   0x0D act as backspace, tab, newline and carriage return, not as cells. */
int display_putc(int code);
int display_puts(const char *text);

/* Moves the cursor and shows or hides it; visible is 0 or 1. */
int display_cursor(int row, int col, int visible);

/* Host line editing. The first call returns DISPLAY_NOT_READY and arms the editor; the host
   echoes typing at the cursor. When Enter completes the line the next call copies it to
   buffer (without a terminator), NUL-terminates it when capacity leaves room, and returns the
   length. Escape cancels the line, so the next call arms a new one and returns
   DISPLAY_NOT_READY again. Once the host closes its input source, subsequent calls answer
   DISPLAY_END_OF_INPUT; a partial draft left at the close is delivered first as a final
   line. */
int display_read_line(char *buffer, int capacity);

/* Polled input: a key code, or DISPLAY_NOT_READY when nothing is queued. After the host
   closes its input source it answers DISPLAY_END_OF_INPUT once the queue is drained. */
int display_get_key(void);

/* Polled mouse input: row/col are cell coordinates and buttons is the mask above. Returns
   DISPLAY_OK with the outputs written, or DISPLAY_NOT_READY when nothing is queued. */
int display_get_click(int *row, int *col, int *buttons);

#endif
