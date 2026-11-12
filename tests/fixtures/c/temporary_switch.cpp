struct S { int value; S(int input) : value(input) {} operator int() const { return value; } };
S make() { return {7}; }
int main() { switch (make()) { case 7: return 7; default: return 1; } }
