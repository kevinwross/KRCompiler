struct Inner { static int destroyed; ~Inner() { destroyed++; } };
int Inner::destroyed = 0;
struct Outer { Inner items[2]; };
int main() { Outer *pointer = new Outer[2]; delete[] pointer; return Inner::destroyed; }
