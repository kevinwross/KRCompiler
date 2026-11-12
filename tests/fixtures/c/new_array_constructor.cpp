struct S { int value; S() : value(7) {} };
int main() { S *items = new S[2]; int result = items[1].value; delete[] items; return result; }
