struct S { operator int() const { return 1; } };
int main() { S s; if (s) { return 0; } return 1; }
