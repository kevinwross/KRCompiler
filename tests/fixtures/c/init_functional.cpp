struct S { float value; S(float input) : value(input) {} };
int main() { S s = S(1.5f); return s.value > 1.0f ? 0 : 1; }
