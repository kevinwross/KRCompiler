// A pointer-returning variadic function template with an explicit argument. The
// parameters hang off a FunctionDeclarator nested under a PointerDeclarator, and
// reading the outer node's children made the deduction loop see one
// non-parameter child, so the pack was never bound and the instance was named
// from an unknown type. Returns 42.

struct Item {
    int value;
    Item(int start) : value(start) {}
};

template <typename T, typename... Args>
T* construct(Args... args) {
    return new T(args...);
}

int main() {
    Item* one = construct<Item>(41);
    if (one->value != 41) { return 1; }
    Item* zero = construct<Item>(0);
    if (zero->value != 0) { return 2; }
    return 42;
}