/* Compute-shaped workload for the optimiser baseline.
 *
 * Five kernels that a pass could plausibly move, run in sequence so one tick count per
 * target covers the set. Each result is checked against the value the same algorithm
 * produces by hand, and main returns 42 only when all five agree -- so a pass that makes
 * the code faster by computing the wrong thing fails the measurement rather than improving
 * it.
 *
 * Deliberately KR16-safe: no `double`, no `long long`, no variadics. The IP16 profile
 * rejects the first two, and a benchmark that cannot run on one of the two targets is not
 * a baseline for both.
 */

static int fail(int code) { return code; }

/* Integer arithmetic in a loop: the shape constant folding and strength reduction act on. */
static int kernel_arith(int n) {
    int acc = 0;
    int i;
    for (i = 1; i <= n; ++i) {
        acc += i * 3 - (i / 2) + (i % 7);
    }
    return acc;
}

/* Nested loop with a carried dependency: harder to vectorise, easy to CSE badly. */
static int kernel_nested(int n) {
    int sum = 0;
    int i;
    int j;
    for (i = 0; i < n; ++i) {
        for (j = 0; j < 8; ++j) {
            sum += i ^ j;
        }
    }
    return sum;
}

/* Recursion: move/copy elimination and frame traffic show up here rather than in loops. */
static int kernel_fib(int n) {
    if (n < 2) {
        return n;
    }
    return kernel_fib(n - 1) + kernel_fib(n - 2);
}

struct Pair { int a; int b; };

/* Struct copies by value: aggregate argument and return paths, not arithmetic. */
static struct Pair kernel_struct(int n) {
    struct Pair out;
    struct Pair acc;
    int i;
    acc.a = 0;
    acc.b = 0;
    for (i = 0; i < n; ++i) {
        out.a = acc.a + i;
        out.b = acc.b + 1;
        acc = out;
    }
    return acc;
}

/* Pointer walk over a fixed buffer: addressing modes and slot reuse. */
static int kernel_pointer(int n) {
    int buffer[64];
    int* p;
    int i;
    int total = 0;
    for (i = 0; i < 64; ++i) {
        buffer[i] = i * 2;
    }
    p = buffer;
    for (i = 0; i < n; ++i) {
        total += *p;
        p = p + 1;
        if (p == buffer + 64) {
            p = buffer;
        }
    }
    return total;
}

int main(void) {
    struct Pair pair;

    if (kernel_arith(100) != 12947) {
        return fail(1);
    }
    if (kernel_nested(50) != 9848) {
        return fail(2);
    }
    if (kernel_fib(17) != 1597) {
        return fail(3);
    }
    pair = kernel_struct(100);
    if (pair.a != 4950 || pair.b != 100) {
        return fail(4);
    }
    if (kernel_pointer(150) != 8526) {
        return fail(5);
    }
    return 42;
}
