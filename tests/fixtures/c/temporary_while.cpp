struct S { bool value; S(bool input) : value(input) {} operator bool() const { return value; } };
static int calls;
S make() { return {calls++ == 0}; }
int main() { while (make()) { } return 7; }
