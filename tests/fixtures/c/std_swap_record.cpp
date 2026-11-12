struct S { int value; };
int main() { S left; left.value = 1; S right; right.value = 7; std::swap(left, right); return left.value; }
