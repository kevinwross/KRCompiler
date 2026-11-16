// <tuple> on the named API: `tuple_size`, `tuple_head`, `tuple_tail`, and the
// members `.head` / `.tail`.
//
// There is no element constructor -- a *class member* whose signature carries a
// pack parameter is expanded for one element only, so `tuple<int, char> p(41, 'x')`
// compiles and then fails to link -- so every tuple here is default-constructed and
// assigned, which works at any arity. The empty tuple is the primary, and a
// default-constructed one is value-initialised through every level. Returns 42.

#include <tuple>

int main() {
    // Two elements, default construct and assign.
    std::tuple<int, char> pair;
    pair.head = 41;
    pair.tail.head = 'x';
    if (std::tuple_size(pair) != 2) { return 1; }
    if (std::tuple_head(pair) != 41) { return 2; }
    if (std::tuple_tail(pair).head != 'x') { return 3; }
    if (pair.head != 41) { return 4; }
    if (pair.tail.head != 'x') { return 5; }

    // Three elements, the same way.
    std::tuple<int, char, long> three;
    three.head = 1;
    three.tail.head = 'y';
    three.tail.tail.head = 9;
    if (std::tuple_size(three) != 3) { return 6; }
    if (three.head != 1) { return 7; }
    if (three.tail.head != 'y') { return 8; }
    if (three.tail.tail.head != 9) { return 9; }

    std::tuple<int> one;
    one.head = 5;
    if (std::tuple_size(one) != 1) { return 10; }
    if (one.head != 5) { return 11; }

    // The empty tuple, reached as a member rather than as a local: declaring a
    // `std::tuple<>` local emits a call to the pattern's constructor, whose symbol
    // nothing defines (`undefined symbol 'tuple_ctor_0_value'`), which the header
    // records as a limit.
    if (std::tuple_size(pair.tail.tail) != 0) { return 12; }

    // A default-constructed tuple is value-initialised through every level.
    std::tuple<int, char> zero;
    if (zero.head != 0) { return 13; }
    if (zero.tail.head != 0) { return 14; }

    // A write through each accessor reaches the object.
    std::tuple_head(pair) = 7;
    if (pair.head != 7) { return 15; }
    std::tuple_tail(pair).head = 'z';
    if (pair.tail.head != 'z') { return 16; }
    return 42;
}
