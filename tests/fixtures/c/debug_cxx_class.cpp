// A C++ class whose debug records must name its methods and its type.
//
// The existing debug fixtures link and stop there, so nothing noticed whether a
// class's methods reach KDBG at all. This one is checked by
// `krc_debug_cxx_class`, which runs `krc --dump-symbols` over the linked image and
// requires the method and the class type to appear.
//
// Returns 42 so the same image can also be executed.

class Counter {
public:
    Counter() : value_(0) { }

    void add(int amount) {
        value_ = value_ + amount;
    }

    int value() const {
        return value_;
    }

private:
    int value_;
};

int main() {
    Counter counter;
    counter.add(42);
    return counter.value();
}
