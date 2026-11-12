struct S { static int destroyed; S() {} ~S() { destroyed++; } };
int S::destroyed = 0;
int f() { S items[2]; return 0; }
int main() { f(); return S::destroyed; }
