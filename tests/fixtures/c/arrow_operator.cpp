// A class that overloads `operator->` answers `owner->member` with the record
// the operator returns, so an owner type reads and writes through the arrow the
// way a pointer does. `operator*` is the other half. Returns 42.

struct Item {
    int value;
    Item(int start) : value(start) {}
    int read() const { return value; }
    void set(int next) { value = next; }
};

template <typename T>
class Owner {
public:
    Owner() : pointer(0) {}
    explicit Owner(T* value) : pointer(value) {}
    T& operator*() const { return *pointer; }
    T* operator->() const { return pointer; }
    T* get() const { return pointer; }
    void reset(T* value) { pointer = value; }

private:
    T* pointer;
};

int main() {
    Item item(7);
    Owner<Item> owner(&item);

    // A member read through the arrow.
    if (owner->value != 7) {
        return 1;
    }

    // A member call through the arrow.
    if (owner->read() != 7) {
        return 2;
    }

    // A write through the arrow reaches the object it refers to.
    owner->value = 9;
    if (item.value != 9) {
        return 3;
    }
    owner->set(11);
    if (item.read() != 11) {
        return 4;
    }

    // The dereference form agrees with the arrow form.
    if ((*owner).read() != 11) {
        return 5;
    }

    // Two owners over different objects stay independent.
    Item other(5);
    Owner<Item> second(&other);
    second->value = 1;
    if (item.value != 11 || other.value != 1) {
        return 6;
    }

    // A reset owner reads through the new object.
    Owner<Item> moved;
    if (moved.get() != 0) {
        return 7;
    }
    moved.reset(&other);
    if (moved->read() != 1) {
        return 8;
    }
    return 42;
}
