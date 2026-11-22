#include <stdarg.h>

int __krc_write(int fd, const char *buffer, int count);

void *__krc_dynamic_cast(unsigned char *object, unsigned char *target_type);

unsigned long __krc_negate32(unsigned long value);
void __krc_negate64(unsigned int *value);
unsigned long __krc_f64_to_u32(double value);
unsigned long long __krc_f64_to_u64(double value);

extern char __heap_start[];
extern char __heap_end[];

/*
 * Single-inheritance RTTI: the object's vptr at offset 0 points at the first
 * virtual slot, the typeinfo pointer sits one word below it, and each typeinfo
 * links its base at +6. No address adjustment is needed for single
 * inheritance.
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
    typeinfo = *(unsigned char **)(vptr - 2);
    while (typeinfo != 0) {
        if (typeinfo == target_type) {
            return object;
        }
        typeinfo = *(unsigned char **)(typeinfo + 6);
    }
    return 0;
}

static char *heap_cursor;

unsigned int strlen(const char *text) {
    unsigned int length = 0;
    while (text[length] != 0) {
        length++;
    }
    return length;
}

int strcmp(const char *left, const char *right) {
    int index = 0;
    while (left[index] != 0 && left[index] == right[index]) {
        index++;
    }
    return (unsigned char)left[index] - (unsigned char)right[index];
}

char *strcpy(char *destination, const char *source) {
    int index = 0;
    while (source[index] != 0) {
        destination[index] = source[index];
        index++;
    }
    destination[index] = 0;
    return destination;
}

void *memcpy(void *destination, const void *source, unsigned int count) {
    char *to = (char *)destination;
    const char *from = (const char *)source;
    unsigned int index = 0;
    while (index < count) {
        to[index] = from[index];
        index++;
    }
    return destination;
}

void *memmove(void *destination, const void *source, unsigned int count) {
    char *to = (char *)destination;
    const char *from = (const char *)source;
    if (to < from) {
        unsigned int index = 0;
        while (index < count) {
            to[index] = from[index];
            index++;
        }
    } else {
        unsigned int index = count;
        while (index > 0) {
            index--;
            to[index] = from[index];
        }
    }
    return destination;
}

void *memset(void *destination, int value, unsigned int count) {
    char *to = (char *)destination;
    unsigned int index = 0;
    while (index < count) {
        to[index] = (char)value;
        index++;
    }
    return destination;
}

void *malloc(unsigned int size) {
    if (heap_cursor == 0) {
        heap_cursor = __heap_start;
    }
    unsigned int rounded = (size + 1) & 0xFFFEu;
    if (heap_cursor + rounded > __heap_end) {
        return 0;
    }
    void *result = heap_cursor;
    heap_cursor += rounded;
    return result;
}

void free(void *pointer) {
    (void)pointer;
}

unsigned long __krc_udiv32(unsigned long numerator, unsigned long denominator) {
    unsigned int *n = (unsigned int *)&numerator;
    unsigned int *d = (unsigned int *)&denominator;
    unsigned int nlow = n[0];
    unsigned int nhigh = n[1];
    const unsigned int dlow = d[0];
    const unsigned int dhigh = d[1];
    if (dlow == 0 && dhigh == 0) {
        return 0;
    }
    unsigned int qlow = 0;
    unsigned int qhigh = 0;
    unsigned int rlow = 0;
    unsigned int rhigh = 0;
    int bit = 0;
    while (bit < 32) {
        qhigh = (qhigh << 1) | (qlow >> 15);
        qlow = qlow << 1;
        rhigh = (rhigh << 1) | (rlow >> 15);
        rlow = rlow << 1;
        rlow = rlow | ((nhigh >> 15) & 1);
        nhigh = (nhigh << 1) | (nlow >> 15);
        nlow = nlow << 1;
        if (rhigh > dhigh || (rhigh == dhigh && rlow >= dlow)) {
            const unsigned int old = rlow;
            rlow = rlow - dlow;
            const unsigned int borrow = rlow > old ? 1 : 0;
            rhigh = rhigh - dhigh - borrow;
            qlow = qlow | 1;
        }
        bit++;
    }
    unsigned long quotient;
    unsigned int *q = (unsigned int *)&quotient;
    q[0] = qlow;
    q[1] = qhigh;
    return quotient;
}

unsigned long __krc_urem32(unsigned long numerator, unsigned long denominator) {
    unsigned int *n = (unsigned int *)&numerator;
    unsigned int *d = (unsigned int *)&denominator;
    unsigned int nlow = n[0];
    unsigned int nhigh = n[1];
    const unsigned int dlow = d[0];
    const unsigned int dhigh = d[1];
    if (dlow == 0 && dhigh == 0) {
        return 0;
    }
    unsigned int rlow = 0;
    unsigned int rhigh = 0;
    int bit = 0;
    while (bit < 32) {
        rhigh = (rhigh << 1) | (rlow >> 15);
        rlow = rlow << 1;
        rlow = rlow | ((nhigh >> 15) & 1);
        nhigh = (nhigh << 1) | (nlow >> 15);
        nlow = nlow << 1;
        if (rhigh > dhigh || (rhigh == dhigh && rlow >= dlow)) {
            const unsigned int old = rlow;
            rlow = rlow - dlow;
            const unsigned int borrow = rlow > old ? 1 : 0;
            rhigh = rhigh - dhigh - borrow;
        }
        bit++;
    }
    unsigned long remainder;
    unsigned int *r = (unsigned int *)&remainder;
    r[0] = rlow;
    r[1] = rhigh;
    return remainder;
}

unsigned long __krc_shl32(unsigned long value, unsigned int count) {
    unsigned int *words = (unsigned int *)&value;
    while (count != 0) {
        const unsigned int carry = words[0] >> 15;
        words[0] = words[0] << 1;
        words[1] = (words[1] << 1) | carry;
        count = count - 1;
    }
    return value;
}

unsigned long __krc_shr32(unsigned long value, unsigned int count) {
    unsigned int *words = (unsigned int *)&value;
    while (count != 0) {
        const unsigned int carry = words[1] & 1;
        words[1] = words[1] >> 1;
        words[0] = (words[0] >> 1) | (carry << 15);
        count = count - 1;
    }
    return value;
}

unsigned long __krc_sar32(unsigned long value, unsigned int count) {
    unsigned int *words = (unsigned int *)&value;
    while (count != 0) {
        const unsigned int carry = words[1] & 1;
        words[0] = (words[0] >> 1) | (carry << 15);
        words[1] = (words[1] >> 1) | (words[1] & 0x8000);
        count = count - 1;
    }
    return value;
}

unsigned long __krc_negate32(unsigned long value) {
    unsigned int *words = (unsigned int *)&value;
    unsigned int low = ~words[0];
    unsigned int high = ~words[1];
    low = low + 1;
    high = high + (low == 0 ? 1 : 0);
    words[0] = low;
    words[1] = high;
    return value;
}

long __krc_sdiv32(long numerator, long denominator) {
    unsigned int *np = (unsigned int *)&numerator;
    unsigned int *dp = (unsigned int *)&denominator;
    const int nneg = np[1] >> 15;
    const int dneg = dp[1] >> 15;
    unsigned long left = (unsigned long)numerator;
    unsigned long right = (unsigned long)denominator;
    if (nneg != 0) {
        left = __krc_negate32(left);
    }
    if (dneg != 0) {
        right = __krc_negate32(right);
    }
    unsigned long quotient = __krc_udiv32(left, right);
    if (nneg != dneg) {
        quotient = __krc_negate32(quotient);
    }
    return (long)quotient;
}

long __krc_srem32(long numerator, long denominator) {
    unsigned int *np = (unsigned int *)&numerator;
    unsigned int *dp = (unsigned int *)&denominator;
    const int nneg = np[1] >> 15;
    const int dneg = dp[1] >> 15;
    unsigned long left = (unsigned long)numerator;
    unsigned long right = (unsigned long)denominator;
    if (nneg != 0) {
        left = __krc_negate32(left);
    }
    if (dneg != 0) {
        right = __krc_negate32(right);
    }
    unsigned long remainder = __krc_urem32(left, right);
    if (nneg != 0) {
        remainder = __krc_negate32(remainder);
    }
    return (long)remainder;
}

double __krc_i32_to_f64(long value) {
    unsigned int *words = (unsigned int *)&value;
    const int negative = words[1] >> 15;
    if (negative != 0) {
        value = (long)__krc_negate32((unsigned long)value);
        words = (unsigned int *)&value;
    }
    double result = 0.0;
    int i = 1;
    while (i >= 0) {
        result = result * 65536.0 + (double)words[i];
        i--;
    }
    if (negative != 0) {
        result = 0.0 - result;
    }
    return result;
}

double __krc_u32_to_f64(unsigned long value) {
    unsigned int *words = (unsigned int *)&value;
    double result = 0.0;
    int i = 1;
    while (i >= 0) {
        result = result * 65536.0 + (double)words[i];
        i--;
    }
    return result;
}

double __krc_i64_to_f64(long long value) {
    unsigned int *words = (unsigned int *)&value;
    const int negative = words[3] >> 15;
    if (negative != 0) {
        __krc_negate64(words);
    }
    double result = 0.0;
    int i = 3;
    while (i >= 0) {
        result = result * 65536.0 + (double)words[i];
        i--;
    }
    if (negative != 0) {
        result = 0.0 - result;
    }
    return result;
}

double __krc_u64_to_f64(unsigned long long value) {
    unsigned int *words = (unsigned int *)&value;
    double result = 0.0;
    int i = 3;
    while (i >= 0) {
        result = result * 65536.0 + (double)words[i];
        i--;
    }
    return result;
}

long __krc_f64_to_i32(double value) {
    if (value < 0.0) {
        double t = 0.0 - value;
        return (long)__krc_negate32(__krc_f64_to_u32(t));
    }
    return (long)__krc_f64_to_u32(value);
}

unsigned long __krc_f64_to_u32(double value) {
    unsigned long result = 0;
    unsigned int *words = (unsigned int *)&result;
    double t = value;
    words[1] = (unsigned int)(t / 65536.0);
    t = t - (double)words[1] * 65536.0;
    words[0] = (unsigned int)t;
    return result;
}

long long __krc_f64_to_i64(double value) {
    if (value < 0.0) {
        double t = 0.0 - value;
        unsigned long long result = __krc_f64_to_u64(t);
        __krc_negate64((unsigned int *)&result);
        return (long long)result;
    }
    return (long long)__krc_f64_to_u64(value);
}

unsigned long long __krc_f64_to_u64(double value) {
    unsigned long long result = 0;
    unsigned int *words = (unsigned int *)&result;
    double t = value;
    words[3] = (unsigned int)(t / 281474976710656.0);
    t = t - (double)words[3] * 281474976710656.0;
    words[2] = (unsigned int)(t / 4294967296.0);
    t = t - (double)words[2] * 4294967296.0;
    words[1] = (unsigned int)(t / 65536.0);
    t = t - (double)words[1] * 65536.0;
    words[0] = (unsigned int)t;
    return result;
}

void __krc_udiv64(unsigned int *numerator, unsigned int *denominator, unsigned int *out) {
    unsigned int n[4];
    unsigned int d[4];
    unsigned int q[4];
    unsigned int r[4];
    int i = 0;
    while (i < 4) {
        n[i] = numerator[i];
        d[i] = denominator[i];
        q[i] = 0;
        r[i] = 0;
        i++;
    }
    if ((d[0] | d[1] | d[2] | d[3]) != 0) {
        int word = 3;
        while (word >= 0) {
            unsigned int mask = 0x8000;
            int pos = 0;
            while (pos < 16) {
                r[3] = (r[3] << 1) | (r[2] >> 15);
                r[2] = (r[2] << 1) | (r[1] >> 15);
                r[1] = (r[1] << 1) | (r[0] >> 15);
                r[0] = r[0] << 1;
                r[0] = r[0] | (n[3] >> 15);
                n[3] = (n[3] << 1) | (n[2] >> 15);
                n[2] = (n[2] << 1) | (n[1] >> 15);
                n[1] = (n[1] << 1) | (n[0] >> 15);
                n[0] = n[0] << 1;
                int ge = 1;
                int j = 3;
                while (j >= 0) {
                    if (r[j] > d[j]) {
                        ge = 1;
                        break;
                    }
                    if (r[j] < d[j]) {
                        ge = 0;
                        break;
                    }
                    j--;
                }
                if (ge) {
                    unsigned int borrow = 0;
                    int k = 0;
                    while (k < 4) {
                        const unsigned int sub = d[k] + borrow;
                        borrow = r[k] < sub;
                        r[k] = r[k] - sub;
                        k++;
                    }
                    q[word] = q[word] | mask;
                }
                mask = mask >> 1;
                pos++;
            }
            word--;
        }
    }
    i = 0;
    while (i < 4) {
        out[i] = q[i];
        i++;
    }
}

void __krc_urem64(unsigned int *numerator, unsigned int *denominator, unsigned int *out) {
    unsigned int n[4];
    unsigned int d[4];
    unsigned int r[4];
    int i = 0;
    while (i < 4) {
        n[i] = numerator[i];
        d[i] = denominator[i];
        r[i] = 0;
        i++;
    }
    if ((d[0] | d[1] | d[2] | d[3]) != 0) {
        int bit = 0;
        while (bit < 64) {
            r[3] = (r[3] << 1) | (r[2] >> 15);
            r[2] = (r[2] << 1) | (r[1] >> 15);
            r[1] = (r[1] << 1) | (r[0] >> 15);
            r[0] = r[0] << 1;
            r[0] = r[0] | (n[3] >> 15);
            n[3] = (n[3] << 1) | (n[2] >> 15);
            n[2] = (n[2] << 1) | (n[1] >> 15);
            n[1] = (n[1] << 1) | (n[0] >> 15);
            n[0] = n[0] << 1;
            int ge = 1;
            int j = 3;
            while (j >= 0) {
                if (r[j] > d[j]) {
                    ge = 1;
                    break;
                }
                if (r[j] < d[j]) {
                    ge = 0;
                    break;
                }
                j--;
            }
            if (ge) {
                unsigned int borrow = 0;
                int k = 0;
                while (k < 4) {
                    const unsigned int sub = d[k] + borrow;
                    borrow = r[k] < sub;
                    r[k] = r[k] - sub;
                    k++;
                }
            }
            bit++;
        }
    }
    i = 0;
    while (i < 4) {
        out[i] = r[i];
        i++;
    }
}

void __krc_negate64(unsigned int *value) {
    unsigned int carry = 1;
    int i = 0;
    while (i < 4) {
        value[i] = ~value[i];
        const unsigned int old = value[i];
        value[i] = value[i] + carry;
        carry = old > value[i];
        i++;
    }
}

void __krc_sdiv64(unsigned int *numerator, unsigned int *denominator, unsigned int *out) {
    unsigned int n[4];
    unsigned int d[4];
    unsigned int q[4];
    int i = 0;
    while (i < 4) {
        n[i] = numerator[i];
        d[i] = denominator[i];
        q[i] = 0;
        i++;
    }
    const int nneg = n[3] >> 15;
    const int dneg = d[3] >> 15;
    if (nneg != 0) {
        __krc_negate64(n);
    }
    if (dneg != 0) {
        __krc_negate64(d);
    }
    __krc_udiv64(n, d, q);
    if (nneg != dneg) {
        __krc_negate64(q);
    }
    i = 0;
    while (i < 4) {
        out[i] = q[i];
        i++;
    }
}

void __krc_srem64(unsigned int *numerator, unsigned int *denominator, unsigned int *out) {
    unsigned int n[4];
    unsigned int d[4];
    unsigned int r[4];
    int i = 0;
    while (i < 4) {
        n[i] = numerator[i];
        d[i] = denominator[i];
        r[i] = 0;
        i++;
    }
    const int nneg = n[3] >> 15;
    const int dneg = d[3] >> 15;
    if (nneg != 0) {
        __krc_negate64(n);
    }
    if (dneg != 0) {
        __krc_negate64(d);
    }
    __krc_urem64(n, d, r);
    if (nneg != 0) {
        __krc_negate64(r);
    }
    i = 0;
    while (i < 4) {
        out[i] = r[i];
        i++;
    }
}

void __krc_mul64(unsigned int *left, unsigned int *right, unsigned int *out) {
    unsigned long long a;
    unsigned long long b;
    unsigned long long result = 0;
    unsigned int *ap = (unsigned int *)&a;
    unsigned int *bp = (unsigned int *)&b;
    ap[0] = left[0];
    ap[1] = left[1];
    ap[2] = left[2];
    ap[3] = left[3];
    bp[0] = right[0];
    bp[1] = right[1];
    bp[2] = right[2];
    bp[3] = right[3];
    int bit = 0;
    while (bit < 64) {
        if ((b & 1) != 0) {
            result = result + a;
        }
        a = a << 1;
        b = b >> 1;
        bit++;
    }
    unsigned int *rp = (unsigned int *)&result;
    out[0] = rp[0];
    out[1] = rp[1];
    out[2] = rp[2];
    out[3] = rp[3];
}

void __krc_shl64(unsigned int *words, unsigned int count) {
    while (count != 0) {
        words[3] = (words[3] << 1) | (words[2] >> 15);
        words[2] = (words[2] << 1) | (words[1] >> 15);
        words[1] = (words[1] << 1) | (words[0] >> 15);
        words[0] = words[0] << 1;
        count = count - 1;
    }
}

void __krc_shr64(unsigned int *words, unsigned int count) {
    while (count != 0) {
        words[0] = (words[0] >> 1) | (words[1] << 15);
        words[1] = (words[1] >> 1) | (words[2] << 15);
        words[2] = (words[2] >> 1) | (words[3] << 15);
        words[3] = words[3] >> 1;
        count = count - 1;
    }
}

void __krc_sar64(unsigned int *words, unsigned int count) {
    while (count != 0) {
        const unsigned int sign = words[3] & 0x8000;
        words[0] = (words[0] >> 1) | (words[1] << 15);
        words[1] = (words[1] >> 1) | (words[2] << 15);
        words[2] = (words[2] >> 1) | (words[3] << 15);
        words[3] = (words[3] >> 1) | sign;
        count = count - 1;
    }
}

int putchar(int c) {
    char byte = (char)c;
    __krc_write(1, &byte, 1);
    return c;
}

int puts(const char *text) {
    const unsigned int length = strlen(text);
    __krc_write(1, text, (int)length);
    putchar('\n');
    return (int)(length + 1);
}

static int print_unsigned(unsigned int value, unsigned int base) {
    char buffer[16];
    int length = 0;
    do {
        const unsigned int digit = value % base;
        buffer[length++] = (char)(digit < 10 ? '0' + digit : 'a' + digit - 10);
        value /= base;
    } while (value != 0);
    for (int index = length - 1; index >= 0; index--) {
        putchar(buffer[index]);
    }
    return length;
}

static int print_unsigned_long(unsigned long value, unsigned int base) {
    char buffer[24];
    int length = 0;
    do {
        const unsigned int digit = (unsigned int)(value % base);
        buffer[length++] = (char)(digit < 10 ? '0' + digit : 'a' + digit - 10);
        value /= base;
    } while (value != 0);
    for (int index = length - 1; index >= 0; index--) {
        putchar(buffer[index]);
    }
    return length;
}

static int print_unsigned_long_long(unsigned long long value, unsigned int base) {
    unsigned int words[4];
    unsigned int *source = (unsigned int *)&value;
    int i = 0;
    while (i < 4) {
        words[i] = source[i];
        i++;
    }
    if (base == 16) {
        int written = 0;
        int started = 0;
        int word = 3;
        while (word >= 0) {
            int shift = 12;
            while (shift >= 0) {
                const unsigned int digit = (words[word] >> shift) & 0xF;
                if (digit != 0 || started != 0 || (word == 0 && shift == 0)) {
                    putchar((char)(digit < 10 ? '0' + digit : 'a' + digit - 10));
                    written++;
                    started = 1;
                }
                shift -= 4;
            }
            word--;
        }
        return written;
    }
    unsigned int digits[8];
    i = 0;
    while (i < 8) {
        digits[i] = 0;
        i++;
    }
    i = 0;
    while (i < 8) {
        digits[i] = (words[i / 2] >> ((i % 2) * 8)) & 0xFF;
        i++;
    }
    char buffer[24];
    int length = 0;
    int non_zero = 0;
    i = 7;
    while (i >= 0) {
        if (digits[i] != 0) non_zero = 1;
        i--;
    }
    if (non_zero == 0) {
        putchar('0');
        return 1;
    }
    while (non_zero != 0) {
        unsigned int remainder = 0;
        int byte = 7;
        while (byte >= 0) {
            const unsigned int current = remainder * 256 + digits[byte];
            digits[byte] = current / 10;
            remainder = current % 10;
            byte--;
        }
        buffer[length++] = (char)('0' + remainder);
        non_zero = 0;
        byte = 7;
        while (byte >= 0) {
            if (digits[byte] != 0) non_zero = 1;
            byte--;
        }
    }
    for (i = length - 1; i >= 0; i--) {
        putchar(buffer[i]);
    }
    return length;
}

int printf(const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    int written = 0;
    int index = 0;
    while (format[index] != 0) {
        if (format[index] != '%') {
            putchar(format[index++]);
            written++;
            continue;
        }
        index++;
        if (format[index] == '%') {
            putchar('%');
            index++;
            written++;
            continue;
        }
        int long_count = 0;
        while (format[index] == 'l') {
            long_count++;
            index++;
        }
        if (format[index] == 'c') {
            putchar(va_arg(arguments, int));
            index++;
            written++;
            continue;
        }
        if (format[index] == 's') {
            const char *text = va_arg(arguments, const char *);
            const unsigned int length = strlen(text);
            __krc_write(1, text, (int)length);
            written += (int)length;
            index++;
            continue;
        }
        if (format[index] == 'd' || format[index] == 'i') {
            if (long_count >= 2) {
                long long value = va_arg(arguments, long long);
                unsigned long long magnitude = (unsigned long long)value;
                if (value < 0) {
                    __krc_negate64((unsigned int *)&magnitude);
                    putchar('-');
                    written++;
                }
                written += print_unsigned_long_long(magnitude, 10);
            } else if (long_count == 1) {
                long value = va_arg(arguments, long);
                unsigned long magnitude = (unsigned long)value;
                if (value < 0) {
                    magnitude = __krc_negate32(magnitude);
                    putchar('-');
                    written++;
                }
                written += print_unsigned_long(magnitude, 10);
            } else {
                int value = va_arg(arguments, int);
                if (value < 0) {
                    putchar('-');
                    written++;
                    value = -value;
                }
                written += print_unsigned((unsigned int)value, 10);
            }
            index++;
            continue;
        }
        if (format[index] == 'u') {
            if (long_count >= 2) {
                written += print_unsigned_long_long(va_arg(arguments, unsigned long long), 10);
            } else if (long_count == 1) {
                written += print_unsigned_long(va_arg(arguments, unsigned long), 10);
            } else {
                written += print_unsigned(va_arg(arguments, unsigned int), 10);
            }
            index++;
            continue;
        }
        if (format[index] == 'x') {
            if (long_count >= 2) {
                written += print_unsigned_long_long(va_arg(arguments, unsigned long long), 16);
            } else if (long_count == 1) {
                written += print_unsigned_long(va_arg(arguments, unsigned long), 16);
            } else {
                written += print_unsigned(va_arg(arguments, unsigned int), 16);
            }
            index++;
            continue;
        }
        putchar('%');
        putchar(format[index]);
        written += 2;
        index++;
    }
    va_end(arguments);
    return written;
}
