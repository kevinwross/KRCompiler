struct S { int value; S(int input) : value(input) {} };
int main() { char storage[sizeof(S)]; S *pointer = new (storage) S(7); return pointer->value; }
