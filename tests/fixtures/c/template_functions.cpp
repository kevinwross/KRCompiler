// Function template deduction and instantiation (ADR 0031): a call whose
// deduced type is not the int substitution instantiates the pattern, so one
// translation unit can hold several instances of the same template. Returns 42
// when each instance computes with its own type.

template <typename T>
T add(T left, T right) {
    return left + right;
}

template <typename T>
T maximum(T left, T right) {
    return left > right ? left : right;
}

template <typename T>
T doubled(T value) {
    return value + value;
}

template <typename T>
T first_of(T* values) {
    return values[0];
}

int main() {
    // The all-int call uses the definition the parser substitutes.
    if (add(3, 4) != 7) {
        return 1;
    }
    // The same template instantiated for double alongside it.
    if (add(1.5, 2.5) != 4.0) {
        return 2;
    }

    // The conditional operator keeps its operand type inside an instance.
    if (maximum(3, 9) != 9) {
        return 3;
    }
    if (maximum(2.5, 1.5) != 2.5) {
        return 4;
    }

    // A single-parameter template instantiated three ways.
    if (doubled(21) != 42) {
        return 5;
    }
    if (doubled(1.25) != 2.5) {
        return 6;
    }
    char small = 2;
    if (doubled(small) != 4) {
        return 7;
    }

    // A pointer pattern deduces through array decay.
    int values[3];
    values[0] = 11;
    values[1] = 22;
    values[2] = 33;
    if (first_of(values) != 11) {
        return 8;
    }
    double reals[2];
    reals[0] = 1.5;
    reals[1] = 2.5;
    if (first_of(reals) != 1.5) {
        return 9;
    }

    // Calling an instance again reuses it rather than defining another.
    int total = 0;
    int index = 0;
    while (index < 3) {
        total = total + doubled(index);
        index = index + 1;
    }
    if (total != 6) {
        return 10;
    }

    return 42;
}
