struct S { static int value; static int get() { return value; } };
int S::value = 7;
int main() { return S::get(); }
