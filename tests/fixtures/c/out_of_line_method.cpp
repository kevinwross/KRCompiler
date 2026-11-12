struct S { int get(); };
int S::get() { return 7; }
int main() { S s; return s.get(); }
