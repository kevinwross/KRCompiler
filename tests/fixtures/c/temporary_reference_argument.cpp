struct S { int value; S(int input) : value(input) {} };
S make() { return {7}; }
int take(S const &input) { return input.value; }
int main() { return take(make()); }
