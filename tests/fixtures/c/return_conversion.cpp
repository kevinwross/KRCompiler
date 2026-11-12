struct S { int value; S(int input) : value(input) {} operator int() const { return value; } };
int get() { S s(7); return s; }
int main() { return get(); }
