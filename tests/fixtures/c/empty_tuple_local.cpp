#include <tuple>

// The empty tuple as a local. Its record is the empty primary, which declares no
// constructor of its own; the call used to bind to the partial specialisation's pattern
// constructor, which is only ever declared, so the image failed with
// `undefined symbol 'tuple_ctor_0_value'`. Both the local and the placement-new form are
// covered, because they reach the constructor through different IR paths.
int main() {
    std::tuple<> empty;
    if (std::tuple_size(empty) != 0) { return 1; }

    char buffer[16];
    std::tuple<>* placed = new (buffer) std::tuple<>();
    if (std::tuple_size(*placed) != 0) { return 2; }

    std::tuple<int, int> two = std::make_tuple(40, 2);
    if (std::tuple_size(two) != 2 || std::tuple_head(two) != 40) { return 3; }
    return 42;
}