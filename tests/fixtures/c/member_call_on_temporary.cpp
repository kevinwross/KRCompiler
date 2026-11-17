// A member call on a temporary: `One().value()` and `Holder().value()`.
//
// Two defects lived here. First, a class with an **implicit** default constructor was
// rejected with `constructor-initialized class has no constructor`, because the temporary
// path required a declared one. Second, and visible in the IR, a temporary of a class that
// *does* declare a constructor was loaded as a record value and then **bitcast from a
// record to a pointer** to serve as `this`:
//
//     %temporary.value = load %Holder, %temporary.record
//     %this.cast = bitcast %Holder %temporary.value to ptr<%Holder>
//
// The callee read whatever the load contained, so `Holder().value()` returned garbage.
// A member call now materialises the base and passes its address.
//
// This file was a diagnostic fixture for the first error until it was fixed; the
// expectation it encoded *was* the defect.
//
// Returns 42.

class One {
public:
    int value() const {
        return 42;
    }
};

class Holder {
public:
    Holder() : value_(0) { }

    explicit Holder(int value) : value_(value) { }

    void set(int value) {
        value_ = value;
    }

    int value() const {
        return value_;
    }

private:
    int value_;
};

int main() {
    // A class with an implicit default constructor, through the temporary path.
    if (One().value() != 42) { return 1; }

    // A class with a declared constructor, through the same path: the callee must see a
    // zero-initialised object, not whatever the load happened to contain.
    if (Holder().value() != 0) { return 2; }

    // A temporary built by a converting constructor.
    if (Holder(7).value() != 7) { return 3; }

    // A named object still works, and is distinct from the temporaries above.
    Holder holder;
    holder.set(9);
    if (holder.value() != 9) { return 4; }

    // The temporary's writes do not reach a named object.
    Holder other;
    other.set(3);
    if (Holder().value() != 0) { return 5; }
    if (other.value() != 3) { return 6; }

    return 42;
}
