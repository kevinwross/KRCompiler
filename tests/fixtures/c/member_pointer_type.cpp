// A class template's own record stands for the unparameterised spelling, so a
// body written there is analysed against stub types -- inside Holder<T>, `T*` is
// `int*` and `pointer->value` is not a member of an integer. Only an instance
// analyses and builds its members' bodies, with the bindings that make them
// valid. Before this, `pointer->value` reported "member access on a non-record
// value" and the pattern's own function failed verification. Returns 42.

struct Item {
    int value;
};

template <typename T>
struct Holder {
    T* pointer;
    int read() { return pointer->value; }
    void write(int value) { pointer->value = value; }
};

int main() {
    Item item;
    item.value = 5;

    Holder<Item> holder;
    holder.pointer = &item;
    if (holder.read() != 5) {
        return 1;
    }
    holder.write(9);
    if (item.value != 9) {
        return 2;
    }

    // A second instance of the same pattern is its own type.
    Item other;
    other.value = 3;
    Holder<Item> second;
    second.pointer = &other;
    if (second.read() != 3) {
        return 3;
    }
    return 42;
}