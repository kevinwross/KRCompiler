int f(int a, float b, int c) { return a + (b > 1.0f ? 1 : 0) + c; }
int main() { return f(1, 1.5f, 2); }
