// A function template whose *parameter type* is a class template-id carrying a
// pack. Two things had to agree before this worked. Sema must deduce the
// function's parameters from the argument's instance -- the specifier spells
// `Tup<T, Rest...>`, not `T` -- and the pattern's body must not be analysed at
// all, because `Tup<T, Rest...>` is not a record until the pack is bound.
//
// `tuple_arity` is the second half's check: a `sizeof...(Rest)` in the body folds
// to the instance's arity, which the pattern has no value for. `tuple_head` and
// `tuple_tail` are the first half's, and the write through `tuple_head(pair)`
// shows the reference reaches the object rather than a copy.
//
// Not asserted here: a reference-returning call passed *directly* as an argument
// (`tuple_head(tuple_tail(pair))`) crashes the compiler, on a shape with no
// template in it; the reproduction is recorded in implementation.md and the
// member form (`tuple_tail(pair).head`) is used instead. Returns 42.

template <typename... Ts>
struct Tup { };

template <typename T, typename... R>
struct Tup<T, R...> {
    T head;
    Tup<R...> tail;
};

template <typename T, typename... R>
T& tuple_head(Tup<T, R...>& value) {
    return value.head;
}

template <typename T, typename... R>
Tup<R...>& tuple_tail(Tup<T, R...>& value) {
    return value.tail;
}

template <typename T, typename... R>
int tuple_arity(Tup<T, R...>& value) {
    return 1 + sizeof...(R);
}

int main() {
    Tup<int, char> pair;
    pair.head = 41;
    pair.tail.head = 'x';

    if (tuple_head(pair) != 41) { return 1; }
    if (tuple_arity(pair) != 2) { return 2; }
    if (tuple_tail(pair).head != 'x') { return 3; }

    // The shorter instance is deduced from a member's type, and its arity is 1.
    if (tuple_arity(pair.tail) != 1) { return 4; }

    // A write through the returned reference reaches the object.
    tuple_head(pair) = 7;
    if (pair.head != 7) { return 5; }
    return 42;
}
