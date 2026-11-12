struct S { int value; constexpr S(int input) : value(input) {} };
constexpr S object(7);
int main() { return object.value; }
