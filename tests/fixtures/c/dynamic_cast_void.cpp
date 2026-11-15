#include <typeinfo>
struct Base { virtual ~Base() {} };
struct Derived : Base { int value; };
int main() {
    Derived object;
    object.value = 5;
    Base* base = &object;

    // The complete object is the derived object itself.
    void* complete = dynamic_cast<void*>(base);
    if (complete != &object) { return 1; }

    // A null source yields null.
    Base* missing = 0;
    if (dynamic_cast<void*>(missing) != 0) { return 2; }

    return 42;
}