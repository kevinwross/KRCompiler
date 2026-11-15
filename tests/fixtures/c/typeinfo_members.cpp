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

    // A scalar has a descriptor of its own, distinct from every class and from
    // other scalars, and equal to itself.
    const std::type_info& integer = typeid(int);
    const std::type_info& character = typeid(char);
    if (integer.hash_code() != typeid(int).hash_code()) {
        return 8;
    }
    if (integer.hash_code() == character.hash_code()) {
        return 9;
    }
    if (integer.hash_code() == first.hash_code()) {
        return 10;
    }
    if (integer.name() == nullptr || character.name() == nullptr) {
        return 11;
    }

    // Equal descriptors hash equally, and distinct ones do not.
    if (first.hash_code() != typeid(First).hash_code()) {
        return 6;
    }
    if (first.hash_code() == second.hash_code()) {
        return 7;
    }
    return 42;
}