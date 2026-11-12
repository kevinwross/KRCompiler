struct S { int *pointer; S(int *input) : pointer(input) {} };
int main() { S s = nullptr; return s.pointer == nullptr ? 0 : 1; }
