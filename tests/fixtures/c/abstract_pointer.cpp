struct B { virtual int f() = 0; virtual ~B() {} };
struct D : B { int f() override { return 7; } };
int main() { D d; B *pointer = &d; return pointer->f(); }
