#include <tuple>

int main() {
    std::tuple<int, int> two = std::make_tuple(40, 2);
    if (std::tuple_size(two) != 2) { return 1; }
    if (std::tuple_head(two) != 40) { return 2; }
    if (std::tuple_head(std::tuple_tail(two)) != 2) { return 3; }
    return 42;
}