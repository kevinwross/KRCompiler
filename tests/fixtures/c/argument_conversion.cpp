struct S { int value; S(int input) : value(input) {} operator int() const { return value; } };
int consume(int value) { return value; }
int main() { S s(7); return consume(s); }
