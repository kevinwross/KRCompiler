struct S { int value; S() : value(7) {} };
int get() { static S object; return object.value; }
int main() { return get(); }
