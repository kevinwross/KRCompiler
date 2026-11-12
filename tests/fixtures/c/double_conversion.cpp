struct S { operator double() const { return 1.5; } };
int main() { S s; double d = static_cast<double>(s); return d > 1.0 ? 0 : 1; }
