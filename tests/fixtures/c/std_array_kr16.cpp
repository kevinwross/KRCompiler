// std::array on the sixteen-bit profile: a fixed-size array that knows its
// length, with the element storage being the array itself and the bound a
// template value parameter. This is the int-only surface, because that profile
// has no `double`, and a record element is left out because the member
// destructor's loop indexes an array of records with a dynamic index, which is
// not lowered on this profile yet.
//
// `at` stops the program on a bad index here, since a profile without
// exceptions cannot throw; only a valid index is exercised. Returns 42.

#include <array>

int main() {
    std::array<int, 4> values;
    if (values.size() != 4) {
        return 1;
    }
    if (values.empty()) {
        return 2;
    }

    // The bounds-free operator, written and read.
    values[0] = 7;
    values[1] = 8;
    values[2] = 9;
    values[3] = 10;
    if (values[0] != 7 || values[3] != 10) {
        return 3;
    }

    // The bounds-checked operator, on a valid index. It is read only: writing
    // through a reference-returning member call does not reach the element yet,
    // which is the recorded gap for a scalar reference return.
    if (values.at(1) != 8) {
        return 4;
    }
    if (values.at(2) != 9) {
        return 5;
    }

    // front, back, and data.
    if (values.front() != 7 || values.back() != 10) {
        return 6;
    }
    if (values.data()[1] != 8) {
        return 7;
    }

    // fill writes every element.
    values.fill(5);
    if (values[0] != 5 || values[3] != 5) {
        return 8;
    }

    // begin and end bound the elements.
    int total = 0;
    for (int* cursor = values.begin(); cursor != values.end(); cursor = cursor + 1) {
        total = total + *cursor;
    }
    if (total != 20) {
        return 9;
    }

    // Two arrays compare element by element.
    std::array<int, 4> same;
    same.fill(5);
    if (!(values == same)) {
        return 10;
    }
    if (values != same) {
        return 11;
    }
    same[3] = 6;
    if (values == same) {
        return 12;
    }
    if (!(values != same)) {
        return 13;
    }
    return 42;
}