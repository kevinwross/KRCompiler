// The other translation unit. It instantiates the same template for the same
// type, so the second definition of the instance must be discarded rather than
// reported as a duplicate symbol. Returns 42.

template <typename T>
T sum_three(T first, T second, T third) {
    return first + second + third;
}

int from_a();

int main() {
    if (from_a() != 42) {
        return 1;
    }
    // The instance this unit defines is the one the linker kept, or the other;
    // either way the result is the same because the bodies are identical.
    if (sum_three(1, 2, 3) != 6) {
        return 2;
    }
    return 42;
}