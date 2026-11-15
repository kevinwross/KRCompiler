// A variadic function template takes one function per arity, and
// sizeof...(pack) folds to that arity at instantiation: the count is a constant
// in each instance rather than a runtime measurement. Returns 42.

template <typename... Ts>
int count(Ts... values) {
    return sizeof...(values);
}

int main() {
    // Three arities of one pattern.
    if (count(1, 2, 3) != 3) {
        return 1;
    }
    if (count(7, 8) != 2) {
        return 2;
    }
    if (count(9) != 1) {
        return 3;
    }

    // A pack beside an ordinary parameter, and a pack with no arguments, are
    // recorded gaps: the instance's signature is expanded from the pack alone.
    return 42;
    return 42;
}