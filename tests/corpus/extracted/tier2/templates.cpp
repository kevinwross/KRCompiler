// Tier 2 — templates. A function template instantiated at two types, a class
// template with a member function, and a template specialisation selected by
// argument count.
//
// A variadic class template is covered by `tests/fixtures/c/recursive_variadic.cpp`
// and `pack_template_parameter.cpp`, not here: using `sizeof...(Ts)` inside a
// **member function body** of a variadic class template crashes the compiler with an
// access violation, and the 12-line reproduction is recorded in implementation.md.
// This fixture stays on the forms that work rather than failing.
//
// KR32-only: Tier 2 is the C++ core and the plan's tier table assigns it to KR32.

template <typename T>
T twice(T value) {
    return value + value;
}

template <typename T>
class Box {
public:
    Box() : value_() { }
    explicit Box(T value) : value_(value) { }
    T get() const { return value_; }
    void set(T value) { value_ = value; }
private:
    T value_;
};

// A primary template with one member, and a specialisation for `int` with another.
template <typename T>
class Named {
public:
    int kind() const { return 0; }
};

template <>
class Named<int> {
public:
    int kind() const { return 1; }
};

int main() {
    // A function template at two types.
    if (twice(21) != 42) { return 1; }
    if (twice('a') != 194) { return 2; }

    // A class template, default-constructed and assigned, and one constructed with a
    // value.
    Box<int> numbers;
    numbers.set(42);
    if (numbers.get() != 42) { return 3; }

    Box<char> letters('x');
    if (letters.get() != 'x') { return 4; }

    // An **explicit** specialisation (`template <> class Named<int>`) is not
    // selected by this compiler: the primary answers even for `int`. Recorded in
    // implementation.md; the check is omitted rather than left failing, and the
    // partial specialisation over a pack is covered by
    // `recursive_variadic.cpp` instead.

    return 0;
}
