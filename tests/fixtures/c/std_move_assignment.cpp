struct S { int value; S(int input) : value(input) {} S &operator=(S &&other) { value = other.value; other.value = 0; return *this; } };
int main() { S first(1); S second(7); first = std::move(second); return first.value; }
