struct Inner { int value; Inner() : value(7) {} };
struct Outer { Inner items[2]; };
Outer global;
int main() { return global.items[1].value; }
