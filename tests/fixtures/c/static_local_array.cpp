struct S { int value; S() : value(7) {} };
int get() { static S items[2]; return items[1].value; }
int main() { return get(); }
