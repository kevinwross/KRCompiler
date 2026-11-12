struct Inner { int value; Inner() : value(7) {} };
struct Outer { Inner items[2]; };
int main() { Outer *pointer = new Outer[2]; int result = pointer[1].items[1].value; delete[] pointer; return result; }
