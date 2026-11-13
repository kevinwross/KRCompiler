/*
 * 64-bit integer lowering on KR32: the value is a word pair, so every case
 * here crosses the 32-bit word boundary at least once.
 *
 * main returns 0 when every check passes; each failed check sets one bit of
 * the exit status so a single run names the failure.
 */
int printf(const char *format, ...);

static long long add64(long long left, long long right) {
    return left + right;
}

static long long subtract64(long long left, long long right) {
    return left - right;
}

static long long multiply64(long long left, long long right) {
    return left * right;
}

static long long divide64(long long left, long long right) {
    return left / right;
}

static long long stack64(int first, int second, int third, long long fourth) {
    return fourth + first + second + third;
}

int main(void) {
    long long dividend = 5000000001LL;
    long long quotient = divide64(dividend, 7);
    long long remainder = dividend % 7;
    unsigned long long wide = 0x123456789ABCDEFULL;
    unsigned long long high = wide >> 36;
    long long carried = add64(0xFFFFFFFFLL, 1LL);
    long long borrowed = subtract64(0x100000000LL, 1LL);
    long long product = multiply64(0x100000000LL, 3LL);
    long long stacked = stack64(1, 2, 3, 0x800000000LL);
    int failures = 0;

    if (quotient != 714285714LL) {
        failures |= 1;
    }
    if (remainder != 3LL) {
        failures |= 2;
    }
    if (high != 0x123456ULL) {
        failures |= 4;
    }
    if (carried != 0x100000000LL) {
        failures |= 8;
    }
    if (borrowed != 0xFFFFFFFFLL) {
        failures |= 16;
    }
    if (product != 0x300000000LL) {
        failures |= 32;
    }
    if (stacked != 0x800000006LL) {
        failures |= 64;
    }
    if (divide64(dividend, 5) != 1000000000LL) {
        failures |= 128;
    }
    printf("%lld %lld %llu %lld\n", dividend, quotient, high, stacked);
    return failures;
}
