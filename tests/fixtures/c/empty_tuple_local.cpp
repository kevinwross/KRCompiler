#include <tuple>

// The empty tuple as a local. Its record is the empty primary, which declares no
// constructor of its own; the call used to bind to the partial specialisation's pattern
// constructor, which is only ever declared, so the image failed with
// `undefined symbol 'tuple_ctor_0_value'`. Three forms are covered -- the local, the
// placement-new form and a functional-cast temporary -- because each reaches that
// constructor through a different IR path and one guard did not cover the others.
int main() {
    std::tuple<> empty;
    if (std::tuple_size(empty) != 0) { return 1; }

    char buffer[16];
    std::tuple<>* placed = new (buffer) std::tuple<>();
    if (std::tuple_size(*placed) != 0) { return 2; }

    std::tuple<> built = std::tuple<>();
    if (std::tuple_size(built) != 0) { return 3; }

    std::tuple<int, int> two = std::make_tuple(40, 2);
    if (std::tuple_size(two) != 2 || std::tuple_head(two) != 40) { return 4; }
    return 42;
}