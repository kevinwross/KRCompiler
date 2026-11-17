// The element constructor: `tuple<int, char>(41, 'x')` builds a tuple whose head and
// tail both hold what the call passed. The constructor's signature carries the class
// template's pack, so an instance takes one argument per element and the initializer
// forwards the remainder to the tail.
//
// This is worth a fixture because the failure mode is silent. Before the member's
// pack was expanded per element, the head was initialised correctly and the tail kept
// its default -- the program compiled, linked and ran, and returned the wrong number.
// Returns 42.

#include <tuple>

int main() {
    tuple<int, char> two(41, 'x');
    if (tuple_head(two) != 41) { return 1; }
    if (two.tail.head != 'x') { return 2; }
    if (tuple_size(two) != 2) { return 3; }

    tuple<int, char, long> three(1, 'a', 2);
    if (tuple_head(three) != 1) { return 4; }
    if (three.tail.head != 'a') { return 5; }
    if (three.tail.tail.head != 2) { return 6; }
    if (tuple_size(three) != 3) { return 7; }

    // The one-argument case leaves an empty tail, which must not need a constructor
    // call of its own here.
    tuple<long> one(9);
    if (tuple_head(one) != 9) { return 8; }
    return 42;
}
