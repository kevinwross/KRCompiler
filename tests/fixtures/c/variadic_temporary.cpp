// A member call on a temporary of a variadic class template.
//
// `P<int>().c()` failed with `no member named 'c'` while `P<int> p; p.c();` worked. The
// cause was not the temporary: a constructor-style call names its class with an identifier
// carrying `template-id` and `template-arg:` labels, and the constructor path ignored those
// labels and returned the ordinary symbol's type -- the **pattern**. The pattern has the
// methods, but its record is not the instance's, so code generation looked up
// `class_methods_["P__int__1"]`, missed, and fell back to a field.
//
// Instantiating on that path, during analysis, means the instance exists before code
// generation collects class methods.
//
// Returns 42.

template <typename... Ts>
class P {
public:
    int count() const { return sizeof...(Ts); }

    int zero() const { return 0; }
};

template <typename T>
class Box {
public:
    Box() : value_(0) { }

    void set(T value) { value_ = value; }

    T value() const { return value_; }

private:
    T value_;
};

int main() {
    // A temporary of a variadic class template.
    if (P<int>().count() != 1) { return 1; }
    if (P<int, char>().count() != 2) { return 2; }
    if (P<int, char, char>().count() != 3) { return 3; }

    // A second member on the same temporary type.
    if (P<char>().zero() != 0) { return 4; }

    // The named form still works and agrees with the temporary.
    P<int> named;
    if (named.count() != P<int>().count()) { return 5; }

    // A non-variadic template temporary, through the same path.
    if (Box<int>().value() != 0) { return 6; }
    Box<int> box;
    box.set(7);
    if (box.value() != 7) { return 7; }

    return 42;
}
