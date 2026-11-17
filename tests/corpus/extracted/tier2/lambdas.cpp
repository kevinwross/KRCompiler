// Tier 2 — lambdas. A captureless lambda, a by-value capture, a by-reference
// capture, and a lambda stored in a variable and called twice.
//
// KR32-only: Tier 2 is the C++ core and the plan's tier table assigns it to KR32.

int apply(int (*function)(int), int value) {
    return function(value);
}

int main() {
    // A captureless lambda is a plain function.
    if (apply([](int value) { return value + 1; }, 41) != 42) { return 1; }

    // By-value capture: the lambda keeps the value it saw, so a later change to the
    // variable does not reach it.
    int captured = 10;
    int (*by_value)(int) = [captured](int value) { return value + captured; };
    captured = 100;
    if (by_value(1) != 11) { return 2; }

    // By-reference capture: the lambda sees the variable, so a later change does
    // reach it.
    int shared = 5;
    int (*by_reference)(int) = [&shared](int value) { return value + shared; };
    shared = 7;
    if (by_reference(1) != 8) { return 3; }

    // A lambda that mutates the variable it captured by reference.
    int total = 0;
    int (*accumulate)(int) = [&total](int value) { total = total + value; return total; };
    if (accumulate(2) != 2) { return 4; }
    if (accumulate(3) != 5) { return 5; }

    return 0;
}
