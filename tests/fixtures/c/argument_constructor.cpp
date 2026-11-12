struct S { int value; S(int input) : value(input) {} };
int take(S s) { return s.value; }
int main() { return take(7); }
