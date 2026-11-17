// Tier 2 — constexpr. A constexpr function used where a constant is required, a
// constexpr variable, and a constexpr function whose value is checked at run time
// as well as being usable as a bound.
//
// KR32-only: Tier 2 is the C++ core and the plan's tier table assigns it to KR32.

constexpr int square(int value) {
    return value * value;
}

constexpr int answer = square(6) + 6;

// A constexpr function with a conditional, which is the form `if constexpr` is built
// on and which must fold at compile time.
constexpr int larger(int left, int right) {
    return left > right ? left : right;
}

int main() {
    // Usable as a compile-time bound: an array of this size must be legal.
    int table[square(3)];
    table[8] = 42;
    if (table[8] != 42) { return 1; }

    // A constexpr variable is a constant expression.
    if (answer != 42) { return 2; }

    // The same functions are callable at run time with the same results.
    if (square(5) != 25) { return 3; }
    if (larger(3, 9) != 9) { return 4; }
    if (larger(9, 3) != 9) { return 5; }

    // A constexpr value used to size a second array.
    constexpr int count = larger(2, 4);
    int more[count];
    more[3] = 7;
    if (more[3] != 7) { return 6; }

    return 0;
}
