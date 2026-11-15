// <utility>: std::swap exchanges two objects, and std::move names its argument
// as an rvalue so a value context copies the value. Both are function templates
// in the header; the compiler treats a move as a cast, which is what the
// language says it is. Returns 42.
//
// Constructing a record from std::move(x) is a recorded gap: it compiles but
// reads the wrong value, so this fixture stays on scalars.

#include <utility>

struct Item {
    int value;
};

int main() {
    int first = 3;
    int second = 5;
    std::swap(first, second);
    if (first != 5 || second != 3) {
        return 1;
    }

    // A move names its argument; copying it copies the value.
    int moved = std::move(first);
    if (moved != 5) {
        return 2;
    }
    if (first != 5) {
        return 3;
    }

    // The source is unchanged, because a move is a cast and not a transfer.
    first = 9;
    if (moved != 5) {
        return 4;
    }

    // Swapping records exchanges their fields.
    Item left;
    left.value = 11;
    Item right;
    right.value = 13;
    std::swap(left, right);
    if (left.value != 13 || right.value != 11) {
        return 5;
    }
    return 42;
}