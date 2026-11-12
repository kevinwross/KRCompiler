namespace A { int f() { return 7; } }
namespace B = A;
int main() { return B::f(); }
