// A call through operator() passes a reference parameter as the object's
// address. Promoting the loaded value to a pointer instead is what made a
// `const T&` operator() receive zero, while a by-value parameter worked.
// Returns 42.

template <typename T>
struct ByValue {
    T operator()(T left, T right) const { return left + right; }
};

template <typename T>
struct PlainRef {
    T operator()(const T& left, const T& right) const { return left + right; }
};

template <typename T>
struct OneRef {
    T operator()(const int& left, int right) const { return left + right; }
};

namespace ns {
template <typename T>
struct Namespaced {
    T operator()(const T& left, const T& right) const { return left + right; }
};
}  // namespace ns

int main() {
    ByValue<int> by_value;
    PlainRef<int> plain_ref;
    OneRef<int> one_ref;
    ns::Namespaced<int> namespaced;

    if (by_value(20, 22) != 42) {
        return 1;
    }
    if (plain_ref(20, 22) != 42) {
        return 2;
    }
    if (one_ref(20, 22) != 42) {
        return 3;
    }
    if (namespaced(20, 22) != 42) {
        return 4;
    }

    // An lvalue argument passes its address directly.
    int left = 20;
    int right = 22;
    if (plain_ref(left, right) != 42) {
        return 5;
    }
    return 42;
}