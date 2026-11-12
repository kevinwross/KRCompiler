struct S { static int add(int input); };
int S::add(int input) { return input + 1; }
int main() { return S::add(2); }
