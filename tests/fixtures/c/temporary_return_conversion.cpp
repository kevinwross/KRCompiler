struct S { int value; S(int input) : value(input) {} operator int() const { return value; } };
S make() { return {7}; }
int convert() { return make(); }
int main() { return convert(); }
