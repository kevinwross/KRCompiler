struct S { bool value; S(bool input) : value(input) {} operator bool() const { return value; } };
S make() { return {false}; }
int main() { return (!make()) ? 7 : 1; }
