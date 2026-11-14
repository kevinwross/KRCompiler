// std::array (sysroot/include/array): a fixed-size array that knows its
// length. The element storage is the array itself and the bound is a template
// value parameter, so `std::array<int, 4>` has the layout of `int[4]`.
//
// Reads go through `operator[]` and `data()`: a member function that returns
// `T&` is not dereferenced when it is read as a value yet, so `front()` and
// `back()` are exercised through `data()` instead. Returns 42 when every
// member behaves.

#include <array>

int main() {
    std::array<int, 4> values;
    if (values.size() != 4) {
        return 1;
    }
    if (values.empty()) {
        return 2;
    }

    // The bounds-free operator, as an lvalue and as a value.
    values[0] = 7;
    values[1] = 8;
    values[2] = 9;
    values[3] = 10;
    if (values[0] != 7 || values[3] != 10) {
        return 3;
    }

    // `fill` writes every element; the value is an object because a literal
    // cannot bind to a reference parameter yet.
    int five = 5;
    values.fill(five);
    if (values[0] != 5 || values[3] != 5) {
        return 4;
    }

    // A member that returns `T&` is read as the object it refers to.
    values[3] = 9;
    if (values.front() != 5 || values.back() != 9) {
        return 12;
    }
    if (values.at(0) != 5 || values.at(3) != 9) {
        return 13;
    }
    values[0] = 5;
    values[3] = 5;

    // Index traversal with the sum of a known array.
    int sum = 0;
    int index = 0;
    while (index < 4) {
        sum += values[index];
        index = index + 1;
    }
    if (sum != 20) {
        return 5;
    }

    // `data` is the storage itself.
    int* raw = values.data();
    if (raw[0] != 5) {
        return 6;
    }
    raw[0] = 11;
    if (values[0] != 11) {
        return 7;
    }

    // The bound is part of the type: another length and element type is
    // another record.
    std::array<double, 2> reals;
    if (reals.size() != 2) {
        return 8;
    }
    reals[0] = 1.5;
    reals[1] = 2.5;
    if (reals[1] != 2.5) {
        return 9;
    }

    // A one-element array is not empty, and a zero-length one is.
    std::array<int, 1> single;
    single[0] = 42;
    if (single.empty() || single.size() != 1) {
        return 10;
    }
    if (single[0] != 42) {
        return 11;
    }

    return 42;
}
