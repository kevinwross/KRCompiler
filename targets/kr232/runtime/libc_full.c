#include <stdarg.h>
#include <stdlib.h>
#include <time.h>
#include <locale.h>

int krc_errno = 0;

int *__krc_errno_location(void) {
    return &krc_errno;
}

/*
 * dynamic_cast support, docs/abi.md: an object's vptr sits at offset 0 and the
 * typeinfo pointer one word before the first virtual slot. Each typeinfo names
 * its base's typeinfo in its fourth word, so walking that chain tells whether
 * the object's dynamic type is the target or derives from it. Single
 * inheritance means the address needs no adjustment.
 */
void *__krc_dynamic_cast(unsigned char *object, unsigned char *target_type) {
    unsigned char *vptr;
    unsigned char *typeinfo;
    if (object == 0 || target_type == 0) {
        return 0;
    }
    vptr = *(unsigned char **)object;
    if (vptr == 0) {
        return 0;
    }
    typeinfo = *(unsigned char **)(vptr - 4);
    while (typeinfo != 0) {
        if (typeinfo == target_type) {
            return object;
        }
        typeinfo = *(unsigned char **)(typeinfo + 12);
    }
    return 0;
}

/*
 * 64-bit integer helpers, plan.md §14.7. The KR32 backend lowers a `long`
 * value to a word pair and calls these entry points for division and
 * remainder, because the ISA divides only 32-bit values.
 *
 * The pair travels as an eight-byte aggregate in the argument registers
 * R0..R3 and returns in R0/R1 (plan.md §10.2). Each body uses 32-bit words
 * only: calling a 64-bit operator here would recurse into the helper that
 * implements it.
 *
 * __krc_pair64_divide publishes its results in file-scope variables rather
 * than through pointer parameters because the four entry points need both
 * results from one pass. The compiler never calls a helper from inside
 * another, so one set of results is enough; the runtime is single-threaded.
 */
struct __krc_pair64 {
    unsigned int lo;
    unsigned int hi;
};

static struct __krc_pair64 __krc_divide_quotient;
static struct __krc_pair64 __krc_divide_remainder;

static int __krc_pair64_zero(struct __krc_pair64 value) {
    return value.lo == 0u && value.hi == 0u;
}

static int __krc_pair64_negative(struct __krc_pair64 value) {
    return (value.hi & 0x80000000u) != 0u;
}

static int __krc_pair64_unsigned_less(struct __krc_pair64 left,
                                      struct __krc_pair64 right) {
    if (left.hi != right.hi) {
        return left.hi < right.hi;
    }
    return left.lo < right.lo;
}

static struct __krc_pair64 __krc_pair64_negate(struct __krc_pair64 value) {
    struct __krc_pair64 result;
    result.lo = (~value.lo) + 1u;
    result.hi = (~value.hi) + (result.lo == 0u ? 1u : 0u);
    return result;
}

/* Shift-subtract long division. A set carry out of the top bit means the
 * shifted remainder exceeded 64 bits, in which case the wrapped value already
 * exceeds any divisor and the subtraction still applies. */
static void __krc_pair64_divide(struct __krc_pair64 dividend,
                                struct __krc_pair64 divisor) {
    struct __krc_pair64 partial;
    struct __krc_pair64 result;
    int bit;
    partial.lo = 0u;
    partial.hi = 0u;
    result.lo = 0u;
    result.hi = 0u;
    if (__krc_pair64_zero(divisor)) {
        /* The ISA traps on a zero divisor; the helper reports zero rather
         * than faulting inside a runtime routine. */
        __krc_divide_quotient = partial;
        __krc_divide_remainder = partial;
        return;
    }
    for (bit = 63; bit >= 0; --bit) {
        unsigned int next = bit >= 32 ? ((dividend.hi >> (bit - 32)) & 1u)
                                      : ((dividend.lo >> bit) & 1u);
        unsigned int carry = partial.hi >> 31;
        unsigned int borrow;
        partial.hi = (partial.hi << 1) | (partial.lo >> 31);
        partial.lo = (partial.lo << 1) | next;
        if (carry != 0u || !__krc_pair64_unsigned_less(partial, divisor)) {
            borrow = partial.lo < divisor.lo ? 1u : 0u;
            partial.lo = partial.lo - divisor.lo;
            partial.hi = partial.hi - divisor.hi - borrow;
            if (bit >= 32) {
                result.hi |= 1u << (bit - 32);
            } else {
                result.lo |= 1u << bit;
            }
        }
    }
    __krc_divide_quotient = result;
    __krc_divide_remainder = partial;
}

struct __krc_pair64 __krc_udivdi3(struct __krc_pair64 left,
                                  struct __krc_pair64 right) {
    struct __krc_pair64 quotient;
    __krc_pair64_divide(left, right);
    quotient = __krc_divide_quotient;
    return quotient;
}

struct __krc_pair64 __krc_umoddi3(struct __krc_pair64 left,
                                  struct __krc_pair64 right) {
    struct __krc_pair64 remainder;
    __krc_pair64_divide(left, right);
    remainder = __krc_divide_remainder;
    return remainder;
}

struct __krc_pair64 __krc_sdivdi3(struct __krc_pair64 left,
                                  struct __krc_pair64 right) {
    struct __krc_pair64 magnitude_left;
    struct __krc_pair64 magnitude_right;
    struct __krc_pair64 quotient;
    int negative = __krc_pair64_negative(left) != __krc_pair64_negative(right);
    magnitude_left = left;
    magnitude_right = right;
    if (__krc_pair64_negative(left)) {
        struct __krc_pair64 negated = __krc_pair64_negate(left);
        magnitude_left = negated;
    }
    if (__krc_pair64_negative(right)) {
        struct __krc_pair64 negated = __krc_pair64_negate(right);
        magnitude_right = negated;
    }
    __krc_pair64_divide(magnitude_left, magnitude_right);
    quotient = __krc_divide_quotient;
    if (negative) {
        struct __krc_pair64 negated = __krc_pair64_negate(quotient);
        quotient = negated;
    }
    return quotient;
}

struct __krc_pair64 __krc_smoddi3(struct __krc_pair64 left,
                                  struct __krc_pair64 right) {
    struct __krc_pair64 magnitude_left;
    struct __krc_pair64 magnitude_right;
    struct __krc_pair64 remainder;
    magnitude_left = left;
    magnitude_right = right;
    if (__krc_pair64_negative(left)) {
        struct __krc_pair64 negated = __krc_pair64_negate(left);
        magnitude_left = negated;
    }
    if (__krc_pair64_negative(right)) {
        struct __krc_pair64 negated = __krc_pair64_negate(right);
        magnitude_right = negated;
    }
    __krc_pair64_divide(magnitude_left, magnitude_right);
    remainder = __krc_divide_remainder;
    if (__krc_pair64_negative(left)) {
        struct __krc_pair64 negated = __krc_pair64_negate(remainder);
        remainder = negated;
    }
    return remainder;
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

static void print_integer(unsigned long value, int base, int width,
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
            unsigned long digit = value % (unsigned long)base;
            if (digit < 10) {
                digits[count++] = (char)('0' + digit);
            } else {
                digits[count++] =
                    (char)((uppercase ? 'A' : 'a') + (digit - 10));
            }
            value = value / (unsigned long)base;
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
                long value = va_arg(arguments, long);
                unsigned long magnitude =
                    value < 0 ? (unsigned long)(-(value + 1)) + 1
                              : (unsigned long)value;
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
                print_integer(va_arg(arguments, unsigned long), 10,
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
                print_integer(va_arg(arguments, unsigned long), 8,
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
                print_integer(va_arg(arguments, unsigned long), 16,
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

int vsprintf(char *destination, const char *format, va_list arguments) {
    char *saved_buffer = format_buffer;
    int saved_count = format_count;
    int saved_limit = format_limit;
    format_buffer = destination;
    format_count = 0;
    format_limit = -1;
    format_output(format, arguments);
    destination[format_count] = 0;
    const int result = format_count;
    format_buffer = saved_buffer;
    format_count = saved_count;
    format_limit = saved_limit;
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
/* The heap must start above the loaded image. Images load at 0x10000 and a
   large program's .text alone can run past 0x40000, so a base of 0x00020000
   handed out memory inside the code and allocations overwrote the
   instructions that were about to run. */
#define HEAP_BEGIN ((unsigned char *)0x00100000)
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

/*
 * Exception runtime, ADR 0026. The frame walk and the `.cpp_except` table scan
 * live in eh.s; these are the allocation and handler bookkeeping entry points.
 * The exception object carries a sixteen-byte header: typeinfo, destructor,
 * the previously handled exception, and a spare word. The thrown object starts
 * at header + 16.
 */
extern unsigned char *__krc_current_exception;
void _Unwind_Resume(void *exception);
void _Exit(int status);

void *__cxa_allocate_exception(unsigned int size) {
    unsigned char *block = (unsigned char *)malloc(size + 16);
    if (block == 0) {
        return 0;
    }
    return block + 16;
}

void __cxa_free_exception(void *object) {
    if (object != 0) {
        free((unsigned char *)object - 16);
    }
}

void *__cxa_begin_catch(void *object) {
    if (object != 0) {
        __krc_current_exception = (unsigned char *)object - 16;
    }
    return object;
}

void __cxa_end_catch(void) {
    unsigned char *header = __krc_current_exception;
    if (header == 0) {
        return;
    }
    __krc_current_exception = *(unsigned char **)(header + 8);
    free(header);
}

void __cxa_rethrow(void) {
    if (__krc_current_exception != 0) {
        _Unwind_Resume(__krc_current_exception + 16);
    }
    _Exit(134);
}

/* A handler for `candidate` catches a thrown type that is or derives from it. */
int __krc_eh_matches(void *thrown, void *candidate) {
    unsigned char *typeinfo = (unsigned char *)thrown;
    while (typeinfo != 0) {
        if (typeinfo == (unsigned char *)candidate) {
            return 1;
        }
        typeinfo = *(unsigned char **)(typeinfo + 12);
    }
    return 0;
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

long double strtold(const char *text, char **end) {
    /* long double is 64-bit IEEE on this target, so the double parse is exact
     * for every value either type can represent. */
    return (long double)strtod(text, end);
}
double atof(const char *text) {
    return strtod(text, 0);
}

/* strtof is absent: kr232 has one floating-point format, so a conversion to
   single precision is an FPTrunc the backend cannot lower and the ISA cannot
   express. A program that needs it gets an undefined symbol at link time rather
   than a silent widening. */

long llabs(long value) {
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

div_t div(int numerator, int denominator) {
    div_t result;
    result.quot = numerator / denominator;
    result.rem = numerator % denominator;
    return result;
}

ldiv_t ldiv(long numerator, long denominator) {
    ldiv_t result;
    result.quot = numerator / denominator;
    result.rem = numerator % denominator;
    return result;
}

/* kr232 has no 64-bit integer type, so `lldiv` is not declared by its
   <stdlib.h> and is not defined here either: a program that calls it fails at
   link time with a name the profile never promised. */
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
    char temporary_path[32];
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
    stream->temporary_path[0] = 0;
    return stream;
}

int feof(struct FILE *stream) {
    return stream != 0 && stream->eof != 0;
}

int ferror(struct FILE *stream) {
    return stream != 0 && stream->error != 0;
}

void clearerr(struct FILE *stream) {
    if (stream != 0) {
        stream->eof = 0;
        stream->error = 0;
    }
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

int getdelim(char **line, unsigned int *capacity, int delimiter,
             struct FILE *stream) {
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
        if (character == delimiter) {
            break;
        }
    }
    (*line)[length] = 0;
    return (int)length;
}

int getline(char **line, unsigned int *capacity, struct FILE *stream) {
    return getdelim(line, capacity, '\n', stream);
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

long atoll(const char *text) {
    return (long)atol(text);
}

long strtoll(const char *text, char **end, int base) {
    return (long)strtol(text, end, base);
}

unsigned long strtoull(const char *text, char **end, int base) {
    return (unsigned long)strtoul(text, end, base);
}

void _Exit(int status) {
    exit(status);
}

int isalpha(int character) {
    return (character >= 'a' && character <= 'z') ||
           (character >= 'A' && character <= 'Z');
}

int isdigit(int character) {
    return character >= '0' && character <= '9';
}

int isalnum(int character) {
    return isalpha(character) || isdigit(character);
}

int isblank(int character) {
    return character == ' ' || character == '\t';
}

int iscntrl(int character) {
    return (character >= 0 && character < 32) || character == 127;
}

int isgraph(int character) {
    return character >= 33 && character <= 126;
}

int islower(int character) {
    return character >= 'a' && character <= 'z';
}

int isprint(int character) {
    return character >= 32 && character <= 126;
}

int ispunct(int character) {
    return isgraph(character) && !isalnum(character);
}

int isspace(int character) {
    return character == ' ' || (character >= 9 && character <= 13);
}

int isupper(int character) {
    return character >= 'A' && character <= 'Z';
}

int isxdigit(int character) {
    return isdigit(character) || (character >= 'a' && character <= 'f') ||
           (character >= 'A' && character <= 'F');
}

int tolower(int character) {
    return isupper(character) ? character + ('a' - 'A') : character;
}

int toupper(int character) {
    return islower(character) ? character - ('a' - 'A') : character;
}

double fmin(double left, double right) {
    return left < right ? left : right;
}

double fmax(double left, double right) {
    return left > right ? left : right;
}

int fputs(const char *text, struct FILE *stream) {
    const unsigned int length = strlen(text);
    if (fwrite(text, 1, length, stream) != length) {
        return -1;
    }
    return 0;
}

int putc(int character, struct FILE *stream) {
    return fputc(character, stream);
}

int setbuf(struct FILE *stream, char *buffer) {
    return setvbuf(stream, buffer, buffer == 0 ? 2 : 0, 1024);
}

int fgetpos(struct FILE *stream, long *position) {
    const long offset = ftell(stream);
    if (offset < 0) {
        return -1;
    }
    *position = offset;
    return 0;
}

int fsetpos(struct FILE *stream, const long *position) {
    return fseek(stream, *position, 0);
}

struct FILE *freopen(const char *path, const char *mode,
                    struct FILE *stream) {
    if (stream != 0) {
        fclose(stream);
    }
    return fopen(path, mode);
}

static void temporary_name(char *buffer) {
    unsigned int value = (unsigned int)sys_random();
    char digits[8];
    int count = 0;
    do {
        digits[count++] = (char)('0' + value % 10U);
        value /= 10U;
    } while (value != 0U);
    buffer[0] = 'k'; buffer[1] = 'r'; buffer[2] = '3'; buffer[3] = '2';
    buffer[4] = '_'; buffer[5] = 't'; buffer[6] = 'm'; buffer[7] = 'p'; buffer[8] = '_';
    int position = 9;
    while (count > 0) {
        buffer[position++] = digits[--count];
    }
    buffer[position] = 0;
}

struct FILE *tmpfile(void) {
    char path[32];
    temporary_name(path);
    struct FILE *stream = fopen(path, "wb+");
    if (stream != 0) {
        for (int index = 0; index < 32; index++) {
            stream->temporary_path[index] = path[index];
        }
    }
    return stream;
}

char *tmpnam(char *buffer) {
    static char storage[32];
    if (buffer == 0) {
        buffer = storage;
    }
    temporary_name(buffer);
    return buffer;
}

static long days_from_civil(long year, long month, long day) {
    year -= month <= 2 ? 1 : 0;
    const long era = (year >= 0 ? year : year - 399) / 400;
    const unsigned int year_of_era = (unsigned int)(year - era * 400);
    const unsigned int day_of_year =
        (153U * (unsigned int)(month + (month > 2 ? -3 : 9)) + 2U) / 5U +
        (unsigned int)day - 1U;
    const unsigned int day_of_era =
        year_of_era * 365U + year_of_era / 4U - year_of_era / 100U +
        day_of_year;
    return era * 146097L + (long)day_of_era - 719468L;
}

static void civil_from_days(long days, long *year, long *month, long *day) {
    days += 719468L;
    const long era = (days >= 0 ? days : days - 146096L) / 146097L;
    const unsigned int day_of_era = (unsigned int)(days - era * 146097L);
    const unsigned int year_of_era =
        (day_of_era - day_of_era / 1460U + day_of_era / 36524U -
         day_of_era / 146096U) /
        365U;
    const long result_year = (long)year_of_era + era * 400L;
    const unsigned int day_of_year =
        day_of_era -
        (365U * year_of_era + year_of_era / 4U - year_of_era / 100U);
    const unsigned int month_prime = (5U * day_of_year + 2U) / 153U;
    const unsigned int result_day =
        day_of_year - (153U * month_prime + 2U) / 5U + 1U;
    const unsigned int result_month =
        month_prime + (month_prime < 10U ? 3U : (unsigned int)-9);
    *year = result_year + (result_month <= 2U ? 1L : 0L);
    *month = (long)result_month;
    *day = (long)result_day;
}

static struct tm time_result;

struct tm *gmtime(const time_t *value) {
    if (value == 0) {
        return 0;
    }
    long seconds = (long)*value;
    long days = seconds / 86400L;
    long remainder = seconds % 86400L;
    if (remainder < 0) {
        remainder += 86400L;
        days--;
    }
    long year = 0;
    long month = 0;
    long day = 0;
    civil_from_days(days, &year, &month, &day);
    time_result.tm_sec = (int)(remainder % 60L);
    time_result.tm_min = (int)((remainder / 60L) % 60L);
    time_result.tm_hour = (int)(remainder / 3600L);
    time_result.tm_mday = (int)day;
    time_result.tm_mon = (int)month - 1;
    time_result.tm_year = (int)year - 1900;
    time_result.tm_wday = (int)((days + 4L) % 7L);
    if (time_result.tm_wday < 0) {
        time_result.tm_wday += 7;
    }
    time_result.tm_yday =
        (int)(days - days_from_civil(year, 1, 1));
    time_result.tm_isdst = 0;
    return &time_result;
}

struct tm *localtime(const time_t *value) {
    return gmtime(value);
}

time_t mktime(struct tm *value) {
    if (value == 0) {
        return -1;
    }
    const long days =
        days_from_civil((long)value->tm_year + 1900L,
                        (long)value->tm_mon + 1L, (long)value->tm_mday);
    return (time_t)(days * 86400L + (long)value->tm_hour * 3600L +
                    (long)value->tm_min * 60L + (long)value->tm_sec);
}

static const char *const short_months[] = {"Jan", "Feb", "Mar", "Apr",
                                           "May", "Jun", "Jul", "Aug",
                                           "Sep", "Oct", "Nov", "Dec"};
static const char *const long_months[] = {
    "January", "February", "March",     "April",   "May",      "June",
    "July",    "August",   "September", "October", "November", "December"};
static const char *const short_days[] = {"Sun", "Mon", "Tue", "Wed",
                                         "Thu", "Fri", "Sat"};
static const char *const long_days[] = {"Sunday",   "Monday", "Tuesday",
                                        "Wednesday", "Thursday", "Friday",
                                        "Saturday"};

static void append_character(char *buffer, unsigned int size,
                            unsigned int *used, char character) {
    if (*used + 1 < size) {
        buffer[(*used)++] = character;
        buffer[*used] = 0;
    }
}

static void append_text(char *buffer, unsigned int size, unsigned int *used,
                        const char *text) {
    while (*text != 0 && *used + 1 < size) {
        buffer[(*used)++] = *text++;
    }
    buffer[*used] = 0;
}

/* One target instead of three separate pieces of it: kr232 passes four arguments
   in registers and refuses a fifth, so the helpers that write into a caller's
   buffer carry a pointer to it. */
struct append_target {
    char *buffer;
    unsigned int size;
    unsigned int *used;
};

static void append_number(struct append_target *target, long value,
                          unsigned int width, char padding) {
    char digits[16];
    unsigned int count = 0;
    unsigned int magnitude =
        value < 0 ? (unsigned int)(-value) : (unsigned int)value;
    do {
        digits[count++] = (char)('0' + magnitude % 10U);
        magnitude /= 10U;
    } while (magnitude != 0);
    while (count < width) {
        digits[count++] = padding;
    }
    if (value < 0 && *target->used + 1 < target->size) {
        target->buffer[(*target->used)++] = '-';
    }
    while (count > 0 && *target->used + 1 < target->size) {
        target->buffer[(*target->used)++] = digits[--count];
    }
    target->buffer[*target->used] = 0;
}

static void append_format(struct append_target *target, const char *format,
                          const struct tm *value) {
    const unsigned int available =
        *target->used < target->size ? target->size - *target->used : 0;
    *target->used += (unsigned int)strftime(target->buffer + *target->used,
                                           available, format, value);
}

size_t strftime(char *buffer, size_t size, const char *format,
                const struct tm *value) {
    if (buffer == 0 || size == 0 || format == 0 || value == 0) {
        return 0;
    }
    unsigned int used = 0;
    struct append_target target = {buffer, (unsigned int)size, &used};
    buffer[0] = 0;
    for (const char *cursor = format; *cursor != 0; ++cursor) {
        if (*cursor != '%') {
            append_character(buffer, (unsigned int)size, &used, *cursor);
            continue;
        }
        ++cursor;
        switch (*cursor) {
        case 'Y':
            append_number(&target,
                          (long)value->tm_year + 1900L, 4, '0');
            break;
        case 'y':
            append_number(&target,
                          ((long)value->tm_year + 1900L) % 100L, 2, '0');
            break;
        case 'm':
            append_number(&target,
                          (long)value->tm_mon + 1L, 2, '0');
            break;
        case 'd':
            append_number(&target,
                          (long)value->tm_mday, 2, '0');
            break;
        case 'e':
            append_number(&target,
                          (long)value->tm_mday, 2, ' ');
            break;
        case 'H':
            append_number(&target,
                          (long)value->tm_hour, 2, '0');
            break;
        case 'M':
            append_number(&target,
                          (long)value->tm_min, 2, '0');
            break;
        case 'S':
            append_number(&target,
                          (long)value->tm_sec, 2, '0');
            break;
        case 'j':
            append_number(&target,
                          (long)value->tm_yday + 1L, 3, '0');
            break;
        case 'a':
            if (value->tm_wday >= 0 && value->tm_wday < 7) {
                append_text(buffer, (unsigned int)size, &used,
                            short_days[value->tm_wday]);
            }
            break;
        case 'A':
            if (value->tm_wday >= 0 && value->tm_wday < 7) {
                append_text(buffer, (unsigned int)size, &used,
                            long_days[value->tm_wday]);
            }
            break;
        case 'b':
            if (value->tm_mon >= 0 && value->tm_mon < 12) {
                append_text(buffer, (unsigned int)size, &used,
                            short_months[value->tm_mon]);
            }
            break;
        case 'B':
            if (value->tm_mon >= 0 && value->tm_mon < 12) {
                append_text(buffer, (unsigned int)size, &used,
                            long_months[value->tm_mon]);
            }
            break;
        case 'p':
            append_text(buffer, (unsigned int)size, &used,
                        value->tm_hour < 12 ? "AM" : "PM");
            break;
        case 'Z':
            append_text(buffer, (unsigned int)size, &used, "UTC");
            break;
        case 'z':
            append_text(buffer, (unsigned int)size, &used, "+0000");
            break;
        case 'C':
            append_number(&target,
                          ((long)value->tm_year + 1900L) / 100L, 2, '0');
            break;
        case 'D':
            append_format(&target, "%m/%d/%y", value);
            break;
        case 'h':
            if (value->tm_mon >= 0 && value->tm_mon < 12) {
                append_text(buffer, (unsigned int)size, &used,
                            short_months[value->tm_mon]);
            }
            break;
        case 'n':
            append_character(buffer, (unsigned int)size, &used, '\n');
            break;
        case 't':
            append_character(buffer, (unsigned int)size, &used, '\t');
            break;
        case 'F':
            append_format(&target, "%Y-%m-%d", value);
            break;
        case 'T':
            append_format(&target, "%H:%M:%S", value);
            break;
        case 'R':
            append_format(&target, "%H:%M", value);
            break;
        case 'x':
            append_format(&target, "%m/%d/%y", value);
            break;
        case 'c':
            append_format(&target,
                          "%a %b %e %H:%M:%S %Y", value);
            break;
        case '%':
            append_text(buffer, (unsigned int)size, &used, "%");
            break;
        case 0:
            return used;
        default:
            append_text(buffer, (unsigned int)size, &used, "%");
            append_character(buffer, (unsigned int)size, &used, *cursor);
            break;
        }
    }
    return used;
}

char *asctime(const struct tm *value) {
    static char result[32];
    if (value == 0) {
        return 0;
    }
    strftime(result, sizeof(result), "%a %b %e %H:%M:%S %Y\n", value);
    return result;
}

char *ctime(const time_t *value) {
    return asctime(localtime(value));
}

void __krc_assert_fail(const char *expression, const char *file, int line) {
    fprintf(stderr, "assertion failed: %s (%s:%d)\n", expression, file, line);
    abort();
}

static int fold_case(int character) {
    if (character >= 'A' && character <= 'Z') {
        return character + ('a' - 'A');
    }
    return character;
}

int strcasecmp(const char *left, const char *right) {
    while (*left != 0 && *right != 0) {
        const int difference =
            fold_case((unsigned char)*left) - fold_case((unsigned char)*right);
        if (difference != 0) {
            return difference;
        }
        left++;
        right++;
    }
    return fold_case((unsigned char)*left) - fold_case((unsigned char)*right);
}

int strncasecmp(const char *left, const char *right, size_t count) {
    size_t index = 0;
    while (index < count && left[index] != 0 && right[index] != 0) {
        const int difference = fold_case((unsigned char)left[index]) -
                               fold_case((unsigned char)right[index]);
        if (difference != 0) {
            return difference;
        }
        index++;
    }
    if (index == count) {
        return 0;
    }
    return fold_case((unsigned char)left[index]) -
           fold_case((unsigned char)right[index]);
}

char *strsep(char **text, const char *delimiters) {
    if (text == 0 || *text == 0) {
        return 0;
    }
    char *start = *text;
    char *cursor = start;
    while (*cursor != 0) {
        if (character_in_set(*cursor, delimiters)) {
            *cursor = 0;
            *text = cursor + 1;
            return start;
        }
        cursor++;
    }
    *text = 0;
    return start;
}

int vdprintf(int descriptor, const char *format, va_list arguments) {
    char buffer[1024];
    const int length = vsnprintf(buffer, sizeof(buffer), format, arguments);
    if (length < 0) {
        return -1;
    }
    const int count = length < (int)sizeof(buffer) ? length : (int)sizeof(buffer);
    return write(descriptor, buffer, count);
}

int dprintf(int descriptor, const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    const int result = vdprintf(descriptor, format, arguments);
    va_end(arguments);
    return result;
}

struct FILE *fdopen(int descriptor, const char *mode) {
    (void)mode;
    if (descriptor < 0) {
        return 0;
    }
    struct FILE *stream = (struct FILE *)malloc(sizeof(struct FILE));
    if (stream == 0) {
        return 0;
    }
    stream->descriptor = descriptor;
    stream->eof = 0;
    stream->error = 0;
    stream->pushback = -1;
    stream->temporary_path[0] = 0;
    return stream;
}

int fileno(struct FILE *stream) {
    return stream == 0 ? -1 : stream->descriptor;
}

void *memmem(const void *haystack, unsigned int haystack_length,
             const void *needle, unsigned int needle_length) {
    const unsigned char *hay = (const unsigned char *)haystack;
    const unsigned char *pin = (const unsigned char *)needle;
    if (needle_length == 0) {
        return (void *)haystack;
    }
    if (haystack_length < needle_length) {
        return 0;
    }
    for (unsigned int index = 0; index + needle_length <= haystack_length;
         index++) {
        unsigned int offset = 0;
        while (offset < needle_length && hay[index + offset] == pin[offset]) {
            offset++;
        }
        if (offset == needle_length) {
            return (void *)(hay + index);
        }
    }
    return 0;
}

int strerror_r(int error, char *buffer, unsigned int size) {
    if (buffer == 0 || size == 0) {
        return -1;
    }
    const char *message = strerror(error);
    unsigned int index = 0;
    while (message[index] != 0 && index + 1 < size) {
        buffer[index] = message[index];
        index++;
    }
    buffer[index] = 0;
    return 0;
}

static char locale_empty[] = "";
static char locale_dot[] = ".";
static char locale_c[] = "C";

static struct lconv locale_info = {
    locale_dot, locale_empty, locale_empty, locale_empty, locale_empty,
    locale_dot, locale_empty, locale_empty, locale_empty, locale_empty,
    (char)127, (char)127, (char)127, (char)127, (char)127, (char)127,
    (char)127, (char)127, (char)127, (char)127, (char)127, (char)127,
    (char)127, (char)127
};

char *setlocale(int category, const char *locale) {
    (void)category;
    if (locale == 0) {
        return locale_c;
    }
    if (locale[0] == 0 || (locale[0] == 'C' && locale[1] == 0)) {
        return locale_c;
    }
    return 0;
}

struct lconv *localeconv(void) {
    return &locale_info;
}

struct environment_entry {
    char *name;
    char *value;
};

static struct environment_entry environment_table[16];
static int environment_count;

char *getenv(const char *name) {
    if (name == 0) {
        return 0;
    }
    for (int index = 0; index < environment_count; index++) {
        if (strcmp(environment_table[index].name, name) == 0) {
            return environment_table[index].value;
        }
    }
    return 0;
}

int setenv(const char *name, const char *value, int overwrite) {
    if (name == 0 || value == 0 || *name == 0) {
        return -1;
    }
    for (int index = 0; index < environment_count; index++) {
        if (strcmp(environment_table[index].name, name) == 0) {
            if (!overwrite) {
                return 0;
            }
            char *copy = strdup(value);
            if (copy == 0) {
                return -1;
            }
            free(environment_table[index].value);
            environment_table[index].value = copy;
            return 0;
        }
    }
    if (environment_count >= 16) {
        return -1;
    }
    char *name_copy = strdup(name);
    char *value_copy = strdup(value);
    if (name_copy == 0 || value_copy == 0) {
        free(name_copy);
        free(value_copy);
        return -1;
    }
    environment_table[environment_count].name = name_copy;
    environment_table[environment_count].value = value_copy;
    environment_count++;
    return 0;
}

int unsetenv(const char *name) {
    if (name == 0) {
        return -1;
    }
    for (int index = 0; index < environment_count; index++) {
        if (strcmp(environment_table[index].name, name) == 0) {
            free(environment_table[index].name);
            free(environment_table[index].value);
            environment_count--;
            environment_table[index] = environment_table[environment_count];
            return 0;
        }
    }
    return 0;
}

static void (*atexit_handlers[32])(void);
static int atexit_handler_count;

int atexit(void (*function)(void)) {
    if (function == 0 || atexit_handler_count >= 32) {
        return -1;
    }
    atexit_handlers[atexit_handler_count++] = function;
    return 0;
}

void __krc_run_atexit(void) {
    while (atexit_handler_count > 0) {
        atexit_handler_count--;
        atexit_handlers[atexit_handler_count]();
    }
}

static void (*quick_exit_handlers[16])(void);
static int quick_exit_handler_count;

int at_quick_exit(void (*function)(void)) {
    if (function == 0 || quick_exit_handler_count >= 16) {
        return -1;
    }
    quick_exit_handlers[quick_exit_handler_count++] = function;
    return 0;
}

void quick_exit(int status) {
    while (quick_exit_handler_count > 0) {
        quick_exit_handler_count--;
        quick_exit_handlers[quick_exit_handler_count]();
    }
    _Exit(status);
}