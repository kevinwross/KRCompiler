struct S { bool value; S(bool input) : value(input) {} };
int main() { S s = true; return s.value ? 0 : 1; }
