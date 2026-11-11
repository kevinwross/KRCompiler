class Base { public: Base() { value = 5; } int value; };
class Derived : public Base { public: Derived() { } };
int main() { Derived d; return d.value; }
