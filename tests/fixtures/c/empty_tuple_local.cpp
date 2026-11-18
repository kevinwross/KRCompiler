#include <tuple>

// The empty tuple as a local. Its record is the empty primary, which declares no
// constructor of its own; the call used to bind to the partial specialisation's pattern
// constructor, which is only declared, so the image failed to link with
// `undefined symbol 'tuple_ctor_0_value'`.
int main() {
    std::tuple<> empty;
    (void)empty;

    std::tuple<int, int> two = std::make_tuple(40, 2);
    if (std::tuple_size(two) != 2 || std::tuple_head(two) != 40) { return 1; }
    return 42;
}