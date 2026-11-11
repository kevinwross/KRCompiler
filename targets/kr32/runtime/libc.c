#include <stdarg.h>

int krc_errno = 0;

int *__krc_errno_location(void) {
    return &krc_errno;
}

extern int krc_sys_getchar(void);
extern int krc_sys_open(const char *path, int flags);
extern int krc_sys_close(int descriptor);
extern int krc_sys_read(int descriptor, void *buffer, int count);
extern int krc_sys_write(int descriptor, const void *buffer, int count);
extern int krc_sys_seek(int descriptor, int offset, int whence);
extern int krc_sys_time(void);
extern int krc_sys_argc(void);
extern int krc_sys_arg(int index, char *buffer, int size);
extern int krc_sys_unlink(const char *path);
extern int krc_sys_rename(const char *old_path, const char *new_path);
extern int krc_sys_stat(const char *path, void *buffer, int capacity);
extern int krc_sys_truncate(const char *path, int length);
extern int krc_sys_getcwd(char *buffer, int capacity);
extern int krc_sys_chdir(const char *path);
extern int krc_sys_dup(int descriptor);
extern int krc_sys_dup2(int source, int target);

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

static char *format_buffer;
static int format_count;
static int format_limit = -1;

static void emit_char(int character) {
    if (format_buffer) {
        if (format_limit < 0 || format_count < format_limit - 1) {
            format_buffer[format_count] = (char)character;
        }
        format_count++;
    } else {
        putchar(character);
    }
}

static void print_unsigned(unsigned int value) {
    char digits[16];
    int count = 0;
    if (value == 0) {
        emit_char('0');
        return;
    }
    while (value > 0) {
        digits[count] = (char)('0' + (value % 10));
        value = value / 10;
        count++;
    }
    while (count > 0) {
        count--;
        emit_char(digits[count]);
    }
}

static void print_hex(unsigned int value) {
    char digits[16];
    int count = 0;
    if (value == 0) {
        emit_char('0');
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
        emit_char(digits[count]);
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

static void print_integer(unsigned long long value, int base, int width,
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
            unsigned long long digit = value % (unsigned long long)base;
            if (digit < 10) {
                digits[count++] = (char)('0' + digit);
            } else {
                digits[count++] =
                    (char)((uppercase ? 'A' : 'a') + (digit - 10));
            }
            value = value / (unsigned long long)base;
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
        emit_char(sign);
    }
    if (prefix) {
        emit_char('0');
        emit_char(uppercase ? 'X' : 'x');
    }
    if ((flags & PRINT_LEFT) == 0) {
        int zero_padding = (flags & PRINT_ZERO_PAD) && precision == 0;
        for (int index = 0; index < padding; index++) {
            emit_char(zero_padding ? '0' : ' ');
        }
    }
    for (int index = 0; index < zeros; index++) {
        emit_char('0');
    }
    while (count > 0) {
        emit_char(digits[--count]);
    }
    if ((flags & PRINT_LEFT) != 0) {
        for (int index = 0; index < padding; index++) {
            emit_char(' ');
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
            emit_char(' ');
        }
    }
    for (int index = 0; index < length; index++) {
        emit_char(text[index]);
    }
    if (left_align) {
        for (int index = 0; index < padding; index++) {
            emit_char(' ');
        }
    }
}

static void print_fixed(double value, int precision, int trim) {
    if (value < 0.0) {
        emit_char('-');
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
            emit_char('.');
            for (int index = 0; index < count; index++) {
                emit_char(digits[index]);
            }
        }
    }
}

static void print_float_formatted(double value, int precision, int width,
                                  int flags) {
    char buffer[96];
    char *saved_buffer = format_buffer;
    int saved_count = format_count;
    int saved_limit = format_limit;
    format_buffer = buffer;
    format_count = 0;
    format_limit = -1;
    print_fixed(value, precision, 0);
    int length = format_count;
    if (length > (int)sizeof(buffer) - 1) {
        length = (int)sizeof(buffer) - 1;
    }
    buffer[length] = 0;
    format_buffer = saved_buffer;
    format_count = saved_count;
    format_limit = saved_limit;
    const int negative = length > 0 && buffer[0] == '-';
    int sign = 0;
    int start = 0;
    if (negative) {
        sign = '-';
        start = 1;
    } else if ((flags & PRINT_PLUS) != 0) {
        sign = '+';
    } else if ((flags & PRINT_SPACE) != 0) {
        sign = ' ';
    }
    const int body = length - start;
    const int total = body + (sign ? 1 : 0);
    int padding = width > total ? width - total : 0;
    if ((flags & PRINT_LEFT) == 0 &&
        (flags & PRINT_ZERO_PAD) == 0) {
        while (padding-- > 0) {
            emit_char(' ');
        }
    }
    if (sign) {
        emit_char(sign);
    }
    if ((flags & PRINT_LEFT) == 0 &&
        (flags & PRINT_ZERO_PAD) != 0) {
        while (padding-- > 0) {
            emit_char('0');
        }
    }
    for (int index = start; index < length; index++) {
        emit_char(buffer[index]);
    }
    if ((flags & PRINT_LEFT) != 0) {
        while (padding-- > 0) {
            emit_char(' ');
        }
    }
}

static void print_exponent(double value, int precision) {
    if (value < 0.0) {
        emit_char('-');
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
    emit_char('e');
    if (exponent < 0) {
        emit_char('-');
        exponent = -exponent;
    } else {
        emit_char('+');
    }
    if (exponent < 10) {
        emit_char('0');
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

static int format_output(const char *format, va_list arguments) {
    int written = 0;
    while (*format) {
        if (*format != '%') {
            emit_char(*format);
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
        int long_count = 0;
        while (*format == 'l') {
            long_count++;
            format++;
        }
        if (*format == 'z') {
            format++;
        }
        if (*format == '%') {
            emit_char('%');
            written++;
        } else if (*format == 'd' || *format == 'i') {
            int flags = (zero_pad ? PRINT_ZERO_PAD : 0) |
                        (left_align ? PRINT_LEFT : 0) |
                        (plus ? PRINT_PLUS : 0) |
                        (space ? PRINT_SPACE : 0) |
                        (precision_set ? (precision << 8) : 0);
            if (long_count >= 2) {
                long long value = va_arg(arguments, long long);
                unsigned long long magnitude =
                    value < 0 ? (unsigned long long)(-(value + 1)) + 1
                              : (unsigned long long)value;
                print_integer(magnitude, 10, width,
                              flags | (value < 0 ? PRINT_NEGATIVE : 0));
            } else {
                int value = va_arg(arguments, int);
                print_integer(value < 0 ? (unsigned int)(-value)
                                        : (unsigned int)value,
                              10, width,
                              flags | (value < 0 ? PRINT_NEGATIVE : 0));
            }
        } else if (*format == 'u') {
            int flags = (zero_pad ? PRINT_ZERO_PAD : 0) |
                        (left_align ? PRINT_LEFT : 0) |
                        (precision_set ? (precision << 8) : 0);
            if (long_count >= 2) {
                print_integer(va_arg(arguments, unsigned long long), 10,
                              width, flags);
            } else {
                print_integer(va_arg(arguments, unsigned int), 10, width,
                              flags);
            }
        } else if (*format == 'o') {
            int flags = (zero_pad ? PRINT_ZERO_PAD : 0) |
                        (left_align ? PRINT_LEFT : 0) |
                        (precision_set ? (precision << 8) : 0);
            if (long_count >= 2) {
                print_integer(va_arg(arguments, unsigned long long), 8,
                              width, flags);
            } else {
                print_integer(va_arg(arguments, unsigned int), 8, width,
                              flags);
            }
        } else if (*format == 'x' || *format == 'X') {
            int flags = (*format == 'X' ? PRINT_UPPER : 0) |
                        (alternate ? PRINT_PREFIX : 0) |
                        (zero_pad ? PRINT_ZERO_PAD : 0) |
                        (left_align ? PRINT_LEFT : 0) |
                        (precision_set ? (precision << 8) : 0);
            if (long_count >= 2) {
                print_integer(va_arg(arguments, unsigned long long), 16,
                              width, flags);
            } else {
                print_integer(va_arg(arguments, unsigned int), 16, width,
                              flags);
            }
        } else if (*format == 'p') {
            print_integer((unsigned int)va_arg(arguments, void *), 16, 0,
                          PRINT_PREFIX);
        } else if (*format == 'h') {
            emit_char('0');
            emit_char('x');
            print_hex((unsigned int)va_arg(arguments, void *));
            written += 2;
        } else if (*format == 's') {
            int flags = (precision_set ? 2 : 0) |
                        (left_align ? 1 : 0);
            print_text(va_arg(arguments, char *), width, precision, flags);
        } else if (*format == 'c') {
            emit_char(va_arg(arguments, int));
            written++;
        } else if (*format == 'f' || *format == 'F') {
            int flags = (zero_pad ? PRINT_ZERO_PAD : 0) |
                        (left_align ? PRINT_LEFT : 0) |
                        (plus ? PRINT_PLUS : 0) |
                        (space ? PRINT_SPACE : 0);
            print_float_formatted(va_arg(arguments, double), precision,
                                  width, flags);
        } else if (*format == 'e' || *format == 'E') {
            print_exponent(va_arg(arguments, double), precision);
        } else if (*format == 'g' || *format == 'G') {
            print_general(va_arg(arguments, double), precision);
        } else {
            emit_char('%');
            emit_char(*format);
            written += 2;
        }
        format++;
    }
    return written;
}

int printf(const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    int written = format_output(format, arguments);
    va_end(arguments);
    return written;
}

int sprintf(char *destination, const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    char *saved_buffer = format_buffer;
    int saved_count = format_count;
    int saved_limit = format_limit;
    format_buffer = destination;
    format_count = 0;
    format_limit = -1;
    format_output(format, arguments);
    destination[format_count] = 0;
    int result = format_count;
    format_buffer = saved_buffer;
    format_count = saved_count;
    format_limit = saved_limit;
    va_end(arguments);
    return result;
}

int snprintf(char *destination, unsigned int size, const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    char *saved_buffer = format_buffer;
    int saved_count = format_count;
    int saved_limit = format_limit;
    format_buffer = destination;
    format_count = 0;
    format_limit = (int)size;
    format_output(format, arguments);
    int result = format_count;
    if (size > 0) {
        int stored = format_count < (int)size ? format_count : (int)size - 1;
        destination[stored] = 0;
    }
    format_buffer = saved_buffer;
    format_count = saved_count;
    format_limit = saved_limit;
    va_end(arguments);
    return result;
}

int vsnprintf(char *destination, unsigned int size, const char *format,
              va_list arguments) {
    char *saved_buffer = format_buffer;
    int saved_count = format_count;
    int saved_limit = format_limit;
    format_buffer = destination;
    format_count = 0;
    format_limit = (int)size;
    format_output(format, arguments);
    const int result = format_count;
    if (size > 0) {
        const int stored = format_count < (int)size ? format_count : (int)size - 1;
        destination[stored] = 0;
    }
    format_buffer = saved_buffer;
    format_count = saved_count;
    format_limit = saved_limit;
    return result;
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
int sys_random(void) {    random_state = random_state * 1103515245u + 12345u;
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

char *strcat(char *destination, const char *source) {
    char *cursor = destination;
    while (*cursor) {
        cursor++;
    }
    while (*source) {
        *cursor = *source;
        cursor++;
        source++;
    }
    *cursor = 0;
    return destination;
}

char *strncpy(char *destination, const char *source, unsigned int count) {
    unsigned int index = 0;
    while (index < count && source[index]) {
        destination[index] = source[index];
        index++;
    }
    while (index < count) {
        destination[index] = 0;
        index++;
    }
    return destination;
}

char *strchr(const char *text, int character) {
    char target = (char)character;
    while (*text) {
        if (*text == target) {
            return (char *)text;
        }
        text++;
    }
    return target == 0 ? (char *)text : 0;
}

char *strstr(const char *text, const char *needle) {
    if (*needle == 0) {
        return (char *)text;
    }
    while (*text) {
        const char *left = text;
        const char *right = needle;
        while (*left && *right && *left == *right) {
            left++;
            right++;
        }
        if (*right == 0) {
            return (char *)text;
        }
        text++;
    }
    return 0;
}

static int character_in_set(char character, const char *set) {
    while (*set) {
        if (*set == character) {
            return 1;
        }
        set++;
    }
    return 0;
}

unsigned int strspn(const char *text, const char *accept) {
    unsigned int count = 0;
    while (text[count] && character_in_set(text[count], accept)) {
        count++;
    }
    return count;
}

unsigned int strcspn(const char *text, const char *reject) {
    unsigned int count = 0;
    while (text[count] && !character_in_set(text[count], reject)) {
        count++;
    }
    return count;
}

char *strpbrk(const char *text, const char *accept) {
    while (*text) {
        if (character_in_set(*text, accept)) {
            return (char *)text;
        }
        text++;
    }
    return 0;
}

static char *token_cursor;

char *strtok(char *text, const char *delimiters) {
    if (text == 0) {
        text = token_cursor;
    }
    if (text == 0) {
        return 0;
    }
    while (*text && character_in_set(*text, delimiters)) {
        text++;
    }
    if (*text == 0) {
        token_cursor = text;
        return 0;
    }
    char *token = text;
    while (*text && !character_in_set(*text, delimiters)) {
        text++;
    }
    if (*text) {
        *text = 0;
        text++;
    }
    token_cursor = text;
    return token;
}

char *strtok_r(char *text, const char *delimiters, char **save) {
    if (save == 0) {
        return 0;
    }
    if (text == 0) {
        text = *save;
    }
    if (text == 0) {
        return 0;
    }
    while (*text && character_in_set(*text, delimiters)) {
        text++;
    }
    if (*text == 0) {
        *save = text;
        return 0;
    }
    char *token = text;
    while (*text && !character_in_set(*text, delimiters)) {
        text++;
    }
    if (*text) {
        *text = 0;
        text++;
    }
    *save = text;
    return token;
}

int atoi(const char *text) {
    int sign = 1;
    int value = 0;
    while (*text == ' ' || *text == '\t' || *text == '\n' ||
           *text == '\r') {
        text++;
    }
    if (*text == '-') {
        sign = -1;
        text++;
    } else if (*text == '+') {
        text++;
    }
    while (*text >= '0' && *text <= '9') {
        value = value * 10 + (*text - '0');
        text++;
    }
    return sign * value;
}

long atol(const char *text) {
    return (long)atoi(text);
}

double strtod(const char *text, char **end) {
    const char *cursor = text;
    while (*cursor == ' ' || *cursor == '\t' || *cursor == '\n' ||
           *cursor == '\r') {
        cursor++;
    }
    int negative = 0;
    if (*cursor == '+' || *cursor == '-') {
        negative = *cursor == '-';
        cursor++;
    }
    double value = 0.0;
    int digits = 0;
    while (*cursor >= '0' && *cursor <= '9') {
        value = value * 10.0 + (double)(*cursor - '0');
        cursor++;
        digits++;
    }
    if (*cursor == '.') {
        cursor++;
        double scale = 0.1;
        while (*cursor >= '0' && *cursor <= '9') {
            value += (double)(*cursor - '0') * scale;
            scale *= 0.1;
            cursor++;
            digits++;
        }
    }
    if (digits > 0 && (*cursor == 'e' || *cursor == 'E')) {
        const char *exponent_start = cursor;
        cursor++;
        int exponent_negative = 0;
        if (*cursor == '+' || *cursor == '-') {
            exponent_negative = *cursor == '-';
            cursor++;
        }
        int exponent = 0;
        int exponent_digits = 0;
        while (*cursor >= '0' && *cursor <= '9') {
            exponent = exponent * 10 + (*cursor - '0');
            cursor++;
            exponent_digits++;
        }
        if (exponent_digits == 0) {
            cursor = exponent_start;
        } else {
            for (int index = 0; index < exponent; index++) {
                value = exponent_negative ? value / 10.0 : value * 10.0;
            }
        }
    }
    if (end) {
        *end = (char *)(digits > 0 ? cursor : text);
    }
    return negative ? -value : value;
}

double atof(const char *text) {
    return strtod(text, 0);
}

float strtof(const char *text, char **end) {
    return (float)strtod(text, end);
}

long long llabs(long long value) {
    return value < 0 ? -value : value;
}

double fabs(double value) {    return value < 0.0 ? -value : value;
}

double floor(double value) {
    long truncated = (long)value;
    if ((double)truncated > value) {
        truncated = truncated - 1;
    }
    return (double)truncated;
}

double ceil(double value) {
    long truncated = (long)value;
    if ((double)truncated < value) {
        truncated = truncated + 1;
    }
    return (double)truncated;
}

double sqrt(double value) {
    if (value <= 0.0) {
        return 0.0;
    }
    double estimate = value >= 1.0 ? value : 1.0;
    for (int iteration = 0; iteration < 32; iteration++) {
        estimate = (estimate + value / estimate) / 2.0;
    }
    return estimate;
}

int abs(int value) {
    return value < 0 ? -value : value;
}

static int digit_value(char character) {
    if (character >= '0' && character <= '9') {
        return character - '0';
    }
    if (character >= 'a' && character <= 'z') {
        return character - 'a' + 10;
    }
    if (character >= 'A' && character <= 'Z') {
        return character - 'A' + 10;
    }
    return 99;
}

long strtol(const char *text, char **end, int base) {
    const char *cursor = text;
    while (*cursor == ' ' || *cursor == '\t' || *cursor == '\n' ||
           *cursor == '\r') {
        cursor++;
    }
    int sign = 1;
    if (*cursor == '-') {
        sign = -1;
        cursor++;
    } else if (*cursor == '+') {
        cursor++;
    }
    if (base == 0) {
        if (cursor[0] == '0' &&
            (cursor[1] == 'x' || cursor[1] == 'X')) {
            base = 16;
            cursor += 2;
        } else if (cursor[0] == '0') {
            base = 8;
        } else {
            base = 10;
        }
    } else if (base == 16 && cursor[0] == '0' &&
               (cursor[1] == 'x' || cursor[1] == 'X')) {
        cursor += 2;
    }
    unsigned long value = 0;
    int digits = 0;
    while (digit_value(*cursor) < base) {
        value = value * (unsigned long)base +
                (unsigned long)digit_value(*cursor);
        cursor++;
        digits++;
    }
    if (end) {
        *end = (char *)(digits > 0 ? cursor : text);
    }
    return sign * (long)value;
}

unsigned long strtoul(const char *text, char **end, int base) {
    const char *cursor = text;
    while (*cursor == ' ' || *cursor == '\t' || *cursor == '\n' ||
           *cursor == '\r') {
        cursor++;
    }
    if (*cursor == '+') {
        cursor++;
    }
    if (base == 0) {
        if (cursor[0] == '0' &&
            (cursor[1] == 'x' || cursor[1] == 'X')) {
            base = 16;
            cursor += 2;
        } else if (cursor[0] == '0') {
            base = 8;
        } else {
            base = 10;
        }
    } else if (base == 16 && cursor[0] == '0' &&
               (cursor[1] == 'x' || cursor[1] == 'X')) {
        cursor += 2;
    }
    unsigned long value = 0;
    int digits = 0;
    while (digit_value(*cursor) < base) {
        value = value * (unsigned long)base +
                (unsigned long)digit_value(*cursor);
        cursor++;
        digits++;
    }
    if (end) {
        *end = (char *)(digits > 0 ? cursor : text);
    }
    return value;
}

void srand(unsigned int seed) {
    random_state = seed;
}

int rand(void) {
    return sys_random();
}

char *strrchr(const char *text, int character) {
    char target = (char)character;
    const char *last = 0;
    while (*text) {
        if (*text == target) {
            last = text;
        }
        text++;
    }
    if (target == 0) {
        return (char *)text;
    }
    return (char *)last;
}

char *strncat(char *destination, const char *source, unsigned int count) {
    char *cursor = destination;
    while (*cursor) {
        cursor++;
    }
    unsigned int index = 0;
    while (index < count && source[index]) {
        cursor[index] = source[index];
        index++;
    }
    cursor[index] = 0;
    return destination;
}

void qsort(void *base, unsigned int count, unsigned int size,
           int (*compare)(const void *, const void *)) {
    unsigned char *bytes = (unsigned char *)base;
    for (unsigned int index = 1; index < count; index++) {
        unsigned int position = index;
        while (position > 0 &&
               compare(bytes + (position - 1) * size,
                       bytes + position * size) > 0) {
            for (unsigned int byte = 0; byte < size; byte++) {
                unsigned char temporary = bytes[(position - 1) * size + byte];
                bytes[(position - 1) * size + byte] =
                    bytes[position * size + byte];
                bytes[position * size + byte] = temporary;
            }
            position--;
        }
    }
}

void *bsearch(const void *key, const void *base, unsigned int count,
              unsigned int size,
              int (*compare)(const void *, const void *)) {
    const unsigned char *bytes = (const unsigned char *)base;
    unsigned int low = 0;
    unsigned int high = count;
    while (low < high) {
        unsigned int middle = low + (high - low) / 2;
        int result = compare(key, bytes + middle * size);
        if (result == 0) {
            return (void *)(bytes + middle * size);
        }
        if (result < 0) {
            high = middle;
        } else {
            low = middle + 1;
        }
    }
    return 0;
}

char *strdup(const char *source) {    unsigned int length = strlen(source);
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

char *strndup(const char *source, unsigned int maximum) {
    unsigned int length = 0;
    while (length < maximum && source[length] != 0) {
        length++;
    }
    char *result = (char *)malloc(length + 1);
    if (result == 0) {
        return 0;
    }
    for (unsigned int index = 0; index < length; index++) {
        result[index] = source[index];
    }
    result[length] = 0;
    return result;
}

int strcoll(const char *left, const char *right) {
    return strcmp(left, right);
}

unsigned int strxfrm(char *destination, const char *source,
                     unsigned int count) {
    unsigned int length = strlen(source);
    if (count > 0) {
        unsigned int copied = length < count - 1 ? length : count - 1;
        for (unsigned int index = 0; index < copied; index++) {
            destination[index] = source[index];
        }
        destination[copied] = 0;
    }
    return length;
}

void *memccpy(void *destination, const void *source, int character,
              unsigned int count) {
    unsigned char *to = (unsigned char *)destination;
    const unsigned char *from = (const unsigned char *)source;
    for (unsigned int index = 0; index < count; index++) {
        to[index] = from[index];
        if (from[index] == (unsigned char)character) {
            return to + index + 1;
        }
    }
    return 0;
}

void *memcpy(void *destination, const void *source, unsigned int count) {
    unsigned char *to = (unsigned char *)destination;
    const unsigned char *from = (const unsigned char *)source;
    for (unsigned int index = 0; index < count; index++) {
        to[index] = from[index];
    }
    return destination;
}

void *memmove(void *destination, const void *source, unsigned int count) {
    unsigned char *to = (unsigned char *)destination;
    const unsigned char *from = (const unsigned char *)source;
    if (to < from) {
        for (unsigned int index = 0; index < count; index++) {
            to[index] = from[index];
        }
    } else if (to > from) {
        for (unsigned int index = count; index > 0; index--) {
            to[index - 1] = from[index - 1];
        }
    }
    return destination;
}

void *memchr(const void *memory, int character, unsigned int count) {
    const unsigned char *bytes = (const unsigned char *)memory;
    const unsigned char target = (unsigned char)character;
    for (unsigned int index = 0; index < count; index++) {
        if (bytes[index] == target) {
            return (void *)(bytes + index);
        }
    }
    return 0;
}

unsigned int strnlen(const char *text, unsigned int limit) {
    unsigned int length = 0;
    while (length < limit && text[length]) {
        length++;
    }
    return length;
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

static int host_result(int result) {
    if (result < 0) {
        krc_errno = -result;
        return -1;
    }
    return result;
}

int getchar(void) {
    return krc_sys_getchar();
}

int open(const char *path, int flags) {
    return host_result(krc_sys_open(path, flags));
}

int close(int descriptor) {
    return host_result(krc_sys_close(descriptor));
}

int read(int descriptor, void *buffer, int count) {
    return host_result(krc_sys_read(descriptor, buffer, count));
}

int write(int descriptor, const void *buffer, int count) {
    return host_result(krc_sys_write(descriptor, buffer, count));
}

int seek(int descriptor, int offset, int whence) {
    return host_result(krc_sys_seek(descriptor, offset, whence));
}

int unlink(const char *path) {
    return host_result(krc_sys_unlink(path));
}

int rename(const char *old_path, const char *new_path) {
    return host_result(krc_sys_rename(old_path, new_path));
}

int stat(const char *path, void *buffer, int capacity) {
    return host_result(krc_sys_stat(path, buffer, capacity));
}

int truncate(const char *path, int length) {
    return host_result(krc_sys_truncate(path, length));
}

int getcwd(char *buffer, int capacity) {
    return host_result(krc_sys_getcwd(buffer, capacity));
}

int chdir(const char *path) {
    return host_result(krc_sys_chdir(path));
}

int dup(int descriptor) {
    return host_result(krc_sys_dup(descriptor));
}

int dup2(int source, int target) {
    return host_result(krc_sys_dup2(source, target));
}

int sys_time(void) {
    return krc_sys_time();
}

int sys_argc(void) {
    return krc_sys_argc();
}

int sys_arg(int index, char *buffer, int size) {
    return host_result(krc_sys_arg(index, buffer, size));
}

char *strerror(int code) {
    switch (code) {
    case 0:
        return "no error";
    case 2:
        return "No such file or directory";
    case 12:
        return "Cannot allocate memory";
    case 22:
        return "Invalid argument";
    case 33:
        return "Domain error";
    case 34:
        return "Result out of range";
    default:
        return "Unknown error";
    }
}

void perror(const char *prefix) {
    if (prefix != 0 && *prefix != 0) {
        while (*prefix != 0) {
            putchar(*prefix++);
        }
        putchar(':');
        putchar(' ');
    }
    puts(strerror(krc_errno));
}

struct FILE {
    int descriptor;
    int eof;
    int error;
    int pushback;
};

static struct FILE standard_input = {0, 0, 0, -1};
static struct FILE standard_output = {1, 0, 0, -1};
static struct FILE standard_error = {2, 0, 0, -1};

struct FILE *stdin = &standard_input;
struct FILE *stdout = &standard_output;
struct FILE *stderr = &standard_error;

struct FILE *__krc_stdin_stream(void) {
    return &standard_input;
}

struct FILE *__krc_stdout_stream(void) {
    return &standard_output;
}

struct FILE *__krc_stderr_stream(void) {
    return &standard_error;
}

static int stdio_flags(const char *mode, int *ok) {
    int flags = 0;
    int plus = 0;
    *ok = 1;
    if (mode == 0 || *mode == 0) {
        *ok = 0;
        return 0;
    }
    if (*mode == 'r') {
        flags = 0;
    } else if (*mode == 'w') {
        flags = 1 | 4 | 8;
    } else if (*mode == 'a') {
        flags = 1 | 4 | 16;
    } else {
        *ok = 0;
        return 0;
    }
    for (const char *cursor = mode + 1; *cursor != 0; ++cursor) {
        if (*cursor == '+') {
            plus = 1;
        }
    }
    if (plus) {
        flags = (flags & ~3) | 2;
    }
    return flags;
}

struct FILE *fopen(const char *path, const char *mode) {
    int ok = 0;
    const int flags = stdio_flags(mode, &ok);
    if (!ok) {
        krc_errno = 22;
        return 0;
    }
    const int descriptor = open(path, flags);
    if (descriptor < 0) {
        return 0;
    }
    struct FILE *stream = (struct FILE *)malloc(sizeof(struct FILE));
    if (stream == 0) {
        close(descriptor);
        krc_errno = 12;
        return 0;
    }
    stream->descriptor = descriptor;
    stream->eof = 0;
    stream->error = 0;
    stream->pushback = -1;
    return stream;
}

int fclose(struct FILE *stream) {
    if (stream == 0 || stream == stdin || stream == stdout || stream == stderr) {
        return 0;
    }
    const int result = close(stream->descriptor);
    free(stream);
    return result;
}

int fflush(struct FILE *stream) {
    (void)stream;
    return 0;
}

int fseek(struct FILE *stream, long offset, int origin) {
    if (stream == 0) {
        return -1;
    }
    const int result = seek(stream->descriptor, (int)offset, origin);
    if (result < 0) {
        stream->error = 1;
        return -1;
    }
    stream->eof = 0;
    return 0;
}

long ftell(struct FILE *stream) {
    if (stream == 0) {
        return -1;
    }
    return (long)seek(stream->descriptor, 0, 1);
}

unsigned int fread(void *buffer, unsigned int size, unsigned int count,
                   struct FILE *stream) {
    if (stream == 0 || size == 0 || count == 0) {
        return 0;
    }
    const unsigned int total = size * count;
    const int result = read(stream->descriptor, buffer, (int)total);
    if (result < 0) {
        stream->error = 1;
        return 0;
    }
    if ((unsigned int)result < total) {
        stream->eof = 1;
    }
    return (unsigned int)result / size;
}

unsigned int fwrite(const void *buffer, unsigned int size, unsigned int count,
                    struct FILE *stream) {
    if (stream == 0 || size == 0 || count == 0) {
        return 0;
    }
    const unsigned int total = size * count;
    const int result = write(stream->descriptor, buffer, (int)total);
    if (result < 0) {
        stream->error = 1;
        return 0;
    }
    return (unsigned int)result / size;
}

int getc(struct FILE *stream) {
    if (stream == 0) {
        return -1;
    }
    if (stream->pushback >= 0) {
        const int value = stream->pushback;
        stream->pushback = -1;
        return value;
    }
    unsigned char value = 0;
    if (fread(&value, 1, 1, stream) != 1) {
        return -1;
    }
    return (int)value;
}

int ungetc(int character, struct FILE *stream) {
    if (stream == 0 || character < 0) {
        return -1;
    }
    stream->pushback = character & 0xFF;
    stream->eof = 0;
    return character & 0xFF;
}

int setvbuf(struct FILE *stream, char *buffer, int mode, unsigned int size) {
    (void)stream;
    (void)buffer;
    (void)mode;
    (void)size;
    return 0;
}

char *fgets(char *buffer, int size, struct FILE *stream) {
    if (buffer == 0 || size <= 0 || stream == 0) {
        return 0;
    }
    int index = 0;
    while (index + 1 < size) {
        const int character = getc(stream);
        if (character < 0) {
            break;
        }
        buffer[index++] = (char)character;
        if (character == '\n') {
            break;
        }
    }
    if (index == 0) {
        return 0;
    }
    buffer[index] = 0;
    return buffer;
}

int getline(char **line, unsigned int *capacity, struct FILE *stream) {
    if (line == 0 || capacity == 0 || stream == 0) {
        return -1;
    }
    if (*line == 0 || *capacity == 0) {
        *capacity = 128;
        *line = (char *)malloc(*capacity);
        if (*line == 0) {
            return -1;
        }
    }
    unsigned int length = 0;
    for (;;) {
        const int character = getc(stream);
        if (character < 0) {
            if (length == 0) {
                return -1;
            }
            break;
        }
        if (length + 1 >= *capacity) {
            const unsigned int grown = *capacity * 2;
            char *larger = (char *)realloc(*line, grown);
            if (larger == 0) {
                return -1;
            }
            *line = larger;
            *capacity = grown;
        }
        (*line)[length++] = (char)character;
        if (character == '\n') {
            break;
        }
    }
    (*line)[length] = 0;
    return (int)length;
}

int vfprintf(struct FILE *stream, const char *format, va_list arguments) {
    if (stream == 0 || format == 0) {
        return -1;
    }
    char buffer[512];
    const int length = vsnprintf(buffer, sizeof(buffer), format, arguments);
    if (length < 0) {
        return -1;
    }
    const unsigned int written =
        (unsigned int)length < sizeof(buffer) ? (unsigned int)length
                                              : sizeof(buffer) - 1;
    fwrite(buffer, 1, written, stream);
    return (int)written;
}

int fprintf(struct FILE *stream, const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    const int result = vfprintf(stream, format, arguments);
    va_end(arguments);
    return result;
}

int vprintf(const char *format, va_list arguments) {
    return vfprintf(stdout, format, arguments);
}

int fgetc(struct FILE *stream) {
    return getc(stream);
}

int fputc(int character, struct FILE *stream) {
    const unsigned char value = (unsigned char)character;
    if (fwrite(&value, 1, 1, stream) != 1) {
        return -1;
    }
    return character;
}

int remove(const char *path) {
    return unlink(path);
}

static int scan_space(char character) {
    if (character == ' ' || character == '\t' || character == '\n' ||
        character == '\r' || character == 11 || character == 12) {
        return 1;
    }
    return 0;
}

static void skip_space(const char **cursor) {
    while (scan_space(**cursor)) {
        (*cursor)++;
    }
}

static int scan_integer(const char **cursor, int base, long *value,
                        int *negative) {
    const char *text = *cursor;
    int sign = 1;
    if (*text == '-') {
        sign = -1;
        text++;
    } else if (*text == '+') {
        text++;
    }
    long result = 0;
    int digits = 0;
    for (;;) {
        const char character = *text;
        int digit = -1;
        if (character >= '0' && character <= '9') {
            digit = character - '0';
        } else if (character >= 'a' && character <= 'f') {
            digit = character - 'a' + 10;
        } else if (character >= 'A' && character <= 'F') {
            digit = character - 'A' + 10;
        }
        if (digit < 0 || digit >= base) {
            break;
        }
        result = result * base + digit;
        digits++;
        text++;
    }
    if (digits == 0) {
        return 0;
    }
    *cursor = text;
    *value = result * sign;
    *negative = sign < 0;
    return 1;
}

static int scan_text(const char *input, const char *format,
                     va_list arguments) {
    const char *cursor = input;
    int assigned = 0;
    while (*format) {
        if (scan_space(*format)) {
            skip_space(&cursor);
            while (scan_space(*format)) {
                format++;
            }
            continue;
        }
        if (*format != '%') {
            if (*cursor != *format) {
                break;
            }
            cursor++;
            format++;
            continue;
        }
        format++;
        int suppress = 0;
        if (*format == '*') {
            suppress = 1;
            format++;
        }
        int width = 0;
        while (*format >= '0' && *format <= '9') {
            width = width * 10 + (*format - '0');
            format++;
        }
        int long_modifier = 0;
        if (*format == 'l') {
            long_modifier = 1;
            format++;
        }
        const char conversion = *format;
        if (conversion == '%') {
            if (*cursor != '%') {
                break;
            }
            cursor++;
            format++;
            continue;
        }
        if (conversion == 0) {
            break;
        }
        format++;
        if (conversion == 'd' || conversion == 'i' || conversion == 'u' ||
            conversion == 'x') {
            skip_space(&cursor);
            const int base = conversion == 'x' ? 16
                              : conversion == 'i' ? 10
                                                  : 10;
            long value = 0;
            int negative = 0;
            if (!scan_integer(&cursor, base, &value, &negative)) {
                break;
            }
            if (!suppress) {
                if (long_modifier) {
                    *va_arg(arguments, long *) = value;
                } else if (conversion == 'u' || conversion == 'x') {
                    *va_arg(arguments, unsigned int *) = (unsigned int)value;
                } else {
                    *va_arg(arguments, int *) = (int)value;
                }
                assigned++;
            }
        } else if (conversion == 's') {
            skip_space(&cursor);
            if (*cursor == 0) {
                break;
            }
            char *destination = suppress ? 0 : va_arg(arguments, char *);
            int copied = 0;
            while (*cursor != 0 && !scan_space(*cursor) &&
                   (width == 0 || copied < width)) {
                if (destination) {
                    destination[copied] = *cursor;
                }
                copied++;
                cursor++;
            }
            if (destination) {
                destination[copied] = 0;
                assigned++;
            }
        } else if (conversion == 'c') {
            const int count = width == 0 ? 1 : width;
            char *destination = suppress ? 0 : va_arg(arguments, char *);
            for (int index = 0; index < count; index++) {
                if (*cursor == 0) {
                    break;
                }
                if (destination) {
                    destination[index] = *cursor;
                }
                cursor++;
            }
            if (destination) {
                assigned++;
            }
        } else {
            break;
        }
    }
    return assigned;
}

int sscanf(const char *input, const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    const int assigned = scan_text(input, format, arguments);
    va_end(arguments);
    return assigned;
}

int vsscanf(const char *input, const char *format, va_list arguments) {
    return scan_text(input, format, arguments);
}

int vfscanf(struct FILE *stream, const char *format, va_list arguments) {
    if (stream == 0) {
        return -1;
    }
    char buffer[4096];
    const unsigned int length = fread(buffer, 1, sizeof(buffer) - 1, stream);
    buffer[length] = 0;
    return scan_text(buffer, format, arguments);
}

int fscanf(struct FILE *stream, const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    const int assigned = vfscanf(stream, format, arguments);
    va_end(arguments);
    return assigned;
}

int scanf(const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    const int assigned = vfscanf(stdin, format, arguments);
    va_end(arguments);
    return assigned;
}

long time(long *result) {
    const long now = (long)sys_time();
    if (result != 0) {
        *result = now;
    }
    return now;
}

double difftime(long end, long beginning) {
    return (double)end - (double)beginning;
}

long clock(void) {
    return (long)sys_time() * 1000000L;
}

long labs(long value) {
    return value < 0 ? -value : value;
}

long long atoll(const char *text) {
    return (long long)atol(text);
}

long long strtoll(const char *text, char **end, int base) {
    return (long long)strtol(text, end, base);
}

unsigned long long strtoull(const char *text, char **end, int base) {
    return (unsigned long long)strtoul(text, end, base);
}

void _Exit(int status) {
    exit(status);
}

