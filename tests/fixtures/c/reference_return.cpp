// A function that returns a reference hands the caller the object it names, so a
// binding aliases it and a write through the call reaches it. A copy of the
// return is the value. ADR 0036. Returns 42.

int& first_of(int* values) { return values[0]; }

template <typename T>
T&& forward_to(T& value) {
    return static_cast<T&&>(value);
}

struct Item {
    int value;
};

Item& pick(Item& item) { return item; }

int main() {
    int array[2];
    array[0] = 7;
    if (first_of(array) != 7) {
        return 1;
    }

    // A copy of a reference return is the value.
    int copy = first_of(array);
    if (copy != 7) {
        return 2;
    }

    // A template's reference return works the same way.
    int value = 5;
    if (forward_to(value) != 5) {
        return 3;
    }

    // A binding names the object, so a change through the object is visible.
    Item item;
    item.value = 11;
    Item& named = pick(item);
    if (named.value != 11) {
        return 4;
    }
    item.value = 13;
    if (named.value != 13) {
        return 5;
    }

    // A write through the binding reaches the object.
    named.value = 17;
    if (item.value != 17) {
        return 6;
    }

    // A write through the call itself reaches it too.
    pick(item).value = 19;
    if (item.value != 19) {
        return 7;
    }
    return 42;
}