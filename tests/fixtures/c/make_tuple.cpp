#include <tuple>

// `make_tuple` returns a tuple by value from a function template whose signature carries
// the class template's pack. Two elements exercise the factory; three exercise the pack
// expansion in the signature, where every duplicate used to be inserted at the same
// position, so a three-element instance arrived with its second and third parameters
// swapped. A mixed pack exercises the instance name: it carries each element's type, so
// `tuple<int, int>` and `tuple<int, char>` from two calls are two instances rather than
// one.
int main() {
    std::tuple<int, int> two = std::make_tuple(40, 2);
    if (std::tuple_size(two) != 2) { return 1; }
    if (std::tuple_head(two) != 40) { return 2; }
    if (std::tuple_head(std::tuple_tail(two)) != 2) { return 3; }

    std::tuple<int, int, int> three = std::make_tuple(7, 8, 27);
    if (std::tuple_size(three) != 3) { return 4; }
    if (std::tuple_head(three) != 7) { return 5; }
    if (std::tuple_head(std::tuple_tail(three)) != 8) { return 6; }
    if (std::tuple_head(std::tuple_tail(std::tuple_tail(three))) != 27) { return 7; }

    std::tuple<int, char> mixed = std::make_tuple(41, 'a');
    if (std::tuple_size(mixed) != 2) { return 8; }
    if (std::tuple_head(mixed) != 41) { return 9; }
    if (std::tuple_head(std::tuple_tail(mixed)) != 'a') { return 10; }
    return 42;
}