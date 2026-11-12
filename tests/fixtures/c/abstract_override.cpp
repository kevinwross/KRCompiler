struct B { virtual int f() = 0; };
struct D : B { int f() override { return 7; } };
int main() { D d; return d.f(); }
