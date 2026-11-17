// Aggregate returns on KR16.
//
// KR16 has no free general-purpose register, so an aggregate return's destination travels as
// a hidden stack argument after every declared argument (ADR 0048). The callee reads it in its
// prologue, keeps it in a frame slot, and copies the aggregate to it in two-byte words.
//
// Three defects in the surrounding code had to be fixed before this worked, each of which
// truncated or misplaced the value: the frame layout gave an aggregate-typed value one
// two-byte slot regardless of its size, and a whole-record load and a whole-record store each
// moved a single word.
//
// Returns 42.

struct Pair {
    int a;
    int b;
};

struct Big {
    int x;
    int y;
    int z;
};

Pair make_pair() {
    Pair p;
    p.a = 3;
    p.b = 4;
    return p;
}

Big make_big(int seed) {
    Big value;
    value.x = seed;
    value.y = seed + 1;
    value.z = seed + 2;
    return value;
}

int main() {
    // Two fields of two bytes each: one word per field.
    Pair q = make_pair();
    if (q.a != 3) { return 1; }
    if (q.b != 4) { return 2; }

    // A larger aggregate, so the copy spans more than one word and must not overlap
    // the hidden argument's slot.
    Big big = make_big(10);
    if (big.x != 10) { return 3; }
    if (big.y != 11) { return 4; }
    if (big.z != 12) { return 5; }

    // Two calls in a row: the second must not inherit the first's destination.
    Pair first = make_pair();
    Pair second = make_pair();
    if (first.a != second.a) { return 6; }
    if (first.b != second.b) { return 7; }

    return 42;
}
