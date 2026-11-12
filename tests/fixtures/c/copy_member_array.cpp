struct Inner { int value; };
struct Outer { Inner items[2]; };
int main() { Outer first; first.items[0].value = 7; Outer second = first; return second.items[0].value; }
