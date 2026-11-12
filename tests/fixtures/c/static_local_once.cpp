struct S { static int constructed; S() { constructed++; } };
int S::constructed = 0;
void touch() { static S object; }
int main() { touch(); touch(); return S::constructed; }
