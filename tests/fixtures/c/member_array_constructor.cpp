struct Inner { int value; Inner() : value(7) {} };
struct Outer { Inner items[2]; };
int main() { Outer out; return out.items[1].value; }
