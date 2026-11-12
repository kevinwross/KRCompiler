struct S { int value; S(int input) : value(input) {} S(S &&other) : value(other.value) { other.value = 0; } };
int main() { S first(7); S second = std::move(first); return second.value; }
