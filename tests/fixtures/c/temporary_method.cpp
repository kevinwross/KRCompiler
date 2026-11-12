struct S { int value; S(int input) : value(input) {} int get() { return value; } };
S make() { return {7}; }
int main() { return make().get(); }
