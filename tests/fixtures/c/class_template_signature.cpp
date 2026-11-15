// A function template may take a class template type in its signature, and a
// non-type parameter beside it. The pattern is only a signature -- `array<T, N>`
// is not a record until N is bound -- so each call instantiates with its own
// arguments and the parameter type is formed from the clone. Returns 42.
//
// The value parameter must be written at the call: it is not deduced from an
// argument's type.

#include <array>

template <typename T, int N>
int count_of(const std::array<T, N>& values) {
    return values.size();
}

template <typename T, int N>
T first_of(const std::array<T, N>& values) {
    return values[0];
}

int main() {
    std::array<int, 3> numbers;
    numbers[0] = 7;
    numbers[1] = 8;
    numbers[2] = 9;
    if (count_of<int, 3>(numbers) != 3) {
        return 1;
    }
    if (first_of<int, 3>(numbers) != 7) {
        return 2;
    }

    // A second instance of the same pattern has its own size.
    std::array<int, 2> pair;
    pair[0] = 4;
    pair[1] = 5;
    if (count_of<int, 2>(pair) != 2) {
        return 3;
    }
    if (first_of<int, 2>(pair) != 4) {
        return 4;
    }
    return 42;
}