// A recursive variadic class template's member written as a pack expansion
// (`Tup<R...> tail`) must name the *tail* of the pack, not the enclosing
// instance and not the empty primary. Resolving it needs the pack's arity and
// its element bindings, which are live only while the instance is analysed, so
// the expansion happens where a specifier's arguments are collected.
//
// The two sizes pin the selection rule: an empty pack is the primary (zero
// bytes), while a pack that receives one argument is still the specialization,
// because its fixed parameter takes that argument and the pack absorbs nothing.
// `t.tail.head` pins the same fact at compile time -- only the specialization
// has a `head` member -- and the round trip shows the two subobjects are
// distinct. Returns 42.

template <typename... Ts>
struct Tup { };

template <typename T, typename... R>
struct Tup<T, R...> {
    T head;
    Tup<R...> tail;
};

int main() {
    if (sizeof(Tup<>) != 0) { return 1; }
    if (sizeof(Tup<char>) != 1) { return 2; }
    if (sizeof(Tup<int>) != sizeof(int)) { return 3; }

    Tup<int, char> pair;
    pair.head = 7;
    pair.tail.head = 'x';
    if (pair.head != 7) { return 4; }
    if (pair.tail.head != 'x') { return 5; }

    Tup<int, char, long> three;
    three.head = 1;
    three.tail.head = 'y';
    three.tail.tail.head = 9;
    if (three.head != 1) { return 6; }
    if (three.tail.head != 'y') { return 7; }
    if (three.tail.tail.head != 9) { return 8; }
    return 42;
}
