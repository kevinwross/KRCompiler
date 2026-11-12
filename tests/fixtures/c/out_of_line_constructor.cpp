struct S { int value; S(int input); };
S::S(int input) : value(input) {}
int main() { S s(3); return s.value; }
