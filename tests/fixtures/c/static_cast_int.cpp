struct S { int value; S(int input) : value(input) {} operator int() const { return value; } };
int main() { S s(7); return static_cast<int>(s); }
