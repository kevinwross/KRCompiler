// `make_unique` on KR16.
//
// It was the case that motivated ADR 0048: `make_unique` returns its `unique_ptr` by value,
// so it needs aggregate returns, and KR16 rejected those before code generation. It works
// now that the destination travels as a hidden stack argument.
//
// Returns 42.

#include <memory>

int main() {
    auto value = make_unique<int>(7);
    if (*value != 7) { return 1; }

    *value = 9;
    if (*value != 9) { return 2; }

    auto second = make_unique<int>(3);
    if (*second != 3) { return 3; }

    // The first object is unaffected by the second.
    if (*value != 9) { return 4; }

    return 42;
}
