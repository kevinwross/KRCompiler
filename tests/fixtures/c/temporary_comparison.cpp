struct S { int value; S(int input) : value(input) {} operator int() const { return value; } };
S make() { return {7}; }
int main() { return (make() == 7) ? 7 : 1; }
