struct P { int x; int y; };
int main() { P *values = new P[2]{{1, 2}, {3, 4}}; int result = values[1].y; delete[] values; return result; }
