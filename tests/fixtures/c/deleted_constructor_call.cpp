struct T { int v; T(int a) = delete; };
int main() { T object(5); return 0; }
