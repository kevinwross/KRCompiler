// The injected class name inside a template class body (ADR 0032): `C` in a
// member's signature means the instance, so `C* self()` returns the same type
// as the object it is called on. Returns 42 when every instance behaves.

template <typename T>
class Cell {
public:
    Cell() : value() {}
    // The injected name in a return type and in a parameter type.
    Cell* self() { return this; }
    const Cell* self() const { return this; }
    bool same(const Cell& other) const { return value == other.value; }
    void set(T next) { value = next; }
    T get() const { return value; }

private:
    T value;
};

int main() {
    Cell<int> whole;
    whole.set(5);
    if (whole.get() != 5) {
        return 1;
    }

    // The pointer the member returns names the same object.
    Cell<int>* pointer = whole.self();
    if (pointer != &whole) {
        return 2;
    }
    pointer->set(7);
    if (whole.get() != 7) {
        return 3;
    }

    // A const object uses the const overload.
    const Cell<int>* const_pointer = whole.self();
    if (const_pointer != &whole) {
        return 4;
    }

    // The parameter type is the instance, so two objects compare.
    Cell<int> other;
    other.set(7);
    if (!whole.same(other)) {
        return 5;
    }
    other.set(9);
    if (whole.same(other)) {
        return 6;
    }

    // Another element type is another record, and its injected name means it.
    Cell<double> real;
    real.set(1.5);
    if (real.get() != 1.5) {
        return 7;
    }
    Cell<double>* real_pointer = real.self();
    if (real_pointer != &real) {
        return 8;
    }

    return 42;
}
