struct S { bool value; S(bool input) : value(input) {} operator bool() const { return value; } };
S make() { return {true}; }
int main() { if (make()) { return 7; } return 1; }
