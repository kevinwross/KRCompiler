/*
 * The C side of <krc/display.h>: thin wrappers over the SYS 5 hypercall (docs/display-abi.md).
 * Each wrapper builds the 16-bit little-endian argument block the call documents and returns
 * the result the host leaves in R2. Nothing here is buffered: the host owns the screen and
 * the input queues.
 *
 * The blocks are fixed-size records of 16-bit fields; a pointer field is the 16-bit guest
 * address of the data, not a host pointer. The host validates every block and inner pointer
 * against guest RAM and answers DISPLAY_BAD_ARGUMENT instead of faulting.
 */

#include <krc/display.h>

/* The assembly trampoline in targets/kr216/runtime/syscalls.s. */
extern int __krc_display(int command, const void *block, unsigned int length);

struct display_info_block {
    uint16_t abi_version;
    uint16_t rows;
    uint16_t cols;
    uint16_t reserved;
};

struct display_color_block {
    uint16_t foreground_low;
    uint16_t foreground_high;
    uint16_t background_low;
    uint16_t background_high;
};

struct display_char_block {
    uint16_t code;
};

struct display_string_block {
    const char *address;
    uint16_t length;
};

struct display_cursor_block {
    uint16_t row;
    uint16_t column;
    uint16_t visible;
};

struct display_line_block {
    char *buffer;
    uint16_t capacity;
};

struct display_click_block {
    uint16_t row;
    uint16_t column;
    uint16_t buttons;
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
    block.foreground_low = (uint16_t)(foreground & 0xFFFFu);
    block.foreground_high = (uint16_t)(foreground >> 16);
    block.background_low = (uint16_t)(background & 0xFFFFu);
    block.background_high = (uint16_t)(background >> 16);
    return __krc_display(DISPLAY_CMD_SET_COLOR, &block, sizeof block);
}

int display_get_color(uint32_t *foreground, uint32_t *background) {
    struct display_color_block block;
    const int status = __krc_display(DISPLAY_CMD_GET_COLOR, &block, sizeof block);
    if (status != DISPLAY_OK) {
        return status;
    }
    if (foreground != 0) {
        *foreground = (uint32_t)block.foreground_low |
                      ((uint32_t)block.foreground_high << 16);
    }
    if (background != 0) {
        *background = (uint32_t)block.background_low |
                      ((uint32_t)block.background_high << 16);
    }
    return DISPLAY_OK;
}

int display_putc(int code) {
    struct display_char_block block;
    block.code = (uint16_t)code;
    return __krc_display(DISPLAY_CMD_PUT_CHAR, &block, sizeof block);
}

int display_puts(const char *text) {
    struct display_string_block block;
    uint16_t length = 0;
    if (text == 0) {
        return DISPLAY_BAD_ARGUMENT;
    }
    while (text[length] != 0) {
        length = (uint16_t)(length + 1);
    }
    block.address = text;
    block.length = length;
    return __krc_display(DISPLAY_CMD_PUT_STRING, &block, sizeof block);
}

int display_cursor(int row, int col, int visible) {
    struct display_cursor_block block;
    block.row = (uint16_t)row;
    block.column = (uint16_t)col;
    block.visible = (uint16_t)(visible != 0);
    return __krc_display(DISPLAY_CMD_CURSOR, &block, sizeof block);
}

int display_read_line(char *buffer, int capacity) {
    struct display_line_block block;
    int status;
    if (capacity <= 0) {
        return DISPLAY_BAD_ARGUMENT;
    }
    block.buffer = buffer;
    block.capacity = (uint16_t)capacity;
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
