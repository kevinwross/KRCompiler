struct S { int v; int get(); };
int S::get() { return v; }
int main() { S s; s.v = 7; return s.get(); }
