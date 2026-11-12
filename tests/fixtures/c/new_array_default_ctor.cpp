struct S { int value; S() : value(7) {} };
int main() { S *values = new S[2]{}; int result = values[1].value; delete[] values; return result; }
