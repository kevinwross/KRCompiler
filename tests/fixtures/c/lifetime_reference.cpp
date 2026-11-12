struct B { int value; B(int input) : value(input) {} };
B make() { return {7}; }
int main() { B const &reference = make(); return reference.value; }
