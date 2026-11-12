struct S { static int destroyed; S() {} ~S() { destroyed++; } };
int S::destroyed = 0;
int main() { { S items[2]; } return S::destroyed; }
