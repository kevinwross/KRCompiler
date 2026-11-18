#include <tuple>

// Four shapes of the value-parameter deduction added in 706109b, which binds a non-type
// template parameter from the argument's recorded instance tag (`Fixed<3>` records as
// `Fixed__3`). Each check returns a distinct code so a failure names the shape: the feature
// is one feature, but the shapes reach it by different routes -- by value, through a
// pointer, across two parameters, and alongside a pack.
//
// Every value here is chosen so a binding hard-coded to another shape's number cannot pass.

template <int N> struct Fixed { int v; };

template <int N> int by_value(Fixed<N> value) { return N + value.v - 42; }

template <int N> int by_pointer(Fixed<N>* value) { return N + (*value).v - 42; }

template <int A, int B> int two(Fixed<A>& a, Fixed<B>& b) {
    return A + B + a.v + b.v - 84;
}

template <int N, typename... Ts> int with_pack(Fixed<N>& v, Ts... rest) {
    return N + v.v - 42 + (int)sizeof...(Ts);
}

int main() {
    Fixed<3> three;
    three.v = 42;
    if (by_value(three) != 3) { return 1; }

    Fixed<4> four;
    four.v = 42;
    if (by_pointer(&four) != 4) { return 2; }

    Fixed<2> two_a;
    Fixed<5> five;
    two_a.v = 42;
    five.v = 42;
    if (two(two_a, five) != 7) { return 3; }

    Fixed<6> six;
    six.v = 42;
    if (with_pack(six, 1, 2) != 8) { return 4; }

    return 42;
}
