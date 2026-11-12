struct S { double value; S(double input) : value(input) {} };
int main() { S s(1.5); return s.value > 1.0 ? 0 : 1; }
