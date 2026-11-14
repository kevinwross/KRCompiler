// A class that overloads dereference answers `*object` with its member, in a
// value context, in an lvalue context (`(*object).field = v` writes the object
// it refers to), and as a receiver for a member call. Returns 42.

struct Item {
    int value;
    Item(int start) : value(start) {}
    int read() const { return value; }
    void set(int next) { value = next; }
};

// A reference-shaped owner: the dereference yields the object itself.
template <typename T>
class Ref {
public:
    explicit Ref(T* value) : pointer(value) {}
    T& operator*() const { return *pointer; }
    T* operator->() const { return pointer; }
    T* get() const { return pointer; }

private:
    T* pointer;
};

int main() {
    Item item(7);
    Ref<Item> reference(&item);

    // Value context: the dereference is read.
    if ((*reference).read() != 7) {
        return 1;
    }

    // Lvalue context: the assignment writes the object the dereference names.
    (*reference).value = 9;
    if (item.value != 9) {
        return 2;
    }
    (*reference).set(11);
    if (item.read() != 11) {
        return 3;
    }

    // The address the dereference returns is the receiver's.
    if (reference.get() != &item) {
        return 4;
    }

    // A copy out of the dereference reads the same value.
    int copy = (*reference).read();
    if (copy != 11) {
        return 5;
    }

    // A second owner over a different object stays independent.
    Item other(5);
    Ref<Item> second(&other);
    (*second).value = 1;
    if (item.value != 11 || other.value != 1) {
        return 6;
    }
    return 42;
}
