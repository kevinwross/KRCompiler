struct S { bool value; S(bool input) : value(input) {} operator bool() const { return value; } };
S make() { return {true}; }
int main() { return (make() && true) ? 7 : 1; }
