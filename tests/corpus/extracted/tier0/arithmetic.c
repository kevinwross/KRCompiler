/* Tier 0 — arithmetic. Integer operators only, so the program is portable to both
 * targets: KR16's IP16 profile rejects double and float.
 *
 * The exit status is the failure, so a wrong operator is reported by its number
 * rather than by a printed value that a smoke test would not compare. */

int main(void) {
    int a = 7;
    int b = 3;

    if (a + b != 10) { return 1; }
    if (a - b != 4) { return 2; }
    if (a * b != 21) { return 3; }
    if (a / b != 2) { return 4; }
    if (a % b != 1) { return 5; }
    if (-a != -7) { return 6; }

    if ((a << 2) != 28) { return 7; }
    if ((a >> 1) != 3) { return 8; }
    if ((a & b) != 3) { return 9; }
    if ((a | b) != 7) { return 10; }
    if ((a ^ b) != 4) { return 11; }
    if (~a != -8) { return 12; }

    /* Comparison and the three-way operator, which is a common lowering gap. */
    if (!(a > b)) { return 13; }
    if (!(b < a)) { return 14; }
    if (a == b) { return 15; }
    if (a != b) { } else { return 16; }

    /* Compound assignment and increment in both prefix and postfix position. */
    int c = 10;
    c += 5;
    if (c != 15) { return 17; }
    c -= 3;
    if (c != 12) { return 18; }
    c *= 2;
    if (c != 24) { return 19; }
    if (c++ != 24) { return 20; }
    if (c != 25) { return 21; }
    if (++c != 26) { return 22; }

    /* Conversion between widths, which is where an IP16 profile differs most. */
    char small = 100;
    int widened = small;
    if (widened != 100) { return 23; }
    int narrowed = 300;
    char wrapped = (char)narrowed;
    if (wrapped != 44) { return 24; }

    return 0;
}
