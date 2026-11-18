#include <tuple>

// The empty tuple as a local. Its record is the empty primary, which declares no
// constructor of its own; the call used to bind to the partial specialisation's pattern
// constructor, which is only ever declared, so the image failed with
// `undefined symbol 'tuple_ctor_0_value'`.
int main() {
    std::tuple<> empty;
    if (std::tuple_size(empty) != 0) { return 1; }

    std::tuple<int, int> two = std::make_tuple(40, 2);
    if (std::tuple_size(two) != 2 || std::tuple_head(two) != 40) { return 2; }
    return 42;
}