// Constructing or assigning a record from std::move(x) copies its contents. A
// move names its argument, so a record move is the argument's address and the
// initialiser copies field by field; before this the aggregate path ran instead
// and stored the address into the first field, so the new object held garbage.
// Returns 42.
//
// A plain copy of a two-field record is a separate recorded gap: it compiles and
// reads the wrong value, with or without this change, so it is not asserted here.

#include <utility>

struct Item {
    int value;
    int other;
};

int main() {
    Item item;
    item.value = 7;
    item.other = 9;

    // Constructing from a move copies both fields and leaves the source alone.
    Item taken(std::move(item));
    if (taken.value != 7 || taken.other != 9) {
        return 1;
    }
    if (item.value != 7 || item.other != 9) {
        return 2;
    }

    // Assigning from a move does the same.
    Item assigned;
    assigned.value = 0;
    assigned.other = 0;
    assigned = std::move(item);
    if (assigned.value != 7 || assigned.other != 9) {
        return 3;
    }

    // A single-field record through a move still works.
    struct Small {
        int value;
    };
    Small small;
    small.value = 5;
    Small small_taken(std::move(small));
    if (small_taken.value != 5) {
        return 4;
    }
    return 42;
}