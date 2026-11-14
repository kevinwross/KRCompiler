// A cast to a reference is a value in an expression when the referenced type is
// not a record, and the record's address when it is. Before this,
// `int moved = static_cast<int&&>(first)` stored the referent's address in the
// integer. Returns 42.

struct Item {
    int value;
};

int main() {
    int first = 5;
    int direct = first;
    if (direct != 5) {
        return 1;
    }

    // A cast to an rvalue reference copies the integer.
    int moved = static_cast<int&&>(first);
    if (moved != 5) {
        return 2;
    }

    // A cast to a const reference copies it too.
    int seen = static_cast<const int&>(first);
    if (seen != 5) {
        return 3;
    }

    // The value is a copy: changing the source does not change it.
    first = 9;
    if (moved != 5 || seen != 5) {
        return 4;
    }

    // A cast to a record reference still names the object, so a read through it
    // follows the object.
    Item item;
    item.value = 11;
    Item& named = static_cast<Item&&>(item);
    if (named.value != 11) {
        return 5;
    }
    item.value = 13;
    if (named.value != 13) {
        return 6;
    }
    return 42;
}
