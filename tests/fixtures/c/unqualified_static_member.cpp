struct S { static int destroyed; ~S() { destroyed++; } };
int S::destroyed = 0;
int main() { S *items = new S[2]; delete[] items; return S::destroyed; }
