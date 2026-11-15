// Two member functions may differ only in constness. A mutable object calls the
// non-const one; a const object and a const reference call the const one; and a
// method declared in one form only is callable through either. Before this, a
// const object reported "call to non-const member function ... on a const
// object", because the lookup took the first matching name. Returns 42.

struct Box {
    int value;
    int get() { return value; }
    int get() const { return value * 100; }
    int only_const() const { return value + 1; }
};

int main() {
    Box box;
    box.value = 2;

    if (box.get() != 2) {
        return 1;
    }
    const Box frozen = box;
    if (frozen.get() != 200) {
        return 2;
    }
    const Box& read_only = box;
    if (read_only.get() != 200) {
        return 3;
    }
    if (box.only_const() != 3) {
        return 4;
    }
    if (frozen.only_const() != 3) {
        return 5;
    }
    return 42;
}