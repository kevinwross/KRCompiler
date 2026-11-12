struct S { static int constructed; S() { constructed++; } };
int S::constructed = 0;
void touch() { static S items[2]; }
int main() { touch(); touch(); return S::constructed; }
