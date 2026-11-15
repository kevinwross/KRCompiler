// std::type_info members beyond name(): before() gives an implementation-defined
// order so a descriptor can key an ordered container, and raw_name() is the
// compiler's spelling. Returns 42.
//
// The name *strings* are not compared here: on KR16 two different classes report
// the same name(), which is a recorded defect in the descriptor, and a fixture
// that compared them could not pass until it is fixed.

#include <typeinfo>

struct First {
    virtual ~First() {}
};

struct Second {
    virtual ~Second() {}
};

int main() {
    const std::type_info& first = typeid(First);
    const std::type_info& second = typeid(Second);

    // Equality through a reference reaches the member operator.
    if (first != first) {
        return 1;
    }

    // before() is irreflexive.
    if (first.before(first)) {
        return 2;
    }

    // and antisymmetric.
    if (first.before(second) && second.before(first)) {
        return 3;
    }

    // A descriptor evaluates to itself.
    First object;
    if (typeid(object) != first) {
        return 4;
    }

    // raw_name() is the same spelling here.
    if (first.raw_name() != first.name()) {
        return 5;
    }
    return 42;
}