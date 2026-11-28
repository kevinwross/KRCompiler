/*
 * The C side of <krc/display.h> for kr232: wrappers over the SYS 5 display
 * hypercall. The block is the canonical 32-bit word layout of
 * docs/display-abi.md, because kr232 pointers and registers are 32-bit; the
 * assembly trampoline in display.s leaves the status/result in R2.
 */

#include <krc/display.h>

extern int __krc_display(int command, const void *block, unsigned int length);

struct display_info_block {
    uint32_t abi_version;
    uint32_t rows;
    uint32_t cols;
    uint32_t reserved;
};

struct display_color_block {
    uint32_t foreground;
    uint32_t background;
};

struct display_char_block {
    uint32_t code;
};

struct display_string_block {
    const char *address;
    uint32_t length;
};

struct display_cursor_block {
    uint32_t row;
    uint32_t column;
    uint32_t visible;
};

struct display_line_block {
    char *buffer;
    uint32_t capacity;
};

struct display_click_block {
    uint32_t row;
    uint32_t column;
    uint32_t buttons;
};

static int display_info(struct display_info_block *info) {
    return __krc_display(DISPLAY_CMD_INFO, info, sizeof *info);
}

int display_clear(void) {
    return __krc_display(DISPLAY_CMD_CLEAR, 0, 0);
}

int display_rows(void) {
    struct display_info_block info;
    const int status = display_info(&info);
    if (status != DISPLAY_OK) {
        return status;
    }
    return (int)info.rows;
}

int display_cols(void) {
    struct display_info_block info;
    const int status = display_info(&info);
    if (status != DISPLAY_OK) {
        return status;
    }
    return (int)info.cols;
}

int display_set_color(uint32_t foreground, uint32_t background) {
    struct display_color_block block;
    block.foreground = foreground;
    block.background = background;
    return __krc_display(DISPLAY_CMD_SET_COLOR, &block, sizeof block);
}

int display_get_color(uint32_t *foreground, uint32_t *background) {
    struct display_color_block block;
    const int status = __krc_display(DISPLAY_CMD_GET_COLOR, &block, sizeof block);
    if (status != DISPLAY_OK) {
        return status;
    }
    if (foreground != 0) {
        *foreground = block.foreground;
    }
    if (background != 0) {
        *background = block.background;
    }
    return DISPLAY_OK;
}

int display_putc(int code) {
    struct display_char_block block;
    block.code = (uint32_t)code;
    return __krc_display(DISPLAY_CMD_PUT_CHAR, &block, sizeof block);
}

int display_puts(const char *text) {
    struct display_string_block block;
    uint32_t length = 0;
    if (text == 0) {
        return DISPLAY_BAD_ARGUMENT;
    }
    while (text[length] != 0) {
        ++length;
    }
    block.address = text;
    block.length = length;
    return __krc_display(DISPLAY_CMD_PUT_STRING, &block, sizeof block);
}

int display_cursor(int row, int col, int visible) {
    struct display_cursor_block block;
    block.row = (uint32_t)row;
    block.column = (uint32_t)col;
    block.visible = (visible != 0) ? 1u : 0u;
    return __krc_display(DISPLAY_CMD_CURSOR, &block, sizeof block);
}

int display_read_line(char *buffer, int capacity) {
    struct display_line_block block;
    int status;
    if (capacity <= 0) {
        return DISPLAY_BAD_ARGUMENT;
    }
    block.buffer = buffer;
    block.capacity = (uint32_t)capacity;
    status = __krc_display(DISPLAY_CMD_READ_LINE, &block, sizeof block);
    if (status >= 0 && status < capacity) {
        buffer[status] = 0;
    }
    return status;
}

int display_get_key(void) {
    return __krc_display(DISPLAY_CMD_GET_KEY, 0, 0);
}

int display_get_click(int *row, int *col, int *buttons) {
    struct display_click_block block;
    const int status = __krc_display(DISPLAY_CMD_GET_CLICK, &block, sizeof block);
    if (status != DISPLAY_OK) {
        return status;
    }
    if (row != 0) {
        *row = (int)block.row;
    }
    if (col != 0) {
        *col = (int)block.column;
    }
    if (buttons != 0) {
        *buttons = (int)block.buttons;
    }
    return DISPLAY_OK;
}
