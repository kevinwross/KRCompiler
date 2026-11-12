struct S { bool value; S(bool input) : value(input) {} operator bool() const { return value; } };
int main() { S yes(true); S no(false); if (yes && !no) { return 7; } return 1; }
