struct T { int v; T(int a) = delete; };
int main() { int value = 5; T object = value; return 0; }
