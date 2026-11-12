struct B { virtual ~B() = 0; };
B::~B() {}
struct D : B { ~D() {} };
int main() { B *pointer = new D(); delete pointer; return 7; }
