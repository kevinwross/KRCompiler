// One of two translation units that instantiate the same template. Each emits
// the instance weak; the linker keeps one and the program links.

template <typename T>
T sum_three(T first, T second, T third) {
    return first + second + third;
}

int from_a() {
    return sum_three(21, 10, 11);
}