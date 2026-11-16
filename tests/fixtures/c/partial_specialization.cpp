// A partial specialization selected by argument count. The primary handles the
// empty pack; the specialization fixes one parameter and lets its pack absorb the
// rest, so its sizeof...(Rest) is one less than the primary's sizeof...(Ts).
// That difference is what proves the specialization was chosen. Returns 42.

template <typename... Ts>
struct Holder {
    int count;
    Holder() : count(sizeof...(Ts)) {}
};

template <typename T, typename... Rest>
struct Holder<T, Rest...> {
    T head;
    int count;
    Holder() : head(), count(sizeof...(Rest)) {}
};

int main() {
    Holder<int, char> two;
    if (two.count != 1) { return 1; }
    Holder<int, char, long> three;
    if (three.count != 2) { return 2; }
    return 42;
}