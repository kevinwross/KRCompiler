namespace A { inline namespace B { int f() { return 7; } } }
int main() { return A::f(); }
