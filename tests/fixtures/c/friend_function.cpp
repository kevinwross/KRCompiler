struct S { int value; S() : value(7) {} friend int get(S const &object); };
int get(S const &object) { return object.value; }
int main() { S s; return get(s); }
