#include <stdarg.h>
#include <stdlib.h>

unsigned int strlen(const char *text) {
    unsigned int length = 0;
    while (text[length] != 0) {
        length++;
    }
    return length;
}

int strcmp(const char *left, const char *right) {
    while (*left != 0 && *left == *right) {
        left++;
        right++;
    }
    return (int)*left - (int)*right;
}

int strncmp(const char *left, const char *right, unsigned int count) {
    while (count > 0 && *left != 0 && *left == *right) {
        left++;
        right++;
        count--;
    }
    if (count == 0) {
        return 0;
    }
    return (int)*left - (int)*right;
}

char *strcpy(char *destination, const char *source) {
    char *result = destination;
    while ((*destination++ = *source++) != 0) {
    }
    return result;
}

char *strcat(char *destination, const char *source) {
    char *result = destination;
    while (*destination != 0) {
        destination++;
    }
    while ((*destination++ = *source++) != 0) {
    }
    return result;
}

void *memcpy(void *destination, const void *source, unsigned int count) {
    unsigned char *out = (unsigned char *)destination;
    const unsigned char *in = (const unsigned char *)source;
    while (count > 0) {
        *out++ = *in++;
        count--;
    }
    return destination;
}

void *memset(void *destination, int value, unsigned int count) {
    unsigned char *out = (unsigned char *)destination;
    while (count > 0) {
        *out++ = (unsigned char)value;
        count--;
    }
    return destination;
}

int memcmp(const void *left, const void *right, unsigned int count) {
    const unsigned char *a = (const unsigned char *)left;
    const unsigned char *b = (const unsigned char *)right;
    while (count > 0 && *a == *b) {
        a++;
        b++;
        count--;
    }
    if (count == 0) {
        return 0;
    }
    return (int)*a - (int)*b;
}

int putchar(int character) {
    *(unsigned char *)0xF000 = (unsigned char)character;
    return character;
}

int getchar(void) {
    return (int)*(unsigned char *)0xF004;
}

char *strncpy(char *destination, const char *source, unsigned int count) {
    char *result = destination;
    while (count > 0 && *source != 0) {
        *destination++ = *source++;
        count--;
    }
    while (count > 0) {
        *destination++ = 0;
        count--;
    }
    return result;
}

char *strncat(char *destination, const char *source, unsigned int count) {
    char *result = destination;
    while (*destination != 0) {
        destination++;
    }
    while (count > 0 && *source != 0) {
        *destination++ = *source++;
        count--;
    }
    *destination = 0;
    return result;
}

char *strchr(const char *text, int character) {
    while (*text != 0) {
        if (*text == (char)character) {
            return (char *)text;
        }
        text++;
    }
    return character == 0 ? (char *)text : 0;
}

char *strrchr(const char *text, int character) {
    const char *last = 0;
    while (*text != 0) {
        if (*text == (char)character) {
            last = text;
        }
        text++;
    }
    if (character == 0) {
        return (char *)text;
    }
    return (char *)last;
}

char *strstr(const char *text, const char *needle) {
    if (*needle == 0) {
        return (char *)text;
    }
    while (*text != 0) {
        const char *a = text;
        const char *b = needle;
        while (*b != 0 && *a == *b) {
            a++;
            b++;
        }
        if (*b == 0) {
            return (char *)text;
        }
        text++;
    }
    return 0;
}

static int digit_value(char character) {
    if (character >= '0' && character <= '9') {
        return (int)(character - '0');
    }
    if (character >= 'a' && character <= 'z') {
        return (int)(character - 'a') + 10;
    }
    if (character >= 'A' && character <= 'Z') {
        return (int)(character - 'A') + 10;
    }
    return 99;
}

long strtol(const char *text, char **end, int base) {
    const char *cursor = text;
    int negative = 0;
    unsigned long value = 0;
    if (base == 0) {
        base = 10;
        if (*cursor == '0') {
            base = 8;
            cursor++;
            if (*cursor == 'x' || *cursor == 'X') {
                base = 16;
                cursor++;
            }
        }
    }
    if (*cursor == '-') {
        negative = 1;
        cursor++;
    } else if (*cursor == '+') {
        cursor++;
    }
    while (*cursor != 0) {
        const int digit = digit_value(*cursor);
        if (digit >= base) {
            break;
        }
        value = value * (unsigned long)base + (unsigned long)digit;
        cursor++;
    }
    if (end != 0) {
        *end = (char *)cursor;
    }
    return negative ? (long)(0ul - value) : (long)value;
}

unsigned long strtoul(const char *text, char **end, int base) {
    return (unsigned long)strtol(text, end, base);
}

int atoi(const char *text) {
    return (int)strtol(text, 0, 10);
}
int abs(int value) {
    return value < 0 ? -value : value;
}

/* A first-fit heap over the region the linker reports.
 *
 * Every block carries its payload size, a forward link, and a free flag; the
 * list runs in address order, so `free` merges forward with the next block
 * while it is free. There is no backward merge, because a block cannot reach
 * the one before it without a back pointer.
 */

extern unsigned char __heap_start;
extern unsigned char __heap_end;

typedef struct HeapBlock {
    unsigned int size;      /* payload bytes, always even */
    struct HeapBlock *next; /* next block in address order */
    unsigned int free;      /* non-zero when the payload is available */
} HeapBlock;

static HeapBlock *heap_first;

static void heap_initialize(void) {
    if (heap_first != 0) {
        return;
    }
    unsigned char *begin = (unsigned char *)&__heap_start;
    unsigned char *limit = (unsigned char *)&__heap_end;
    while (((unsigned int)begin & 1u) != 0) {
        begin++;
    }
    const unsigned int header = (unsigned int)sizeof(HeapBlock);
    if ((unsigned int)limit <= (unsigned int)begin + header + 2u) {
        return;
    }
    heap_first = (HeapBlock *)begin;
    heap_first->size = (unsigned int)limit - (unsigned int)begin - header;
    heap_first->next = 0;
    heap_first->free = 1;
}

static unsigned int heap_round(unsigned int size) {
    unsigned int rounded = (size + 1u) & ~1u;
    return rounded == 0 ? 2u : rounded;
}

static void heap_split(HeapBlock *block, unsigned int wanted) {
    const unsigned int header = (unsigned int)sizeof(HeapBlock);
    if (block->size < wanted + header + 2u) {
        return;
    }
    HeapBlock *remainder =
        (HeapBlock *)((unsigned char *)block + header + wanted);
    remainder->size = block->size - wanted - header;
    remainder->next = block->next;
    remainder->free = 1;
    block->size = wanted;
    block->next = remainder;
}

void *malloc(unsigned int size) {
    heap_initialize();
    if (size == 0) {
        size = 1;
    }
    const unsigned int wanted = heap_round(size);
    HeapBlock *block = heap_first;
    while (block != 0) {
        if (block->free && block->size >= wanted) {
            heap_split(block, wanted);
            block->free = 0;
            return (void *)((unsigned char *)block + sizeof(HeapBlock));
        }
        block = block->next;
    }
    return 0;
}

void free(void *pointer) {
    if (pointer == 0) {
        return;
    }
    HeapBlock *block =
        (HeapBlock *)((unsigned char *)pointer - (int)sizeof(HeapBlock));
    block->free = 1;
    /* Merge forward with the next block while it is free. */
    while (block->next != 0 && block->next->free) {
        block->size = block->size + (unsigned int)sizeof(HeapBlock) +
                      block->next->size;
        block->next = block->next->next;
    }
    /* Merge backward: the block before this one is found by address order. */
    HeapBlock *previous = heap_first;
    while (previous != 0 && previous->next != block) {
        previous = previous->next;
    }
    if (previous != 0 && previous->free) {
        previous->size = previous->size + (unsigned int)sizeof(HeapBlock) +
                        block->size;
        previous->next = block->next;
    }
}

void *calloc(unsigned int count, unsigned int size) {
    const unsigned int total = count * size;
    if (count != 0 && total / count != size) {
        return 0;
    }
    unsigned char *memory = (unsigned char *)malloc(total);
    if (memory == 0) {
        return 0;
    }
    unsigned int index = 0;
    while (index < total) {
        memory[index++] = 0;
    }
    return memory;
}

void *realloc(void *pointer, unsigned int size) {
    if (pointer == 0) {
        return malloc(size);
    }
    if (size == 0) {
        free(pointer);
        return 0;
    }
    HeapBlock *block =
        (HeapBlock *)((unsigned char *)pointer - sizeof(HeapBlock));
    const unsigned int wanted = heap_round(size);
    if (block->size >= wanted) {
        heap_split(block, wanted);
        if (block->next != 0 && block->next->free &&
            block->next->next != 0 && block->next->next->free) {
            HeapBlock *middle = block->next;
            middle->size = middle->size + (unsigned int)sizeof(HeapBlock) +
                           middle->next->size;
            middle->next = middle->next->next;
        }
        return pointer;
    }
    void *moved = malloc(size);
    if (moved == 0) {
        return 0;
    }
    unsigned char *destination = (unsigned char *)moved;
    unsigned char *source = (unsigned char *)pointer;
    unsigned int index = 0;
    while (index < block->size && index < wanted) {
        destination[index] = source[index];
        index++;
    }
    free(pointer);
    return moved;
}

char *strdup(const char *source) {
    const unsigned int size = strlen(source) + 1;
    char *copy = (char *)malloc(size);
    if (copy != 0) {
        strcpy(copy, source);
    }
    return copy;
}

static unsigned int random_state = 1;

void srand(unsigned int seed) {
    random_state = seed;
}

int rand(void) {
    /* A sixteen-bit target cannot hold the 32-bit multiplier of the usual
       LCG, so the state is sixteen bits wide and the constants fit a word. */
    random_state = random_state * 25173u + 13849u;
    return (int)(random_state & 0x7FFF);
}

static char *token_cursor;

static int is_delimiter(char character, const char *delimiters) {
    while (*delimiters != 0) {
        if (*delimiters == character) {
            return 1;
        }
        delimiters++;
    }
    return 0;
}

char *strtok(char *text, const char *delimiters) {
    if (text != 0) {
        token_cursor = text;
    }
    if (token_cursor == 0) {
        return 0;
    }
    while (*token_cursor != 0 && is_delimiter(*token_cursor, delimiters)) {
        token_cursor++;
    }
    if (*token_cursor == 0) {
        token_cursor = 0;
        return 0;
    }
    char *token = token_cursor;
    while (*token_cursor != 0 && !is_delimiter(*token_cursor, delimiters)) {
        token_cursor++;
    }
    if (*token_cursor != 0) {
        *token_cursor++ = 0;
    }
    return token;
}

/* fprintf-style output for the KR16 profile.
 *
 * IP16 has no long long and no floating point, so every conversion here is
 * integer or string: `int`, `long`, and pointers are all 16 bits. The format
 * engine writes through one sink so `printf` (console) and `snprintf`
 * (buffer) share the same code.
 */


static char *format_buffer;
static int format_count;
static int format_limit = -1;

static void format_char(int character) {
    if (format_buffer) {
        if (format_limit < 0 || format_count < format_limit - 1) {
            format_buffer[format_count] = (char)character;
        }
        format_count++;
    } else {
        putchar(character);
    }
}

#define FORMAT_UPPER 1
#define FORMAT_NEGATIVE 2
#define FORMAT_ZERO_PAD 4
#define FORMAT_LEFT 8
#define FORMAT_PREFIX 16
#define FORMAT_PLUS 32
#define FORMAT_SPACE 64
#define FORMAT_PRECISION_SHIFT 8

static void format_integer(unsigned int value, unsigned int base, int width,
                           int flags) {
    char digits[20];
    int count = 0;
    const int negative = (flags & FORMAT_NEGATIVE) != 0;
    const int uppercase = (flags & FORMAT_UPPER) != 0;
    const int prefix = (flags & FORMAT_PREFIX) != 0;
    const int precision = flags >> FORMAT_PRECISION_SHIFT;
    const char *alphabet = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
    if (value == 0 && precision == 0) {
        digits[count++] = '0';
    } else {
        while (value > 0) {
            digits[count++] = alphabet[value % base];
            value = value / base;
        }
        if (count == 0) {
            digits[count++] = '0';
        }
    }
    const char sign = negative
                          ? '-'
                          : ((flags & FORMAT_PLUS) ? '+'
                                                   : ((flags & FORMAT_SPACE)
                                                          ? ' '
                                                          : 0));
    const int zeros = precision > count ? precision - count : 0;
    const int prefix_length =
        (sign ? 1 : 0) + (prefix ? (base == 8 ? 1 : 2) : 0);
    const int digits_length = count + zeros;
    const int padding = width > prefix_length + digits_length
                            ? width - prefix_length - digits_length
                            : 0;
    if (sign) {
        format_char(sign);
    }
    if (prefix) {
        format_char('0');
        if (base != 8) {
            format_char(uppercase ? 'X' : 'x');
        }
    }
    if ((flags & FORMAT_LEFT) == 0) {
        const int zero_padding = (flags & FORMAT_ZERO_PAD) && precision == 0;
        int index = 0;
        while (index < padding) {
            format_char(zero_padding ? '0' : ' ');
            index++;
        }
    }
    int index = 0;
    while (index < zeros) {
        format_char('0');
        index++;
    }
    while (count > 0) {
        count--;
        format_char(digits[count]);
    }
    if ((flags & FORMAT_LEFT) != 0) {
        index = 0;
        while (index < padding) {
            format_char(' ');
            index++;
        }
    }
}

static void format_text(const char *text, int width, int precision,
                        int precision_set, int left_align) {
    if (text == 0) {
        text = "(null)";
    }
    int length = 0;
    while (text[length] != 0 && (!precision_set || length < precision)) {
        length++;
    }
    const int padding = width > length ? width - length : 0;
    int index = 0;
    if (!left_align) {
        while (index < padding) {
            format_char(' ');
            index++;
        }
    }
    index = 0;
    while (index < length) {
        format_char(text[index]);
        index++;
    }
    if (left_align) {
        index = 0;
        while (index < padding) {
            format_char(' ');
            index++;
        }
    }
}

/* Formats into the sink the caller installed: ormat_buffer names the
 * buffer (null for the console) and ormat_limit bounds the write, with a
 * negative limit meaning unbounded. */
static int format_arguments(const char *format, va_list arguments) {

    while (*format != 0) {
        if (*format != '%') {
            format_char(*format);
            format++;
            continue;
        }
        format++;
        int alternate = 0;
        int zero_pad = 0;
        int left_align = 0;
        int plus = 0;
        int space = 0;
        for (;;) {
            if (*format == '#') {
                alternate = 1;
            } else if (*format == '0') {
                zero_pad = 1;
            } else if (*format == '-') {
                left_align = 1;
            } else if (*format == '+') {
                plus = 1;
            } else if (*format == ' ') {
                space = 1;
            } else {
                break;
            }
            format++;
        }
        int width = 0;
        if (*format == '*') {
            width = va_arg(arguments, int);
            if (width < 0) {
                left_align = 1;
                width = -width;
            }
            format++;
        }
        while (*format >= '0' && *format <= '9') {
            width = width * 10 + (*format - '0');
            format++;
        }
        int precision = 0;
        int precision_set = 0;
        if (*format == '.') {
            precision_set = 1;
            format++;
            if (*format == '*') {
                precision = va_arg(arguments, int);
                if (precision < 0) {
                    precision_set = 0;
                    precision = 0;
                }
                format++;
            }
            while (*format >= '0' && *format <= '9') {
                precision = precision * 10 + (*format - '0');
                format++;
            }
        }
        /* int, long, size_t, and pointers are one 16-bit slot on this profile,
         * so every length modifier is accepted and ignored. */
        while (*format == 'h' || *format == 'l' || *format == 'z' ||
               *format == 't' || *format == 'j') {
            format++;
        }
        const int numeric_flags =
            (zero_pad ? FORMAT_ZERO_PAD : 0) |
            (left_align ? FORMAT_LEFT : 0) | (plus ? FORMAT_PLUS : 0) |
            (space ? FORMAT_SPACE : 0) |
            (precision_set ? (precision << FORMAT_PRECISION_SHIFT) : 0);
        if (*format == '%') {
            format_char('%');
        } else if (*format == 'd' || *format == 'i') {
            const int value = va_arg(arguments, int);
            const unsigned int magnitude =
                value < 0 ? (unsigned int)(0u - (unsigned int)value)
                          : (unsigned int)value;
            format_integer(magnitude, 10, width,
                           numeric_flags |
                               (value < 0 ? FORMAT_NEGATIVE : 0));
        } else if (*format == 'u') {
            format_integer(va_arg(arguments, unsigned int), 10, width,
                           numeric_flags);
        } else if (*format == 'o') {
            format_integer(va_arg(arguments, unsigned int), 8, width,
                           numeric_flags | (alternate ? FORMAT_PREFIX : 0));
        } else if (*format == 'x' || *format == 'X') {
            format_integer(va_arg(arguments, unsigned int), 16, width,
                           numeric_flags |
                               (*format == 'X' ? FORMAT_UPPER : 0) |
                               (alternate ? FORMAT_PREFIX : 0));
        } else if (*format == 'p') {
            format_integer((unsigned int)va_arg(arguments, void *), 16, width,
                           numeric_flags | FORMAT_PREFIX | FORMAT_ZERO_PAD);
        } else if (*format == 'c') {
            const int value = va_arg(arguments, int);
            const int padding = width > 1 ? width - 1 : 0;
            int index = 0;
            if (!left_align) {
                while (index < padding) {
                    format_char(' ');
                    index++;
                }
            }
            format_char(value);
            index = 0;
            if (left_align) {
                while (index < padding) {
                    format_char(' ');
                    index++;
                }
            }
        } else if (*format == 's') {
            format_text(va_arg(arguments, const char *), width, precision,
                        precision_set, left_align);
        } else if (*format == 0) {
            break;
        } else {
            format_char('%');
            format_char(*format);
        }
        format++;
    }

    if (format_buffer && format_limit > 0) {
        const int terminator =
            format_count < format_limit - 1 ? format_count : format_limit - 1;
        format_buffer[terminator] = 0;
    }
    return format_count < 0 ? 0 : format_count;
}

int vsnprintf(char *buffer, unsigned int size, const char *format,
              va_list arguments) {
    char *saved_buffer = format_buffer;
    const int saved_count = format_count;
    const int saved_limit = format_limit;
    format_buffer = buffer;
    format_count = 0;
    format_limit = size > 0x7FFFu ? 0x7FFF : (int)size;
    const int written = format_arguments(format, arguments);
    if (format_buffer && format_limit > 0) {
        const int terminator =
            format_count < format_limit - 1 ? format_count : format_limit - 1;
        format_buffer[terminator] = 0;
    }
    format_buffer = saved_buffer;
    format_count = saved_count;
    format_limit = saved_limit;
    return written;
}

int snprintf(char *buffer, unsigned int size, const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    const int written = vsnprintf(buffer, size, format, arguments);
    va_end(arguments);
    return written;
}

int vsprintf(char *buffer, const char *format, va_list arguments) {
    char *saved_buffer = format_buffer;
    const int saved_count = format_count;
    const int saved_limit = format_limit;
    format_buffer = buffer;
    format_count = 0;
    format_limit = -1;
    const int written = format_arguments(format, arguments);
    buffer[format_count] = 0;
    format_buffer = saved_buffer;
    format_count = saved_count;
    format_limit = saved_limit;
    return written;
}
int sprintf(char *buffer, const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    const int written = vsprintf(buffer, format, arguments);
    va_end(arguments);
    return written;
}

int printf(const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    format_buffer = 0;
    format_limit = -1;
    format_count = 0;
    const int written = vsnprintf(0, 0, format, arguments);
    va_end(arguments);
    return written;
}
