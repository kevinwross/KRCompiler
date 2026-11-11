class Inner { public: Inner() { value = 5; } int value; };
class Outer { public: Outer() { } Inner inner; };
int main() { Outer o; return o.inner.value; }
