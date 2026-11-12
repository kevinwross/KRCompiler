struct S { operator double() const { return 1.5; } };
double get() { S s; return s; }
int main() { return get() > 1.0 ? 0 : 1; }
