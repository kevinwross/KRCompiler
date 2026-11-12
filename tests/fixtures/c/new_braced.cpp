struct S { int value; S(int input) : value(input) {} };
int main() { S *pointer = new S{7}; int result = pointer->value; delete pointer; return result; }
