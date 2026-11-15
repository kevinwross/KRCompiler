// The function objects from <functional>. Each takes its operands by const
// reference, so the call passes their addresses. Returns 42.

#include <functional>

int main() {
    std::plus<int> add;
    std::minus<int> subtract;
    std::multiplies<int> multiply;
    std::divides<int> divide;
    std::negate<int> negate_value;
    std::equal_to<int> equal_values;
    std::not_equal_to<int> not_equal_values;
    std::less<int> is_less;
    std::greater<int> is_greater;
    std::less_equal<int> is_less_equal;
    std::greater_equal<int> is_greater_equal;
    std::logical_and<int> both;
    std::logical_or<int> either;
    std::logical_not<int> is_zero;

    if (add(20, 22) != 42) { return 1; }
    if (subtract(50, 8) != 42) { return 2; }
    if (multiply(6, 7) != 42) { return 3; }
    if (divide(84, 2) != 42) { return 4; }
    if (negate_value(-42) != 42) { return 5; }
    if (!equal_values(42, 42)) { return 6; }
    if (!not_equal_values(1, 2)) { return 7; }
    if (!is_less(41, 42)) { return 8; }
    if (!is_greater(43, 42)) { return 9; }
    if (!is_less_equal(42, 42)) { return 10; }
    if (!is_greater_equal(42, 42)) { return 11; }
    if (!both(1, 1)) { return 12; }
    if (!either(0, 1)) { return 13; }
    if (!is_zero(0)) { return 14; }

    // An lvalue operand passes its address; a literal is materialised.
    int forty_two = 42;
    if (!equal_values(forty_two, 42)) { return 15; }
    return 42;
}