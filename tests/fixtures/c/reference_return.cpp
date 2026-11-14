// A function that returns a reference hands the caller the object it names: a
// caller that copies gets the value, and a template that does the same works the
// same way. Returns 42.
//
// Binding a reference variable to such a call is a recorded defect: the binding
// names a copy rather than the object, so this fixture reads through a returned
// reference without storing one.

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

    // A record reference return reads the object it names.
    Item item;
    item.value = 11;
    if (pick(item).value != 11) {
        return 4;
    }
    item.value = 13;
    if (pick(item).value != 13) {
        return 5;
    }
    return 42;
}