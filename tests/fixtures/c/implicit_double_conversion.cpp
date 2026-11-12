struct S { operator double() const { return 1.5; } };
int main() { S s; double d = s; return d > 1.0 ? 0 : 1; }
