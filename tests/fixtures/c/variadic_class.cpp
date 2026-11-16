// A class template whose only parameter is a pack, instantiated at arity 1, 2 and
// 3. Three things had to agree for this to work: the parser records which
// parameter is a pack, the instance tag carries the arity (or every instance
// collapses onto the first element's tag), and the label is read from the
// definition rather than only its first child. Returns 42.

template <typename... Ts>
struct Pack {
    int count;
    Pack() : count(sizeof...(Ts)) {}
};

int main() {
    Pack<int> one;
    Pack<int, char> two;
    Pack<int, char, long> three;
    if (one.count != 1) { return 1; }
    if (two.count != 2) { return 2; }
    if (three.count != 3) { return 3; }
    return 42;
}