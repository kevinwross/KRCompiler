struct S { int value; S(int input) : value(input) {} operator int() const { return value; } };
S make() { return {7}; }
int main() { int result = make(); return result; }
