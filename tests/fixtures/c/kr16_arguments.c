// KR16 passes the first three scalar arguments in R1-R3 and pushes the rest,
// last first, so the fourth argument ends up just past the saved frame pointer
// (docs/abi-kr16.md, ADR 0028). Returns 42 when every form arrives intact.

static int total(int a, int b, int c, int d, int e) {
    return a + b + c + d + e;
}

static int seven(int a, int b, int c, int d, int e, int f, int g) {
    return a + b + c + d + e + f + g;
}

static int mixed(char first, int second, char third, int fourth) {
    return first + second + third + fourth;
}

static int indirect(int a, int b, int c, int d) {
    return a * 1000 + b * 100 + c * 10 + d;
}

static int (*pointer)(int, int, int, int);

static int with_locals(int a, int b, int c, int d, int e) {
    int memory[2];
    memory[0] = a + b;
    memory[1] = c + d;
    return memory[0] + memory[1] + e;
}

static int recurse(int depth, int a, int b, int c, int d) {
    if (depth == 0) {
        return a + b + c + d;
    }
    return recurse(depth - 1, a + 1, b + 1, c + 1, d + 1);
}

int main(void) {
    if (total(10, 20, 30, 40, 50) != 150) {
        return 1;
    }

    // Five stack slots, and a call whose own argument is another call.
    if (seven(1, 2, 3, 4, 5, 6, 7) != 28) {
        return 2;
    }
    if (seven(1, 2, 3, 4, 5, 6, total(1, 1, 1, 1, 1)) != 26) {
        return 3;
    }

    // Narrow arguments in stack positions occupy their own two-byte slot.
    if (mixed(1, 20, 3, 400) != 424) {
        return 4;
    }

    // A function with stack arguments and locals keeps both areas separate.
    if (with_locals(1, 2, 3, 4, 5) != 15) {
        return 5;
    }

    // The stack area is balanced across repeated and recursive calls.
    int sum = 0;
    int index = 0;
    while (index < 4) {
        sum += total(index, index, index, index, index);
        index++;
    }
    if (sum != 30) {
        return 6;
    }
    if (recurse(3, 1, 2, 3, 4) != 22) {
        return 7;
    }

    // An indirect call passes stack arguments the same way.
    pointer = indirect;
    if (pointer(1, 2, 3, 4) != 1234) {
        return 8;
    }

    return 42;
}
