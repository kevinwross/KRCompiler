/*
 * A conditional expression whose branches are 64-bit must keep both words.
 * Every shape below is a bit copy of a long long, so a truncating cast shows
 * up as a wrong value rather than a wrong branch.
 *
 * main returns 0 when every check passes; each failure sets one bit.
 */
int main(void) {
    long long value = 5000000001LL;
    int failures = 0;

    unsigned long long both = value < 0 ? (unsigned long long)value : (unsigned long long)value;
    if (both != 5000000001ULL) {
        failures |= 1;
    }
    unsigned long long positive = value > 0 ? (unsigned long long)value : 7ULL;
    if (positive != 5000000001ULL) {
        failures |= 2;
    }
    unsigned long long negative = value < 0 ? 7ULL : (unsigned long long)value;
    if (negative != 5000000001ULL) {
        failures |= 4;
    }
    unsigned long long magnitude =
        value < 0 ? (unsigned long long)(-(value + 1)) + 1 : (unsigned long long)value;
    if (magnitude != 5000000001ULL) {
        failures |= 8;
    }
    return failures;
}
