struct S { static int get(); };
int S::get() { return 7; }
int main() { return S::get(); }
