// A variadic function template takes one function per arity, and
// sizeof...(pack) folds to that arity at instantiation: the count is a constant
// in each instance rather than a runtime measurement. Returns 42.

template <typename... Ts>
int count(Ts... values) {
    return sizeof...(values);
}

template <typename... Ts>
int with_fallback(int fallback, Ts... values) {
    return sizeof...(values) == 0 ? fallback : sizeof...(values);
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

    // A zero-arity pack is an instance with no parameters of its own.
    if (count() != 0) {
        return 4;
    }

    // A pack beside an ordinary parameter counts only its own arguments.
    if (with_fallback(9) != 9) {
        return 5;
    }
    if (with_fallback(9, 1, 2) != 2) {
        return 6;
    }
    return 42;
    return 42;
}