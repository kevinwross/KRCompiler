struct S { int value; S(int input) : value(input) {} operator int() const { return value; } };
S make() { return {7}; }
int take(int input) { return input; }
int main() { return take(make()); }
