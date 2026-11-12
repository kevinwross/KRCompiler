struct S { operator float() const { return 1.5f; } };
int main() { S s; float f = static_cast<float>(s); return f > 1.0f ? 0 : 1; }
