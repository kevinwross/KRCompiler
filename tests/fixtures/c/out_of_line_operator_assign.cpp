struct S { int value; S(int input) : value(input) {} S &operator+=(S const &other); };
S &S::operator+=(S const &other) { value += other.value; return *this; }
int main() { S a(2); S b(3); a += b; return a.value; }
