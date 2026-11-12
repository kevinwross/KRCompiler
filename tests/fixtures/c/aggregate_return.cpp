struct P { int x; int y; };
P make() { return {3, 4}; }
int main() { P result = make(); return result.x + result.y; }
