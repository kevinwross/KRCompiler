#include <stdarg.h>

int putchar(int character);
void exit(int status);

int puts(const char *text) {
    while (*text) {
        putchar(*text);
        text++;
    }
    putchar('\n');
    return 0;
}

static void print_unsigned(unsigned int value) {
    char digits[16];
    int count = 0;
    if (value == 0) {
        putchar('0');
        return;
    }
    while (value > 0) {
        digits[count] = (char)('0' + (value % 10));
        value = value / 10;
        count++;
    }
    while (count > 0) {
        count--;
        putchar(digits[count]);
    }
}

static void print_hex(unsigned int value) {
    char digits[16];
    int count = 0;
    if (value == 0) {
        putchar('0');
        return;
    }
    while (value > 0) {
        unsigned int digit = value & 15u;
        if (digit < 10) {
            digits[count] = (char)('0' + digit);
        } else {
            digits[count] = (char)('a' + (digit - 10));
        }
        value = value >> 4;
        count++;
    }
    while (count > 0) {
        count--;
        putchar(digits[count]);
    }
}

#define PRINT_UPPER 1
#define PRINT_NEGATIVE 2
#define PRINT_ZERO_PAD 4
#define PRINT_LEFT 8
#define PRINT_PREFIX 16
#define PRINT_PLUS 32
#define PRINT_SPACE 64
#define PRINT_PRECISION_SHIFT 8

static void print_integer(unsigned int value, int base, int width,
                          int flags) {
    char digits[32];
    int count = 0;
    int negative = (flags & PRINT_NEGATIVE) != 0;
    int uppercase = (flags & PRINT_UPPER) != 0;
    int prefix = (flags & PRINT_PREFIX) != 0;
    int precision = flags >> PRINT_PRECISION_SHIFT;
    if (value == 0 && precision == 0) {
        digits[count++] = '0';
    } else {
        while (value > 0) {
            unsigned int digit = value % (unsigned int)base;
            if (digit < 10) {
                digits[count++] = (char)('0' + digit);
            } else {
                digits[count++] =
                    (char)((uppercase ? 'A' : 'a') + (digit - 10));
            }
            value = value / (unsigned int)base;
        }
        if (count == 0) {
            digits[count++] = '0';
        }
    }
    char sign = negative ? '-' : ((flags & PRINT_PLUS) ? '+'
                                  : ((flags & PRINT_SPACE) ? ' ' : 0));
    int zeros = precision > count ? precision - count : 0;
    int prefix_length = (sign ? 1 : 0) + (prefix ? 2 : 0);
    int digits_length = count + zeros;
    int padding = width > prefix_length + digits_length
                      ? width - prefix_length - digits_length
                      : 0;
    if (sign) {
        putchar(sign);
    }
    if (prefix) {
        putchar('0');
        putchar(uppercase ? 'X' : 'x');
    }
    if ((flags & PRINT_LEFT) == 0) {
        int zero_padding = (flags & PRINT_ZERO_PAD) && precision == 0;
        for (int index = 0; index < padding; index++) {
            putchar(zero_padding ? '0' : ' ');
        }
    }
    for (int index = 0; index < zeros; index++) {
        putchar('0');
    }
    while (count > 0) {
        putchar(digits[--count]);
    }
    if ((flags & PRINT_LEFT) != 0) {
        for (int index = 0; index < padding; index++) {
            putchar(' ');
        }
    }
}

static void print_text(const char *text, int width, int precision,
                       int flags) {
    if (text == 0) {
        text = "(null)";
    }
    int precision_set = (flags & 2) != 0;
    int left_align = (flags & 1) != 0;
    int length = 0;
    while (text[length] != 0 &&
           (!precision_set || length < precision)) {
        length++;
    }
    int padding = width > length ? width - length : 0;
    if (!left_align) {
        for (int index = 0; index < padding; index++) {
            putchar(' ');
        }
    }
    for (int index = 0; index < length; index++) {
        putchar(text[index]);
    }
    if (left_align) {
        for (int index = 0; index < padding; index++) {
            putchar(' ');
        }
    }
}

static void print_fixed(double value, int precision, int trim) {
    if (value < 0.0) {
        putchar('-');
        value = -value;
    }
    if (precision > 15) {
        precision = 15;
    }
    unsigned int whole = (unsigned int)value;
    double fraction = value - (double)whole;
    unsigned int scale = 1;
    for (int index = 0; index < precision; index++) {
        scale = scale * 10;
    }
    unsigned int fractional = (unsigned int)(fraction * (double)scale + 0.5);
    if (fractional >= scale) {
        whole++;
        fractional = fractional - scale;
    }
    print_unsigned(whole);
    if (precision > 0) {
        char digits[16];
        int count = 0;
        unsigned int divisor = scale / 10;
        while (divisor > 0) {
            digits[count] = (char)('0' + ((fractional / divisor) % 10));
            count++;
            divisor = divisor / 10;
        }
        if (trim) {
            while (count > 0 && digits[count - 1] == '0') {
                count--;
            }
        }
        if (count > 0) {
            putchar('.');
            for (int index = 0; index < count; index++) {
                putchar(digits[index]);
            }
        }
    }
}

static void print_exponent(double value, int precision) {
    if (value < 0.0) {
        putchar('-');
        value = -value;
    }
    int exponent = 0;
    if (value != 0.0) {
        while (value >= 10.0) {
            value = value / 10.0;
            exponent++;
        }
        while (value < 1.0) {
            value = value * 10.0;
            exponent--;
        }
    }
    print_fixed(value, precision, 0);
    putchar('e');
    if (exponent < 0) {
        putchar('-');
        exponent = -exponent;
    } else {
        putchar('+');
    }
    if (exponent < 10) {
        putchar('0');
    }
    print_unsigned((unsigned int)exponent);
}

static void print_general(double value, int precision) {
    double magnitude = value;
    if (magnitude < 0.0) {
        magnitude = -magnitude;
    }
    int exponent = 0;
    if (magnitude != 0.0) {
        while (magnitude >= 10.0) {
            magnitude = magnitude / 10.0;
            exponent++;
        }
        while (magnitude < 1.0) {
            magnitude = magnitude * 10.0;
            exponent--;
        }
    }
    if (exponent < -4 || exponent >= precision) {
        int digits = precision - 1;
        if (digits < 0) {
            digits = 0;
        }
        print_exponent(value, digits);
    } else {
        print_fixed(value, precision - 1 - exponent, 1);
    }
}

int printf(const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    int written = 0;
    while (*format) {
        if (*format != '%') {
            putchar(*format);
            format++;
            written++;
            continue;
        }
        format++;
        int alternate = 0;
        if (*format == '#') {
            alternate = 1;
            format++;
        }
        int zero_pad = 0;
        int left_align = 0;
        int plus = 0;
        int space = 0;
        for (;;) {
            if (*format == '0') {
                zero_pad = 1;
                format++;
            } else if (*format == '-') {
                left_align = 1;
                format++;
            } else if (*format == '+') {
                plus = 1;
                format++;
            } else if (*format == ' ') {
                space = 1;
                format++;
            } else {
                break;
            }
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
        int precision = 6;
        int precision_set = 0;
        if (*format == '.') {
            precision_set = 1;
            format++;
            precision = 0;
            if (*format == '*') {
                precision = va_arg(arguments, int);
                if (precision < 0) {
                    precision_set = 0;
                    precision = 6;
                }
                format++;
            }
            while (*format >= '0' && *format <= '9') {
                precision = precision * 10 + (*format - '0');
                format++;
            }
        }
        while (*format == 'l' || *format == 'z') {
            format++;
        }
        if (*format == '%') {
            putchar('%');
            written++;
        } else if (*format == 'd' || *format == 'i') {
            int value = va_arg(arguments, int);
            int flags = (value < 0 ? PRINT_NEGATIVE : 0) |
                        (zero_pad ? PRINT_ZERO_PAD : 0) |
                        (left_align ? PRINT_LEFT : 0) |
                        (plus ? PRINT_PLUS : 0) |
                        (space ? PRINT_SPACE : 0) |
                        (precision_set ? (precision << 8) : 0);
            print_integer(value < 0 ? (unsigned int)(-value)
                                    : (unsigned int)value,
                          10, width, flags);
        } else if (*format == 'u') {
            print_integer(va_arg(arguments, unsigned int), 10, width,
                          (zero_pad ? PRINT_ZERO_PAD : 0) |
                              (left_align ? PRINT_LEFT : 0) |
                              (precision_set ? (precision << 8) : 0));
        } else if (*format == 'o') {
            print_integer(va_arg(arguments, unsigned int), 8, width,
                          (zero_pad ? PRINT_ZERO_PAD : 0) |
                              (left_align ? PRINT_LEFT : 0) |
                              (precision_set ? (precision << 8) : 0));
        } else if (*format == 'x' || *format == 'X') {
            print_integer(va_arg(arguments, unsigned int), 16, width,
                          (*format == 'X' ? PRINT_UPPER : 0) |
                              (alternate ? PRINT_PREFIX : 0) |
                              (zero_pad ? PRINT_ZERO_PAD : 0) |
                              (left_align ? PRINT_LEFT : 0) |
                              (precision_set ? (precision << 8) : 0));
        } else if (*format == 'p') {
            print_integer((unsigned int)va_arg(arguments, void *), 16, 0,
                          PRINT_PREFIX);
        } else if (*format == 'h') {
            putchar('0');
            putchar('x');
            print_hex((unsigned int)va_arg(arguments, void *));
            written += 2;
        } else if (*format == 's') {
            int flags = (precision_set ? 2 : 0) |
                        (left_align ? 1 : 0);
            print_text(va_arg(arguments, char *), width, precision, flags);
        } else if (*format == 'c') {
            putchar(va_arg(arguments, int));
            written++;
        } else if (*format == 'f' || *format == 'F') {
            print_fixed(va_arg(arguments, double), precision, 0);
        } else if (*format == 'e' || *format == 'E') {
            print_exponent(va_arg(arguments, double), precision);
        } else if (*format == 'g' || *format == 'G') {
            print_general(va_arg(arguments, double), precision);
        } else {
            putchar('%');
            putchar(*format);
            written += 2;
        }
        format++;
    }
    va_end(arguments);
    return written;
}

struct heap_header {
    unsigned int size;
    struct heap_header *next;
};

static unsigned char *heap_next;
static struct heap_header *free_head;
#define HEAP_BEGIN ((unsigned char *)0x00020000)
#define HEAP_END ((unsigned char *)0x08000000)

void *malloc(unsigned int size) {
    size = (size + 7) & ~7u;
    struct heap_header *previous = 0;
    struct heap_header *header = free_head;
    while (header) {
        if (header->size >= size) {
            if (previous) {
                previous->next = header->next;
            } else {
                free_head = header->next;
            }
            return (void *)(header + 1);
        }
        previous = header;
        header = header->next;
    }

    if (heap_next == 0) {
        heap_next = HEAP_BEGIN;
    }
    if (heap_next + sizeof(struct heap_header) + size > HEAP_END) {
        return 0;
    }
    header = (struct heap_header *)heap_next;
    header->size = size;
    header->next = 0;
    heap_next = heap_next + sizeof(struct heap_header) + size;
    return (void *)(header + 1);
}

void free(void *pointer) {
    if (pointer == 0) {
        return;
    }
    struct heap_header *header = (struct heap_header *)pointer - 1;
    header->next = free_head;
    free_head = header;
}

void *calloc(unsigned int count, unsigned int size) {
    unsigned int total = count * size;
    unsigned char *result = (unsigned char *)malloc(total);
    if (result == 0) {
        return 0;
    }
    for (unsigned int index = 0; index < total; index++) {
        result[index] = 0;
    }
    return result;
}

void *realloc(void *pointer, unsigned int size) {
    if (pointer == 0) {
        return malloc(size);
    }
    struct heap_header *header = (struct heap_header *)pointer - 1;
    unsigned char *replacement = (unsigned char *)malloc(size);
    if (replacement == 0) {
        return 0;
    }
    unsigned int copy = header->size;
    if (copy > size) {
        copy = size;
    }
    for (unsigned int index = 0; index < copy; index++) {
        replacement[index] = ((unsigned char *)pointer)[index];
    }
    free(pointer);
    return replacement;
}

static unsigned int random_state = 1;
int sys_random(void) {
    random_state = random_state * 1103515245u + 12345u;
    return (int)((random_state >> 16) & 0x7fff);
}

int _heapwalk(void) {
    return 0;
}


unsigned int strlen(const char *text) {
    unsigned int length = 0;
    while (text[length]) {
        length++;
    }
    return length;
}

int strcmp(const char *left, const char *right) {
    while (*left && *left == *right) {
        left++;
        right++;
    }
    return (unsigned char)*left - (unsigned char)*right;
}

int strncmp(const char *left, const char *right, unsigned int count) {
    for (unsigned int index = 0; index < count; index++) {
        unsigned char a = (unsigned char)left[index];
        unsigned char b = (unsigned char)right[index];
        if (a != b || a == 0) {
            return a - b;
        }
    }
    return 0;
}

char *strcpy(char *destination, const char *source) {
    char *result = destination;
    while (*source) {
        *destination = *source;
        destination++;
        source++;
    }
    *destination = 0;
    return result;
}

char *strdup(const char *source) {
    unsigned int length = strlen(source);
    char *result = (char *)malloc(length + 1);
    if (result == 0) {
        return 0;
    }
    char *cursor = result;
    while (*source) {
        *cursor = *source;
        cursor++;
        source++;
    }
    *cursor = 0;
    return result;
}

void *memcpy(void *destination, const void *source, unsigned int count) {
    unsigned char *to = (unsigned char *)destination;
    const unsigned char *from = (const unsigned char *)source;
    for (unsigned int index = 0; index < count; index++) {
        to[index] = from[index];
    }
    return destination;
}

void *memset(void *destination, int value, unsigned int count) {
    unsigned char *to = (unsigned char *)destination;
    for (unsigned int index = 0; index < count; index++) {
        to[index] = (unsigned char)value;
    }
    return destination;
}

int memcmp(const void *left, const void *right, unsigned int count) {
    const unsigned char *a = (const unsigned char *)left;
    const unsigned char *b = (const unsigned char *)right;
    for (unsigned int index = 0; index < count; index++) {
        if (a[index] != b[index]) {
            return (int)a[index] - (int)b[index];
        }
    }
    return 0;
}

void abort(void) {
    exit(1);
}

