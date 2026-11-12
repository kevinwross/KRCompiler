struct Inner { static int destroyed; Inner() {} ~Inner() { destroyed++; } };
int Inner::destroyed = 0;
struct Outer { Inner items[2]; };
int main() { { Outer out; } return Inner::destroyed; }
