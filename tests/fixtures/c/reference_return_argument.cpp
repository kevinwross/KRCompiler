// A call that returns a reference, passed as an argument to another call.
//
// `head(pick(item))` emitted `pick` and never emitted `head`, so the comparison
// consuming the result was built with a null operand and the IR verifier rejected
// the program. A reference return already yields the address the parameter wants,
// so it binds directly; only a record returned by value needs a temporary.
//
// The same shape appears through <tuple>, where `tuple_head(tuple_tail(pair))`
// chained two reference-returning accessors. Returns 42.

#include <tuple>

struct Item {
    int v;
};

int& head(Item& item) {
    return item.v;
}

Item& pick(Item& item) {
    return item;
}

int main() {
    Item item;
    item.v = 42;
    if (head(pick(item)) != 42) { return 1; }

    // The callee must actually run: write through the returned reference and read
    // it back through a second call.
    head(pick(item)) = 7;
    if (item.v != 7) { return 2; }
    if (head(item) != 7) { return 3; }

    tuple<int, char> pair(41, 'x');
    if (tuple_head(tuple_tail(pair)) != 'x') { return 4; }
    if (tuple_head(pair) != 41) { return 5; }
    return 42;
}
