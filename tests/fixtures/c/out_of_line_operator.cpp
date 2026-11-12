struct S { int value; S &operator=(S const &other); };
S &S::operator=(S const &other) { value = other.value; return *this; }
int main() { S a; a.value = 3; S b; b = a; return b.value; }
