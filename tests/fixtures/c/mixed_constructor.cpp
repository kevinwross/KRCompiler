struct S { int first; float second; S(int a, float b) : first(a), second(b) {} };
int main() { S s(1, 1.5f); return (s.first == 1 && s.second > 1.0f) ? 0 : 1; }
