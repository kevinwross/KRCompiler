struct B { int value() { return 7; } };
struct D final : B {};
int main() { D d; return d.value(); }
