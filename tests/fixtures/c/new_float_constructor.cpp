struct S { float value; S(float input) : value(input) {} };
int main() { S *p = new S(1.5f); float v = p->value; delete p; return v > 1.0f ? 0 : 1; }
