#include <tuple>

// Every expanded parameter of a pack function template used to take the head element's
// type, so a value wider than the head was narrowed on the way in and the tuple carried a
// wrong value with no diagnostic: `make_tuple('a', 1000)` returned 3.
//
// These cases pin *widths*, not just types. Each stores a value that a head-typed
// parameter cannot represent, so a regression fails here rather than in a whole-tuple
// comparison -- the end-to-end cover in make_tuple.cpp would still pass if the element
// happened to be small enough to survive a truncation.
int main() {
    // A narrow head. The second parameter has to be wide enough to keep 1000.
    std::tuple<char, int> narrow = std::make_tuple('a', 1000);
    if (std::tuple_head(narrow) != 'a') { return 1; }
    if (std::tuple_head(std::tuple_tail(narrow)) != 1000) { return 2; }

    // A wide head, so the opposite direction keeps working.
    std::tuple<int, char> wide = std::make_tuple(1000, 'a');
    if (std::tuple_head(wide) != 1000) { return 3; }
    if (std::tuple_head(std::tuple_tail(wide)) != 'a') { return 4; }

    // Three elements behind a narrow head, which exercises the third parameter: only the
    // first duplicate was ever renamed, so a two-element case could not see it.
    std::tuple<char, int, int> three = std::make_tuple('a', 1000, 2000);
    if (std::tuple_head(three) != 'a') { return 5; }
    if (std::tuple_head(std::tuple_tail(three)) != 1000) { return 6; }
    if (std::tuple_head(std::tuple_tail(std::tuple_tail(three))) != 2000) { return 7; }

    return 42;
}
