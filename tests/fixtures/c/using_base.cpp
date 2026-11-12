struct B { int f() { return 7; } };
struct D : B { using B::f; };
int main() { D d; return d.f(); }
