// A template declared inside a namespace and called through it. Templates are
// registered under the name their declaration spells, so the call has to fall
// back to the unqualified tail or the pattern is never found and the call
// targets a symbol nothing defines. Returns 42.

namespace ns {
struct Item {
    int value;
    Item(int start) : value(start) {}
};

template <typename T, typename... Args>
T* construct(Args... args) {
    return new T(args...);
}

template <typename T>
T* single(int start) {
    return new T(start);
}
}  // namespace ns

int main() {
    ns::Item* one = ns::construct<ns::Item>(41);
    if (one->value != 41) { return 1; }
    ns::Item* two = ns::single<ns::Item>(41);
    if (two->value != 41) { return 2; }
    return 42;
}