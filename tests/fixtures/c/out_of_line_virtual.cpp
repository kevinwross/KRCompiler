struct B { virtual int f(); };
int B::f() { return 7; }
int main() { B b; B *pointer = &b; return pointer->f(); }
