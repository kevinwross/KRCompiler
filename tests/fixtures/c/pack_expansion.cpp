// `f(pack...)` forwards a parameter pack as that many arguments. The pattern's
// signature names its parameters `values`, `values1`, ..., so the expansion is
// those identifiers in the argument list, written at instantiation because the
// arity exists only per instance. Returns 42.

template <typename... Ts>
int count(Ts... values) {
    return sizeof...(values);
}

template <typename... Ts>
int forwarded(Ts... values) {
    return count(values...);
}

template <typename... Ts>
int additive(int seed, Ts... values) {
    // A pack forwards beside an ordinary argument.
    return seed + count(values...);
}

int main() {
    if (forwarded(1, 2, 3) != 3) {
        return 1;
    }
    if (forwarded(9) != 1) {
        return 2;
    }
    if (additive(40, 1, 2) != 42) {
        return 3;
    }
    return 42;
}