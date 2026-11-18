// A value template parameter is deduced when the argument's type records the instance tag
// and that tag carries nothing but the value: `Fixed<2>` records as `Fixed__2`, so
// `int f(Fixed<N>&)` binds N to 2. This fixture uses `std::array<T, N>`, whose tag also
// carries the element type, and that is **not** deduced yet -- so the call must still name
// the parameter rather than leave an undefined symbol for the linker to find. If deduction
// is later extended to instances that carry other parameters too, this fixture becomes
// wrong and should turn into a passing test.

#include <array>

template <typename T, int N>
int count_of(const std::array<T, N>& values) {
    return values.size();
}

int main() {
    std::array<int, 3> values;
    if (count_of(values) != 3) {
        return 1;
    }
    return 42;
}