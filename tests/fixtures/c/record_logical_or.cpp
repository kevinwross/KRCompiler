struct S { bool value; S(bool input) : value(input) {} operator bool() const { return value; } };
int main() { S yes(true); S no(false); return (no || yes) ? 7 : 1; }
