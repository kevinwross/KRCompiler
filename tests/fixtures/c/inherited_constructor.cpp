struct B { int value; B(int input) : value(input) {} };
struct D : B { using B::B; };
int main() { D d(7); return d.value; }
