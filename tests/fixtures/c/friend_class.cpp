struct A;
struct B { int get(A const &object); };
struct A { int value; A() : value(7) {} friend struct B; };
int B::get(A const &object) { return object.value; }
int main() { A a; B b; return b.get(a); }
