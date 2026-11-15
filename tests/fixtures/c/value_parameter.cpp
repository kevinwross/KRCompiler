// A function template may take a non-type parameter. The pattern itself cannot
// be called without one, so it is only a declaration and each call instantiates
// its own definition with the number bound: `constant<3>` folds N to 3 in the
// body rather than looking for a variable. Returns 42.

template <int N>
int constant() {
    return N;
}

template <typename T, int Factor>
T scaled(T value) {
    return value * Factor;
}

int main() {
    if (constant<3>() != 3) {
        return 1;
    }
    if (constant<7>() != 7) {
        return 2;
    }
    if (scaled<int, 4>(3) != 12) {
        return 3;
    }
    // A type parameter and a value parameter in one call.
    if (scaled<char, 2>('a') != static_cast<char>('a' * 2)) {
        return 4;
    }
    return 42;
}