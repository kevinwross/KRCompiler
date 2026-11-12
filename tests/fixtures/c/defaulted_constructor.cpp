struct S { int value; S() = default; };
int main() { S s; s.value = 7; return s.value; }
