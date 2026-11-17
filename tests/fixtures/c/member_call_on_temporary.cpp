// A member call on a temporary: `One().value()`.
//
// This was rejected with `constructor-initialized class has no constructor`, because the
// temporary path required a **declared** zero-argument constructor even though the class
// has an implicit one, exactly as a declared local of the same type is constructed fine.
// The compiler now zero-initialises the temporary when no constructor is declared.
//
// A temporary of a class that **does** declare a constructor still cannot have a member
// called on it: the temporary is loaded as a record value and bitcast to a pointer for
// `this`, so the callee reads garbage. That is a separate defect, recorded in
// implementation.md, and this fixture stays on the case that works.
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

    // A class with a declared constructor works as a named object.
    Holder holder;
    holder.set(7);
    if (holder.value() != 7) { return 2; }

    return 42;
}
