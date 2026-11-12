struct S { operator int() const { return 0; } };
int main() { S s; if (!s) { return 0; } return 1; }
