struct S { int value; mutable int calls; S() : value(7), calls(0) {} int get() const { calls++; return value; } };
int main() { S s; return s.get() == 7 ? s.calls : 1; }
