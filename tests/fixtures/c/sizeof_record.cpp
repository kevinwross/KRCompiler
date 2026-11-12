struct S { int value; };
static_assert(sizeof(S) == 4, "size");
int main() { return sizeof(S) == 4 ? 7 : 1; }
