struct S { int value; S(int input) : value(input) {} S operator++(int); };
S S::operator++(int) { S copy(value); value++; return copy; }
int main() { S s(1); S old = s++; return old.value; }
