class Base { public: Base() { value = 5; } int value; };
class Derived : public Base { public: int extra = 7; };
Derived global;
int main() { return global.value + global.extra; }
