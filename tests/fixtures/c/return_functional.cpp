struct S { float value; S(float input) : value(input) {} };
S make() { return S(1.5f); }
int main() { S s = make(); return s.value > 1.0f ? 0 : 1; }
