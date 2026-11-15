// A value template parameter is not deduced from an argument's type, so a call
// that omits it cannot pick an instance. This fixture exists to be rejected: the
// compiler must name the parameter at the call site rather than leave an
// undefined symbol for the linker to find.

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