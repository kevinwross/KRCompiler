struct S { operator double() const { return 1.5; } };
double consume(double value) { return value; }
int main() { S s; return consume(s) > 1.0 ? 0 : 1; }
