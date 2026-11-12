struct S { int value; S() : value(7) {} };
S const instance;
int main() { return instance.value; }
