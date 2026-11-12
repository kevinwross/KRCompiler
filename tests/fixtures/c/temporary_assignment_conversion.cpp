struct S { int value; S(int input) : value(input) {} operator int() const { return value; } };
S make() { return {7}; }
int main() { int result = 0; result = make(); return result; }
