struct S { double value; S(double input) : value(input) {} operator double() const { return value; } };
S make() { return {7.5}; }
int main() { double result = static_cast<double>(make()); return result > 7.0 ? 7 : 1; }
