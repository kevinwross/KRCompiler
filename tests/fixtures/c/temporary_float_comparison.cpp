struct S { double value; S(double input) : value(input) {} operator double() const { return value; } };
S make() { return {7.5}; }
int main() { return (make() > 1.0) ? 7 : 1; }
