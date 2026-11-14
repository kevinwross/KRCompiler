// A class that declares both a copy constructor and a move constructor keeps
// both: they share a name and arity, so each declaration must be matched to the
// method recorded with its own parameter category. Before this the copy
// constructor was never generated and the move constructor was emitted twice,
// which left the move that ran not clearing its source. Returns 42.

template <typename T>
class Box {
public:
    Box() : value(0) {}
    Box(Box&& other) : value(0) {
        value = other.value;
        other.value = 0;
    }
    Box(const Box& other) : value(other.value) {}

    int get() const { return value; }
    void set(int next) { value = next; }

    void steal(Box&& other) {
        value = other.value;
        other.value = 0;
    }

private:
    T value;
};

int main() {
    Box<int> first;
    first.set(7);

    // A move clears its source.
    Box<int> moved(static_cast<Box<int>&&>(first));
    if (moved.get() != 7) {
        return 1;
    }
    if (first.get() != 0) {
        return 2;
    }

    // A copy leaves its source alone.
    Box<int> copied(first);
    first.set(5);
    if (copied.get() != 0) {
        return 3;
    }
    if (first.get() != 5) {
        return 4;
    }

    // The copy constructor reads the value it copies.
    Box<int> source;
    source.set(11);
    Box<int> duplicated(source);
    if (duplicated.get() != 11) {
        return 5;
    }
    if (source.get() != 11) {
        return 6;
    }

    // The same through a member function rather than a constructor.
    Box<int> target;
    target.steal(static_cast<Box<int>&&>(source));
    if (target.get() != 11) {
        return 7;
    }
    if (source.get() != 0) {
        return 8;
    }
    return 42;
}
