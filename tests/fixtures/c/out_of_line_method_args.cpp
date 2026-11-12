struct S { int add(int input); };
int S::add(int input) { return input + 1; }
int main() { S s; return s.add(2); }
