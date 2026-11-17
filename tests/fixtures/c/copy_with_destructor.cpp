// Copy construction of a class that declares a destructor and an explicit
// constructor.
//
// This shape selected the wrong constructor: `Counter copy(original)` matched
// `explicit Counter(int)` by arity alone and passed the record where an int belonged,
// which the IR verifier rejected with `call argument type mismatch`. The other copy
// fixtures do not declare a destructor, so none of them reached this path.
//
// Returns 42 when the source is unchanged, the copy is independent, and the
// destructor runs for each object.

int destructed = 0;

class Counter {
public:
    Counter() : value_(0), live_(1) { }

    explicit Counter(int start) : value_(start), live_(1) { }

    ~Counter() {
        destructed = destructed + live_;
        live_ = 0;
    }

    int value() const {
        return value_;
    }

    void add(int amount) {
        value_ = value_ + amount;
    }

private:
    int value_;
    int live_;
};

int main() {
    Counter original(5);
    Counter copy(original);

    // Writing the copy must not reach the original.
    copy.add(10);
    if (original.value() != 5) { return 1; }
    if (copy.value() != 15) { return 2; }

    // The copy is a distinct object with its own destructor to run.
    destructed = 0;
    {
        Counter inner(1);
        Counter inner_copy(inner);
        if (inner_copy.value() != 1) { return 3; }
    }
    if (destructed != 2) { return 4; }

    return 42;
}
