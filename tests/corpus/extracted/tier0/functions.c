/* Tier 0 — functions. Calls, recursion, and the argument-count boundary.
 *
 * `sum5` takes five arguments on purpose: KR16 passes the first three in R1-R3
 * and pushes the rest, so this is the smallest program that exercises the
 * extra-argument rule on the embedded profile. `recurse` covers a call whose
 * argument is the result of another call. */

int add(int a, int b) {
    return a + b;
}

int sum5(int a, int b, int c, int d, int e) {
    return a + b + c + d + e;
}

int factorial(int n) {
    if (n <= 1) {
        return 1;
    }
    return n * factorial(n - 1);
}

/* A call used directly as an argument to another call. */
int twice(int value) {
    return value * 2;
}

int main(void) {
    if (add(2, 3) != 5) { return 1; }
    if (sum5(1, 2, 3, 4, 5) != 15) { return 2; }
    if (factorial(5) != 120) { return 3; }
    if (add(twice(4), twice(5)) != 18) { return 4; }

    /* A function pointer call, which takes a different path through the backend. */
    int (*pointer_to_add)(int, int) = add;
    if (pointer_to_add(20, 22) != 42) { return 5; }

    /* A local whose value comes from a call, then passed on. */
    int held = sum5(1, 1, 1, 1, 1);
    if (held != 5) { return 6; }
    if (twice(held) != 10) { return 7; }

    return 0;
}
