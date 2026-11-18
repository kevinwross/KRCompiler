#include <tuple>

// A value template parameter is deduced from an argument whose type names it. The call
// used to be refused outright -- "cannot deduce the value template parameter 'N'; write it
// explicitly" -- because the positional deduction loop returns early on a parameter whose
// type is a template-id, so nothing ever tried to bind it.
//
// The argument's type records the instance tag (`Fixed__2`), so the value is the integer
// after the pattern's name. Two different values are used because a single one would pass
// even if the binding were hard-coded to it.
template <int N> struct Fixed { int v; };

template <int N> int size_of(Fixed<N>& value) { return N + value.v - 42; }

int main() {
    Fixed<2> two;
    two.v = 42;
    if (size_of(two) != 2) { return 1; }

    Fixed<5> five;
    five.v = 42;
    if (size_of(five) != 5) { return 2; }

    // The argument is still usable afterwards, so deduction did not consume it.
    if (two.v != 42) { return 3; }
    return 42;
}
