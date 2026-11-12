struct S { int value; S(int input) : value(input) {} };
S make() { return 7; }
int main() { S s = make(); return s.value; }
