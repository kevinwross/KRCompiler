// Class templates (ADR 0032): `Box<int>` and `Box<double>` name two records
// with their own layouts and member functions, a member may refer to its own
// instance, and a two-parameter template binds one type per parameter. Returns
// 42 when every instance behaves with its own types.

template <typename T>
class Box {
public:
    Box() : value() {}
    void set(T value_in) { value = value_in; }
    T get() const { return value; }
    void copy_from(const Box<T>& other) { value = other.get(); }

private:
    T value;
};

template <typename First, typename Second>
class Pair {
public:
    Pair() : first(), second() {}
    void put(First first_in, Second second_in) {
        first = first_in;
        second = second_in;
    }
    First left() const { return first; }
    Second right() const { return second; }

private:
    First first;
    Second second;
};

int main() {
    Box<int> outer;
    outer.set(5);
    if (outer.get() != 5) {
        return 1;
    }

    // A member that refers to its own instance.
    Box<int> copy;
    copy.copy_from(outer);
    if (copy.get() != 5) {
        return 2;
    }
    copy.set(11);
    if (copy.get() != 11) {
        return 3;
    }
    if (outer.get() != 5) {
        return 4;
    }

    // A second instance of the same template, with its own layout.
    Box<double> real;
    real.set(1.5);
    Box<double>* pointer = &real;
    pointer->set(2.5);
    if (pointer->get() != 2.5) {
        return 5;
    }
    if (real.get() != 2.5) {
        return 6;
    }

    Pair<int, double> mixed;
    mixed.put(7, 0.5);
    if (mixed.left() != 7) {
        return 7;
    }
    if (mixed.right() != 0.5) {
        return 8;
    }

    // The same two parameters in the other order are another type.
    Pair<double, int> swapped;
    swapped.put(0.25, 9);
    if (swapped.left() != 0.25) {
        return 9;
    }
    if (swapped.right() != 9) {
        return 10;
    }

    return 42;
}
