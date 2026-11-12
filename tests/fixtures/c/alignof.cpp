struct S { int value; double other; };
static_assert(alignof(S) >= 4, "alignment");
int main() { return alignof(S) >= 4 ? 0 : 1; }
