// A class template instance builds a member body only when something calls it.
// Item has no comparison operator, so array<Item, 2>::operator== could not be
// built -- and nothing compares the arrays, so it must not be. Returns 42.

#include <array>

struct Item {
    int value;
};

int main() {
    std::array<Item, 2> items;
    items[0].value = 5;
    items[1].value = 6;
    if (items[0].value != 5) {
        return 1;
    }
    if (items[1].value != 6) {
        return 2;
    }

    // A member that is used is still built: size, at and operator[] all run.
    if (items.size() != 2) {
        return 3;
    }
    items.at(0).value = 7;
    if (items[0].value != 7) {
        return 4;
    }
    return 42;
}