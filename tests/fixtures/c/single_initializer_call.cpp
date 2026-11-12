static int counter;
static int bump() { return ++counter; }
int main() { int first = bump(); int second = bump(); return first == 1 && second == 2 && counter == 2 ? 7 : 1; }
